#!/usr/bin/env python3
"""avvia_fermentazione_v6.py — invia profilo al fermentatore V6 via HA REST API.

V6 rispetto a V5:
- Topic profilo: fermentazione/v6/{fermentatore}/ha/cmd/profile_set
- Sequenza robusta: stop -> control_set -> profile_set
- `control_set` usa i campi firmware: control_mode + params (kp/ki/kd heat/cool).
- Supporta --fermentatore f1|f2 per scalabilità F2.
- Notification ID: f1_v6_avvio / f2_v6_avvio.
"""

import argparse
import datetime
import json
import os
import sys
import time
import urllib.request
import urllib.error

HA_CONFIG_FILE = "/config/scripts_py/mqtt_broker.json"
MQTT_QOS = 1
PROFILI_DIR = "/config/profili_fermentazione"

CONTROL_MODES = ("on_off", "on_off_isteresi", "P", "PI", "PID")


def topic_profilo(fermentatore: str) -> str:
    return f"fermentazione/v6/{fermentatore}/ha/cmd/profile_set"


def topic_control_set(fermentatore: str) -> str:
    return f"fermentazione/v6/{fermentatore}/ha/cmd/control_set"


def topic_stop(fermentatore: str) -> str:
    return f"fermentazione/v6/{fermentatore}/ha/cmd/stop"


def carica_broker_config():
    """Legge ha_url e ha_token da mqtt_broker.json."""
    with open(HA_CONFIG_FILE, "r") as fh:
        return json.load(fh)


def carica_e_converti(nome_profilo: str) -> list:
    """Carica il profilo JSON e converte duration_days/ramp_days → _hours."""
    slug = nome_profilo.strip().lower().replace(" ", "_").replace("-", "_")
    percorso = None
    for fname in os.listdir(PROFILI_DIR):
        if fname.lower().replace("-", "_") == slug + ".json":
            percorso = os.path.join(PROFILI_DIR, fname)
            break
    if percorso is None:
        print(f"ERRORE: profilo '{nome_profilo}' non trovato in {PROFILI_DIR}",
              file=sys.stderr)
        sys.exit(1)

    if not os.path.isfile(percorso):
        print(f"ERRORE: file profilo non trovato: {percorso}", file=sys.stderr)
        sys.exit(1)

    with open(percorso, "r", encoding="utf-8") as fh:
        dati = json.load(fh)

    profilo_raw = dati.get("profilo")
    if not profilo_raw:
        print(f"ERRORE: campo 'profilo' mancante in {percorso}", file=sys.stderr)
        sys.exit(1)

    steps = []
    for step in profilo_raw:
        s = dict(step)
        if "duration_days" in s:
            s["duration_hours"] = round(s.pop("duration_days") * 24, 4)
        if "ramp_days" in s:
            s["ramp_hours"] = round(s.pop("ramp_days") * 24, 4)
        steps.append(s)
    return steps


def pubblica(topic: str, payload_dict: dict, qos: int = MQTT_QOS, retain: bool = False):
    """Pubblica via HA REST API (servizio mqtt.publish). Nessuna dipendenza esterna."""
    cfg = carica_broker_config()
    ha_url = cfg.get("ha_url", "http://localhost:8123").rstrip("/")
    ha_token = cfg["ha_token"]

    payload_json = json.dumps(payload_dict, ensure_ascii=False, separators=(",", ":"))
    service_data = json.dumps({
        "topic": topic,
        "payload": payload_json,
        "qos": qos,
        "retain": retain,
    }).encode("utf-8")

    req = urllib.request.Request(
        f"{ha_url}/api/services/mqtt/publish",
        data=service_data,
        headers={
            "Authorization": f"Bearer {ha_token}",
            "Content-Type": "application/json",
        },
        method="POST",
    )
    try:
        with urllib.request.urlopen(req, timeout=15) as resp:
            resp.read()
    except urllib.error.HTTPError as exc:
        print(f"ERRORE HTTP {exc.code} da HA: {exc.read().decode()}", file=sys.stderr)
        return False, 0
    except Exception as exc:
        print(f"ERRORE chiamata HA: {exc}", file=sys.stderr)
        return False, 0
    return True, len(payload_json)


def normalize_control_mode(mode: str) -> str:
    """Mappa le etichette HA ai valori accettati dal firmware V6."""
    mapping = {
        "on_off": "on_off",
        "on_off_isteresi": "on_off_hyst_sym",
        "P": "P",
        "PI": "PI",
        "PID": "PID",
    }
    return mapping.get(mode, mode)


def build_control_set_payload(args) -> dict:
    """Crea payload control_set secondo il contratto firmware V6."""
    ts_now = int(datetime.datetime.now().timestamp())
    req_id = f"{args.fermentatore}-ctrl-{ts_now % 100000:05d}"
    return {
        "schema_version": "1.0",
        "request_id": req_id,
        "ts": ts_now,
        "control_mode": normalize_control_mode(args.modo_controllo),
        "params": {
            "kp_heat": float(args.kp_risc),
            "ki_heat": float(args.ki_risc),
            "kd_heat": float(args.kd_risc),
            "kp_cool": float(args.kp_frigo),
            "ki_cool": float(args.ki_frigo),
            "kd_cool": float(args.kd_frigo),
            # firmware >= 6.3.0: isteresi on/off (0,1-3,0 °C), salvata in NVS
            "isteresi": float(args.isteresi),
        },
    }


def build_profile_payload(args, steps: list) -> dict:
    """Crea payload profile_set con soli campi profilo letti dal firmware."""
    ts_now = int(datetime.datetime.now().timestamp())
    req_id = f"{args.fermentatore}-prof-{ts_now % 100000:05d}"
    return {
        "schema_version": "1.0",
        "request_id": req_id,
        "ts": ts_now,
        "profilo": args.profilo,
        "avvio_epoch": int(args.avvio_epoch),
        "steps": steps,
    }


def durata_ore(step: dict) -> float:
    return float(step.get("duration_hours", step.get("ramp_hours", 0)))


def crea_notifica_avvio(args, steps: list):
    """Crea persistent_notification in HA con riepilogo profilo V6."""
    cfg = carica_broker_config()
    ha_url = cfg.get("ha_url", "http://localhost:8123").rstrip("/")
    ha_token = cfg["ha_token"]

    avvio_dt = datetime.datetime.fromtimestamp(int(args.avvio_epoch))
    nome_profilo = args.profilo.upper().replace("_", " ")
    ore_totali = sum(durata_ore(s) for s in steps)
    fine_dt = datetime.datetime.fromtimestamp(
        int(args.avvio_epoch) + int(ore_totali * 3600)
    )

    lines = [f"## 🍺 {nome_profilo}\n"]
    lines.append(f"**Avvio:** {avvio_dt.strftime('%d/%m/%Y %H:%M')}\n")
    lines.append(
        f"**Fine stimata:** {fine_dt.strftime('%d/%m/%Y %H:%M')} "
        f"({round(ore_totali / 24, 1)} gg totali)\n\n---\n"
    )

    # Riepilogo parametri di controllo
    lines.append(f"**Modo controllo:** {args.modo_controllo}\n")
    if args.modo_controllo == "on_off_isteresi":
        lines.append(f"**Isteresi:** ±{args.isteresi}°C  ")
    if args.modo_controllo in ("on_off", "on_off_isteresi"):
        lines.append(f"**Min cycle:** {args.min_cycle_sec}s\n")
    if args.modo_controllo in ("P", "PI", "PID"):
        lines.append(
            f"**PID riscaldatore:** kp={args.kp_risc}  ki={args.ki_risc}  kd={args.kd_risc}\n"
        )
        lines.append(
            f"**PID refrigeratore:** kp={args.kp_frigo}  ki={args.ki_frigo}  kd={args.kd_frigo}\n"
        )
        lines.append(
            f"**PWM period:** {args.pwm_period_sec}s  "
            f"**Min cycle:** {args.min_cycle_sec}s\n"
        )
    lines.append(f"**Allarme timeout:** {args.allarme_timeout} min\n\n---\n")

    # Pianificazione step
    t = int(args.avvio_epoch)
    for step in steps:
        ore = durata_ore(step)
        fine_step = t + int(ore * 3600)
        fine_step_dt = datetime.datetime.fromtimestamp(fine_step)
        tipo = "↗ rampa" if "ramp_hours" in step else "→"
        nome = step.get("step", "")
        temp = step.get("temp", "")
        durata_gg = round(ore / 24, 1)
        lines.append(
            f"{tipo} **{nome}** — {temp}°C — {durata_gg} gg"
            f" → fine **{fine_step_dt.strftime('%d/%m/%Y')}**\n"
        )
        t = fine_step

    message = "".join(lines)
    notif_id = f"{args.fermentatore}_v6_avvio"

    service_data = json.dumps({
        "title": f"Fermentatore {args.fermentatore.upper()} V6 avviato: {nome_profilo}",
        "message": message,
        "notification_id": notif_id,
    }).encode("utf-8")

    req = urllib.request.Request(
        f"{ha_url}/api/services/persistent_notification/create",
        data=service_data,
        headers={
            "Authorization": f"Bearer {ha_token}",
            "Content-Type": "application/json",
        },
        method="POST",
    )
    try:
        with urllib.request.urlopen(req, timeout=10) as resp:
            resp.read()
        print("OK: notifica persistente creata in HA.")
    except Exception as exc:
        print(f"WARN: notifica HA non creata: {exc}", file=sys.stderr)


def azione_avvia(args, steps: list):
    ts_now = int(datetime.datetime.now().timestamp())

    stop_topic = topic_stop(args.fermentatore)
    stop_payload = {
        "schema_version": "1.0",
        "request_id": f"{args.fermentatore}-stop-{ts_now % 100000:05d}",
        "ts": ts_now,
        "reason": "ha_start_sequence",
    }
    ok_stop, _ = pubblica(stop_topic, stop_payload, qos=MQTT_QOS, retain=False)
    if not ok_stop:
        print("WARN: stop preliminare non riuscito, continuo con control_set/profile_set.")

    time.sleep(0.25)

    ctrl_topic = topic_control_set(args.fermentatore)
    ctrl_payload = build_control_set_payload(args)
    ok_ctrl, ctrl_bytes = pubblica(ctrl_topic, ctrl_payload, qos=MQTT_QOS, retain=False)
    if not ok_ctrl:
        sys.exit(1)

    time.sleep(0.25)

    prof_topic = topic_profilo(args.fermentatore)
    prof_payload = build_profile_payload(args, steps)
    ok, nbytes = pubblica(prof_topic, prof_payload, qos=MQTT_QOS, retain=False)
    if not ok:
        sys.exit(1)
    print(
        f"OK: sequenza V6 inviata su {args.fermentatore}: "
        f"control_set ({ctrl_bytes} bytes, mode={normalize_control_mode(args.modo_controllo)}) -> "
        f"profile_set ({nbytes} bytes, profilo='{args.profilo}', avvio_epoch={args.avvio_epoch})"
    )
    crea_notifica_avvio(args, steps)


def parse_args():
    parser = argparse.ArgumentParser(
        description=(
            "Invia profilo fermentazione al firmware V6 (multi-mode controller) "
            "via MQTT/HA REST."
        )
    )
    parser.add_argument("--profilo", required=True,
                        help="Nome del profilo (senza .json)")
    parser.add_argument("--avvio_epoch", required=True, type=int,
                        help="Timestamp Unix di inizio fermentazione")
    parser.add_argument("--fermentatore", choices=("f1", "f2"), default="f1",
                        help="ID fermentatore: f1 o f2 (default: f1)")

    # Modo controllo
    parser.add_argument(
        "--modo_controllo",
        choices=CONTROL_MODES,
        default="on_off",
        help="Algoritmo di controllo (default: on_off)",
    )

    # Parametri ON/OFF con isteresi
    parser.add_argument("--isteresi", type=float, default=0.3,
                        help="Semi-banda isteresi in °C per on_off_isteresi (default: 0.3)")
    parser.add_argument("--min_cycle_sec", type=int, default=5,
                        help="Tempo minimo ON/OFF in secondi (default: 5)")

    # Parametri PID riscaldatore
    parser.add_argument("--kp_risc", type=float, default=5.0,
                        help="kp PID riscaldatore (default: 5.0)")
    parser.add_argument("--ki_risc", type=float, default=0.005,
                        help="ki PID riscaldatore (default: 0.005)")
    parser.add_argument("--kd_risc", type=float, default=0.0,
                        help="kd PID riscaldatore (default: 0.0)")

    # Parametri PID refrigeratore
    parser.add_argument("--kp_frigo", type=float, default=1.0,
                        help="kp PID refrigeratore (default: 1.0)")
    parser.add_argument("--ki_frigo", type=float, default=0.001,
                        help="ki PID refrigeratore (default: 0.001)")
    parser.add_argument("--kd_frigo", type=float, default=0.0,
                        help="kd PID refrigeratore (default: 0.0)")

    # Parametri comuni P/PI/PID
    parser.add_argument("--pwm_period_sec", type=int, default=60,
                        help="Periodo PWM in secondi per P/PI/PID (default: 60)")

    # Allarme
    parser.add_argument("--allarme_timeout", type=int, default=30,
                        help="Timeout allarme sensore in minuti (default: 30)")

    return parser.parse_args()


def main():
    args = parse_args()
    steps = carica_e_converti(args.profilo)
    azione_avvia(args, steps)


if __name__ == "__main__":
    main()
