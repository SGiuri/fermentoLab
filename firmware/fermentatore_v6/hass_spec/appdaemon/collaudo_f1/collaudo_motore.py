"""Motore del collaudo di F1 (Fermentatore V6): test reali sul fermentatore, senza birra.

Il motore non dipende da AppDaemon. L'adattatore (collaudo_app.py) gli passa un contesto con:
    ora() -> float                      epoch in secondi
    stato(entity_id) -> str | None      stato attuale di un'entità HA
    storia(entity_id, t0, t1)           [(epoch, stato)] con lo stato a t0 come primo elemento
    mqtt(topic, payload)                pubblica un comando (qos 1, senza retain)
    servizio(dominio, servizio, **dati) chiama un servizio HA
    entita(entity_id, stato, attributi) crea o aggiorna un'entità HA
    log(testo)
Il motore riceve tick periodici (30 s) ed eventi: ack del firmware, comandi dalla dashboard,
chiamate ai servizi notify (per verificare che le email di allarme partano davvero).

Ogni test è una macchina a stati. Fase, istante di inizio fase e dati del test sono salvati su
file a ogni passo: un riavvio di Home Assistant o di AppDaemon riprende dalla fase in corso.
"""
from __future__ import annotations

import bisect
import json
import os
import traceback

VERSIONE = "1.2.0"
CMD = "fermentazione/v6/f1/ha/cmd/"

E = {
    "temp": "sensor.f1_v6_temperatura",
    "sp": "sensor.f1_v6_setpoint",
    "fsm": "sensor.f1_v6_fsm",
    "profilo": "sensor.f1_v6_profilo",
    "step": "sensor.f1_v6_step_index",
    "ore": "sensor.f1_v6_ore_totali",
    "modo": "sensor.f1_v6_modo_controllo_attivo",
    "isteresi": "sensor.f1_v6_isteresi_attiva",
    "online": "binary_sensor.f1_v6_online",
    "dati": "binary_sensor.f1_v6_dati_in_arrivo",
    "caldo": "binary_sensor.f1_v6_riscaldatore",
    "freddo": "binary_sensor.f1_v6_refrigeratore",
    "controllo": "binary_sensor.f1_v6_controllo_attivo",
    "guasto": "binary_sensor.f1_v6_sensore_guasto",
    "allarme_t": "binary_sensor.f1_v6_allarme_temperatura",
    "reset": "sensor.f1_v6_causa_ultimo_reset",
    "uptime": "sensor.f1_v6_uptime",
    "firmware": "sensor.f1_v6_firmware",
    "modalita": "input_select.f1_v6_modalita",
    "soglia": "input_number.f1_v6_soglia_scostamento",
    "minuti_soglia": "input_number.f1_v6_minuti_scostamento",
    "parcheggio": "input_number.f1_collaudo_temp_parcheggio",
    "errore": "sensor.f1_v6_errore",
    "p": "sensor.f1_v6_termine_p",
    "i": "sensor.f1_v6_termine_i",
    "d": "sensor.f1_v6_termine_d",
    "uscita": "sensor.f1_v6_uscita_controllore",
    "duty_freddo": "sensor.f1_v6_duty_refrigeratore",
    "duty_caldo": "sensor.f1_v6_duty_riscaldatore",
    "kp_freddo": "sensor.f1_v6_kp_freddo_attivo",
    "ki_freddo": "sensor.f1_v6_ki_freddo_attivo",
    "kd_freddo": "sensor.f1_v6_kd_freddo_attivo",
    "kp_caldo": "sensor.f1_v6_kp_caldo_attivo",
    "ki_caldo": "sensor.f1_v6_ki_caldo_attivo",
    "kd_caldo": "sensor.f1_v6_kd_caldo_attivo",
}
ZONA_MORTA_PCT = 2.5       # firmware: |uscita| sotto questa soglia = relè spenti
USCITA_MINIMA_PCT = 5.0    # firmware: duty minimo quando l'uscita supera la zona morta

MODO_PRODUZIONE = "on_off_hyst_sym"
PARAMETRI_PRODUZIONE = {"kp_heat": 25.0, "ki_heat": 0.03, "kd_heat": 0.0,
                        "kp_cool": 25.0, "ki_cool": 0.03, "kd_cool": 0.0, "isteresi": 0.5}
NON_VALIDI = (None, "", "unknown", "unavailable", "none", "None")
TOLLERANZA_ARRIVO = 0.3      # °C: temperatura "raggiunta"
LIMITE_BASSO_COLLAUDO = 0.5  # °C: sotto, il collaudo si ferma (rischio ghiaccio)
LIMITE_ALTO_COLLAUDO = 26.0  # °C: sopra, il collaudo si ferma


# ============================================================================ utilità
def versione(v):
    try:
        return tuple(int(x) for x in str(v).split("."))
    except ValueError:
        return (0,)


def numero(v):
    if v in NON_VALIDI:
        return None
    try:
        return float(v)
    except (TypeError, ValueError):
        return None


def booleano(v):
    if v == "on":
        return True
    if v == "off":
        return False
    return None


def ore_min(secondi):
    if secondi is None:
        return "—"
    if secondi < 120:
        return f"{int(round(secondi))} s"
    m = int(round(secondi / 60))
    return f"{m // 60} h {m % 60:02d} min" if m >= 60 else f"{m} min"


def fmt(v, dec=1, unita=""):
    if v is None:
        return "—"
    return f"{v:.{dec}f}".replace(".", ",") + (f" {unita}" if unita else "")


def serie(punti, t0, t1, conv):
    """[(t, stato)] ordinati -> [(t, valore)] da t0 a t1, con il valore a t0 come primo punto."""
    prima = None
    out = []
    for t, st in punti:
        v = conv(st)
        if t <= t0:
            prima = v
            continue
        if t > t1:
            break
        out.append((t, v))
    return [(t0, prima)] + out


def valore_a(s, tempi, t):
    i = bisect.bisect_right(tempi, t) - 1
    return s[i][1] if i >= 0 else None


def conta_riavvii(UP):
    """Riavvii dell'ESP32 in una serie di uptime: ogni volta che l'uptime torna indietro."""
    n, prec = 0, None
    for _, v in UP:
        if v is None:
            continue
        if prec is not None and v < prec:
            n += 1
        prec = v
    return n


def calcola_metriche(T, SP, FR, CA, DATI, GU, t0, t1):
    """Metriche su [t0, t1] da serie già convertite (vedi serie())."""
    serie_tutte = [T, SP, FR, CA, DATI, GU]
    tempi = [[p[0] for p in s] for s in serie_tutte]
    punti = sorted({t for s in serie_tutte for t, _ in s if t0 <= t <= t1} | {t0, t1})
    somma_err = tempo_err = err_max = 0.0
    sopra = sotto = None
    t_min = t_max = None
    fr_on = ca_on = entrambi = senza_dati = guasto = 0.0
    for a, b in zip(punti, punti[1:]):
        dt = b - a
        tv, sv, fv, cv, dv, gv = (valore_a(s, tt, a) for s, tt in zip(serie_tutte, tempi))
        if tv is None or dv is False:
            senza_dati += dt
        if tv is not None:
            t_min = tv if t_min is None else min(t_min, tv)
            t_max = tv if t_max is None else max(t_max, tv)
            if sv is not None:
                e = tv - sv
                somma_err += abs(e) * dt
                tempo_err += dt
                err_max = max(err_max, abs(e))
                sopra = e if sopra is None else max(sopra, e)
                sotto = -e if sotto is None else max(sotto, -e)
        if fv:
            fr_on += dt
        if cv:
            ca_on += dt
        if fv and cv:
            entrambi += dt
        if gv:
            guasto += dt

    def avvii(s):
        n, prec = 0, s[0][1]
        for t, v in s[1:]:
            if v is True and prec is not True and t0 < t <= t1:
                n += 1
            prec = v
        return n

    durata = max(t1 - t0, 1.0)
    ore = durata / 3600
    return {
        "durata_h": round(ore, 2),
        "t_min": None if t_min is None else round(t_min, 2),
        "t_max": None if t_max is None else round(t_max, 2),
        "err_max": round(err_max, 2),
        "err_medio": round(somma_err / tempo_err, 3) if tempo_err > 0 else None,
        "sopra_max": None if sopra is None else round(sopra, 2),
        "sotto_max": None if sotto is None else round(sotto, 2),
        "duty_freddo": round(100 * fr_on / durata, 1),
        "duty_caldo": round(100 * ca_on / durata, 1),
        "avvii_freddo": avvii(FR),
        "avvii_caldo": avvii(CA),
        "avvii_freddo_h": round(avvii(FR) / ore, 2),
        "avvii_caldo_h": round(avvii(CA) / ore, 2),
        "simultanei_s": round(entrambi),
        "senza_dati_min": round(senza_dati / 60, 1),
        "guasto_min": round(guasto / 60, 1),
    }


def controllo(voce, ok, valore, limite, critico=True):
    return {"voce": voce, "ok": bool(ok), "valore": valore, "limite": limite, "critico": critico}


def esito_da_controlli(controlli):
    if any(not c["ok"] and c["critico"] for c in controlli):
        return "fallito"
    if any(not c["ok"] for c in controlli):
        return "avviso"
    return "superato"


def controlli_comuni(met):
    """Controlli validi per ogni finestra di regolazione."""
    return [
        controllo("Riavvii spontanei dell'ESP32", met.get("riavvii", 0) == 0, str(met.get("riavvii", 0)), "0"),
        controllo("Sensore non utilizzabile", met["guasto_min"] == 0, fmt(met["guasto_min"], 1, "min"), "0 min"),
        controllo("Caldo e freddo accesi insieme", met["simultanei_s"] == 0, f"{met['simultanei_s']} s", "0 s"),
        controllo("Tempo senza dati in HA", met["senza_dati_min"] <= 5, fmt(met["senza_dati_min"], 1, "min"),
                  "≤ 5 min", critico=False),
    ]


# ============================================================================ test
class Test:
    id = ""
    nome = ""
    gruppo = "A"          # A: firmware attuale, automatico · B: firmware 6.4.0 · C: manuale
    fw_min = "6.3.0"
    stima_h = 1.0
    durata = ""
    presenza = "nessuna"
    essenziale = True
    descrizione = ""
    criteri: list = []

    def avvia(self, m):
        raise NotImplementedError

    def tick(self, m):
        pass

    def su_ack(self, m, ack):
        pass

    def descrivi(self, m):
        return m.s["fase"] or ""

    def info(self):
        return {"nome": self.nome, "gruppo": self.gruppo, "firmware": self.fw_min, "durata": self.durata,
                "stima_h": self.stima_h, "presenza": self.presenza, "descrizione": self.descrivi_test(),
                "criteri": self.criteri, "essenziale": self.essenziale}

    def descrivi_test(self):
        return self.descrizione


class TestTenuta(Test):
    """Porta F1 a una temperatura costante e la tiene per un certo tempo."""

    def __init__(self, id, nome, target, max_arrivo_h, tenuta_h, stima_h, descrizione,
                 duty_freddo_max=None, t_min_limite=None, essenziale=True):
        self.id, self.nome, self.target = id, nome, target
        self.max_arrivo_h, self.tenuta_h, self.stima_h = max_arrivo_h, tenuta_h, stima_h
        self.duty_freddo_max, self.t_min_limite = duty_freddo_max, t_min_limite
        self.essenziale = essenziale
        self.descrizione = descrizione
        self.durata = f"arrivo (max {max_arrivo_h:g} h) + {tenuta_h:g} h di tenuta, ~{stima_h:g} h"
        self.criteri = [
            f"Arriva a {fmt(target, 1)} °C (±{fmt(TOLLERANZA_ARRIVO, 1)}) entro {max_arrivo_h:g} h",
            "In tenuta: scostamento massimo ≤ 1,0 °C, errore medio ≤ 0,5 °C",
            "Sensore sempre utilizzabile, mai caldo e freddo insieme, ≤ 6 avvii dei relè all'ora",
        ]
        if duty_freddo_max:
            self.criteri.append(f"Pompa accesa ≤ {duty_freddo_max:g} % del tempo in tenuta (margine del freddo)")

    def avvia(self, m):
        m.d["T0"] = m.num("temp")
        m.d["rid"] = m.costante(self.target, f"Collaudo {self.nome}")
        m.vai("arrivo")

    def tick(self, m):
        f = m.s["fase"]
        if f == "arrivo":
            a = m.stato_ack(m.d["rid"], 120)
            if a not in ("ok", "attesa"):
                m.esito("fallito", note=f"F1 non ha accettato il profilo ({a})")
                return
            T = m.num("temp")
            if a == "ok" and T is not None and abs(T - self.target) <= TOLLERANZA_ARRIVO:
                m.d["t_arrivo"], m.d["T_arrivo"] = m.ora(), T
                m.vai("tenuta")
            elif m.da() > self.max_arrivo_h * 3600:
                self.valuta(m, arrivato=False)
        elif f == "tenuta" and m.da() >= self.tenuta_h * 3600:
            self.valuta(m, arrivato=True)

    def valuta(self, m, arrivato):
        t_inizio = m.s["test_t"]
        T0 = m.d.get("T0")
        misure = {"T iniziale": fmt(T0, 2, "°C")}
        if not arrivato:
            T = m.num("temp")
            ore = (m.ora() - t_inizio) / 3600
            vel = (T - T0) / ore if (T is not None and T0 is not None and ore > 0) else None
            misure.update({"T dopo il tempo massimo": fmt(T, 2, "°C"), "Velocità media": fmt(vel, 2, "°C/h")})
            c = [controllo(f"Arriva a {fmt(self.target, 1)} °C", False, f"{fmt(T, 2)} °C dopo {self.max_arrivo_h:g} h",
                           f"≤ {self.max_arrivo_h:g} h")]
            m.esito("fallito", c, misure, note="Temperatura non raggiunta nel tempo massimo")
            return
        dt_arrivo = m.d["t_arrivo"] - t_inizio
        vel = None
        if T0 is not None and abs(m.d["T_arrivo"] - T0) >= 0.5 and dt_arrivo > 300:
            vel = (m.d["T_arrivo"] - T0) / (dt_arrivo / 3600)
        met = m.metriche(m.d["t_arrivo"], m.ora())
        riscaldamento = T0 is not None and self.target > T0
        avvii_h = met["avvii_caldo_h"] if riscaldamento else met["avvii_freddo_h"]
        c = [
            controllo(f"Arriva a {fmt(self.target, 1)} °C", True, ore_min(dt_arrivo), f"≤ {self.max_arrivo_h:g} h"),
            controllo("Scostamento massimo in tenuta", met["err_max"] <= 1.0, fmt(met["err_max"], 2, "°C"), "≤ 1,0 °C"),
            controllo("Errore medio in tenuta", (met["err_medio"] or 0) <= 0.5, fmt(met["err_medio"], 2, "°C"),
                      "≤ 0,5 °C", critico=False),
            controllo("Avvii del relè all'ora", avvii_h <= 6, fmt(avvii_h, 1), "≤ 6", critico=False),
        ] + controlli_comuni(met)
        if self.duty_freddo_max is not None:
            c.append(controllo("Pompa accesa in tenuta", met["duty_freddo"] <= self.duty_freddo_max,
                               fmt(met["duty_freddo"], 0, "%"), f"≤ {self.duty_freddo_max:g} %", critico=False))
        if self.t_min_limite is not None:
            c.append(controllo("Temperatura minima", (met["t_min"] or 99) >= self.t_min_limite,
                               fmt(met["t_min"], 2, "°C"), f"≥ {fmt(self.t_min_limite, 1)} °C", critico=False))
        misure.update({
            ("Velocità di riscaldamento" if riscaldamento else "Velocità di raffreddamento"):
                fmt(abs(vel), 2, "°C/h") if vel is not None else "già in temperatura",
            "Tempo di arrivo": ore_min(dt_arrivo),
            "T minima / massima in tenuta": f"{fmt(met['t_min'], 2)} / {fmt(met['t_max'], 2)} °C",
            "Pompa accesa": fmt(met["duty_freddo"], 0, "%"),
            "Riscaldatore acceso": fmt(met["duty_caldo"], 0, "%"),
            "Avvii pompa / riscaldatore": f"{met['avvii_freddo']} / {met['avvii_caldo']}",
        })
        m.esito(esito_da_controlli(c), c, misure)

    def descrivi(self, m):
        T = m.num("temp")
        if m.s["fase"] == "arrivo":
            return (f"Verso {fmt(self.target, 1)} °C: ora {fmt(T, 2)} °C, da {ore_min(m.da())} "
                    f"(massimo {self.max_arrivo_h:g} h)")
        if m.s["fase"] == "tenuta":
            return f"Tenuta a {fmt(self.target, 1)} °C: {ore_min(m.da())} di {self.tenuta_h:g} h, ora {fmt(T, 2)} °C"
        return m.s["fase"]


class TestComandi(Test):
    id = "comandi"
    nome = "Protezione dei comandi"
    stima_h = 0.2
    durata = "circa 10 minuti"
    descrizione = ("Invia a F1 comandi sbagliati o pericolosi e verifica che vengano rifiutati senza cambiare "
                   "nulla: setpoint a 50 °C, isteresi e guadagni non validi, uno STOP vecchio di un'ora "
                   "(come un messaggio rimasto nel broker), un comando ripetuto due volte, un JSON corrotto.")
    criteri = ["Ogni comando non valido riceve un errore con il motivo atteso",
               "Lo stop vecchio viene rifiutato: F1 resta in marcia",
               "Il comando duplicato viene ignorato",
               "Alla fine profilo, setpoint, modo e isteresi sono invariati"]

    def prove(self, m):
        base = dict(PARAMETRI_PRODUZIONE)
        modo = m.v("modo") if m.v("modo") not in NON_VALIDI else MODO_PRODUZIONE
        return [
            ("Setpoint a 50 °C", "profile_set",
             {"profilo": "Collaudo invalido", "avvio_epoch": 0,
              "steps": [{"step": "X", "temp": 50, "duration_hours": 1}]}, None),
            ("Isteresi 5 °C", "control_set",
             {"control_mode": modo, "params": dict(base, isteresi=5.0)}, "invalid_isteresi"),
            ("Guadagno negativo", "control_set",
             {"control_mode": modo, "params": dict(base, kp_heat=-1.0)}, "invalid_pid_params"),
            ("STOP vecchio di un'ora", "stop", {"reason": "collaudo_comando_vecchio"}, "stale_command"),
            ("JSON corrotto", "control_set", None, "invalid_json"),
        ]

    def avvia(self, m):
        m.d.update(i=0, risultati=[], prima=m.istantanea())
        m.vai("prova")

    def tick(self, m):
        f = m.s["fase"]
        if f == "prova":
            prove = self.prove(m)
            if m.d["i"] >= len(prove):
                m.vai("duplicato")
                return
            nome, cmd, corpo, atteso = prove[m.d["i"]]
            if "rid" not in m.d:
                if corpo is None:
                    m.d["t_invio"] = m.ora()
                    m.mqtt_grezzo(cmd, "{non è json")
                    m.d["rid"] = "*json*"
                else:
                    ts = m.ora() - 3600 if cmd == "stop" else None
                    m.d["rid"] = m.invia(cmd, corpo, ts=ts)
                return
            if m.d["rid"] == "*json*":
                ack = m.ack_per_motivo("invalid_json", m.d["t_invio"])
            else:
                ack = m.ultimo_ack(m.d["rid"])
            if ack is None and m.da() < 60:
                return
            ok = ack is not None and ack.get("status") == "error" and (atteso is None or ack.get("reason_code") == atteso)
            m.d["risultati"].append(controllo(
                nome, ok, "nessuna risposta" if ack is None else f"{ack.get('status')} ({ack.get('reason_code')})",
                f"error ({atteso or 'qualsiasi motivo'})"))
            m.d["i"] += 1
            m.d.pop("rid", None)
            m.vai("prova")
        elif f == "duplicato":
            if "rid" not in m.d:
                m.d["corpo"] = {"schema_version": "1.0", "request_id": f"f1-coll-dup-{int(m.ora())}",
                                "ts": int(m.ora()), "control_mode": m.v("modo") or MODO_PRODUZIONE,
                                "params": dict(PARAMETRI_PRODUZIONE)}
                m.d["rid"] = m.d["corpo"]["request_id"]
                m.s["ack"][m.d["rid"]] = []
                m.mqtt_grezzo("control_set", json.dumps(m.d["corpo"]))
                return
            acks = m.acks(m.d["rid"])
            if len(acks) == 1 and not m.d.get("reinviato"):
                m.mqtt_grezzo("control_set", json.dumps(m.d["corpo"]))
                m.d["reinviato"] = True
                return
            if len(acks) >= 2 or m.da() > 60:
                sec = acks[1] if len(acks) >= 2 else None
                m.d["risultati"].append(controllo(
                    "Comando ripetuto", sec is not None and sec.get("reason_code") == "duplicate_ignored",
                    "nessuna risposta" if sec is None else f"{sec.get('status')} ({sec.get('reason_code')})",
                    "ok (duplicate_ignored)"))
                m.vai("verifica")
        elif f == "verifica" and m.da() >= 20:
            prima, ora = m.d["prima"], m.istantanea()
            c = list(m.d["risultati"])
            for chiave, voce in (("fsm", "Stato"), ("profilo", "Profilo"), ("modo", "Modo di controllo"),
                                 ("isteresi", "Isteresi")):
                c.append(controllo(f"{voce} invariato", prima[chiave] == ora[chiave], str(ora[chiave]), str(prima[chiave])))
            dsp = abs((ora["sp"] or 0) - (prima["sp"] or 0))
            c.append(controllo("Setpoint invariato", dsp <= 0.05, fmt(ora["sp"], 2, "°C"), fmt(prima["sp"], 2, "°C")))
            if prima["fsm"] in ("RUNNING", "HOLDING") and ora["fsm"] not in ("RUNNING", "HOLDING"):
                m.invia("start")  # lo stop vecchio è stato eseguito: rimette in marcia
            m.esito(esito_da_controlli(c), c)

    def descrivi(self, m):
        if m.s["fase"] == "prova":
            prove = self.prove(m)
            i = m.d.get("i", 0)
            return f"Comando {i + 1} di {len(prove) + 1}: {prove[i][0] if i < len(prove) else ''}"
        return {"duplicato": "Comando ripetuto due volte", "verifica": "Verifica dello stato finale"}.get(m.s["fase"], "")


class TestRiavvio(Test):
    id = "riavvio"
    nome = "Riavvio durante la regolazione"
    stima_h = 0.2
    durata = "circa 5 minuti"
    descrizione = ("Riavvia l'ESP32 con il comando reboot mentre regola e verifica che riparta da solo con lo "
                   "stesso profilo, step, setpoint, modo e isteresi, senza falsi allarmi.")
    criteri = ["Torna online entro 3 minuti con causa del reset 'software'",
               "Stesso profilo, step, setpoint (±0,05 °C), modo e isteresi; regolazione di nuovo attiva",
               "Il tempo del profilo non torna indietro", "Nessuna email di allarme"]

    def avvia(self, m):
        if m.v("fsm") not in ("RUNNING", "HOLDING"):
            T = m.num("temp") or 12.0
            m.d["rid_profilo"] = m.costante(round(T, 1), "Collaudo riavvio")
            m.vai("marcia")
        else:
            m.vai("prima")

    def tick(self, m):
        f = m.s["fase"]
        if f == "marcia":
            if m.v("fsm") in ("RUNNING", "HOLDING") and m.da() > 30:
                m.vai("prima")
            elif m.da() > 180:
                m.esito("fallito", note="F1 non è partito con il profilo di prova")
        elif f == "prima":
            m.d["prima"] = m.istantanea()
            m.d["uptime_prima"] = m.num("uptime")
            m.d["rid"] = m.invia("reboot")
            m.d["t_reboot"] = m.ora()
            m.vai("attesa")
        elif f == "attesa":
            up = m.num("uptime")
            if not m.on("online"):
                m.d["visto_offline"] = True
            if up is not None and m.d["uptime_prima"] is not None and up < m.d["uptime_prima"] and m.on("online"):
                m.d["t_ritorno"] = m.ora()
                m.vai("assestamento")
            elif m.da() > 300:
                m.esito("fallito", [controllo("Torna online", False, "non tornato in 5 min", "≤ 3 min")])
        elif f == "assestamento" and m.da() >= 120:
            prima, ora = m.d["prima"], m.istantanea()
            durata = m.d["t_ritorno"] - m.d["t_reboot"]
            c = [controllo("Torna online", durata <= 180, ore_min(durata), "≤ 3 min"),
                 controllo("Causa del reset", ora["reset"] == "software", str(ora["reset"]), "software")]
            for chiave, voce in (("fsm", "Stato"), ("profilo", "Profilo"), ("step", "Step"),
                                 ("modo", "Modo di controllo"), ("isteresi", "Isteresi")):
                c.append(controllo(f"{voce} dopo il riavvio", prima[chiave] == ora[chiave], str(ora[chiave]),
                                   str(prima[chiave])))
            dsp = abs((ora["sp"] or 0) - (prima["sp"] or 0))
            c.append(controllo("Setpoint dopo il riavvio", dsp <= 0.05, fmt(ora["sp"], 2, "°C"), fmt(prima["sp"], 2, "°C")))
            c.append(controllo("Regolazione di nuovo attiva", m.on("controllo"), m.v("controllo"), "on"))
            c.append(controllo("Il tempo del profilo non torna indietro",
                               (ora["ore"] or 0) >= (prima["ore"] or 0) - 0.01,
                               fmt(ora["ore"], 2, "h"), f"≥ {fmt(prima['ore'], 2, 'h')}"))
            email = m.email_dopo(m.s["test_t"], ("offline", "anomalo", "sensore", "nessun dato"))
            c.append(controllo("Nessuna email di allarme", not email, ", ".join(email) or "nessuna", "nessuna"))
            m.esito(esito_da_controlli(c), c, {"Tempo di ritorno": ore_min(durata)})

    def descrivi(self, m):
        return {"marcia": "Avvio di un profilo per il test", "attesa": "Riavvio in corso, attesa del ritorno",
                "assestamento": "Verifica dopo il riavvio (2 minuti)"}.get(m.s["fase"], m.s["fase"])


class TestRampa(Test):
    id = "rampa"
    nome = "Rampa di discesa 17 → 10 °C"
    stima_h = 8.0
    durata = "6 h di rampa + 1,5 h, più l'eventuale arrivo a 17 °C"
    descrizione = ("Profilo con rampa di 6 ore da 17 a 10 °C (1,2 °C/h), come il passaggio dalla sosta del "
                   "diacetile alla maturazione. Se F1 non è a 17 °C ci arriva prima.")
    criteri = ["Il setpoint a metà rampa è 13,5 °C (±0,3)",
               "Durante la rampa la temperatura segue il setpoint entro 1,0 °C (dopo i primi 15 minuti)",
               "1,5 h dopo la fine della rampa la temperatura è a 10 °C (±0,5)"]
    PARTENZA, ARRIVO, ORE = 17.0, 10.0, 6.0

    def avvia(self, m):
        T = m.num("temp")
        if T is None or abs(T - self.PARTENZA) > 0.5:
            m.d["rid"] = m.costante(self.PARTENZA, "Collaudo verso 17 C")
            m.vai("preparazione")
        else:
            self.inizia(m)

    def inizia(self, m):
        m.d["rid"] = m.profilo("Collaudo rampa 17-10", [
            {"step": "Partenza", "temp": self.PARTENZA, "duration_hours": 0.02},
            {"step": "Rampa", "temp": self.ARRIVO, "ramp_hours": self.ORE},
            {"step": "Arrivo", "temp": self.ARRIVO, "duration_hours": 2160}])
        m.d["t_rampa"] = m.ora() + 0.02 * 3600
        m.vai("rampa")

    def tick(self, m):
        f = m.s["fase"]
        a = m.stato_ack(m.d.get("rid"), 120) if m.d.get("rid") else "ok"
        if a not in ("ok", "attesa"):
            m.esito("fallito", note=f"F1 non ha accettato il profilo ({a})")
            return
        if f == "preparazione":
            T = m.num("temp")
            if a == "ok" and T is not None and abs(T - self.PARTENZA) <= TOLLERANZA_ARRIVO:
                self.inizia(m)
            elif m.da() > 16 * 3600:
                m.esito("fallito", [controllo("Arriva a 17 °C prima della rampa", False, fmt(T, 2, "°C"), "≤ 16 h")])
        elif f == "rampa":
            meta = m.d["t_rampa"] + self.ORE * 3600 / 2
            if "sp_meta" not in m.d and m.ora() >= meta:
                m.d["sp_meta"] = m.num("sp")
            if m.ora() >= m.d["t_rampa"] + self.ORE * 3600:
                m.d["t_fine"] = m.ora()
                m.vai("arrivo")
        elif f == "arrivo" and m.da() >= 1.5 * 3600:
            met = m.metriche(m.d["t_rampa"] + 900, m.d["t_fine"])
            T = m.num("temp")
            atteso = (self.PARTENZA + self.ARRIVO) / 2
            spm = m.d.get("sp_meta")
            c = [controllo("Setpoint a metà rampa", spm is not None and abs(spm - atteso) <= 0.3,
                           fmt(spm, 2, "°C"), f"{fmt(atteso, 1)} ± 0,3 °C"),
                 controllo("Scostamento massimo durante la rampa", met["err_max"] <= 1.0, fmt(met["err_max"], 2, "°C"),
                           "≤ 1,0 °C"),
                 controllo("Errore medio durante la rampa", (met["err_medio"] or 0) <= 0.5,
                           fmt(met["err_medio"], 2, "°C"), "≤ 0,5 °C", critico=False),
                 controllo("Temperatura 1,5 h dopo la rampa", T is not None and abs(T - self.ARRIVO) <= 0.5,
                           fmt(T, 2, "°C"), "10,0 ± 0,5 °C")] + controlli_comuni(met)
            m.esito(esito_da_controlli(c), c, {
                "In ritardo sul setpoint (max)": fmt(met["sopra_max"], 2, "°C"),
                "Pompa accesa durante la rampa": fmt(met["duty_freddo"], 0, "%"),
                "Avvii pompa": str(met["avvii_freddo"])})

    def descrivi(self, m):
        T, sp = m.num("temp"), m.num("sp")
        if m.s["fase"] == "preparazione":
            return f"Arrivo a 17 °C prima della rampa: ora {fmt(T, 2)} °C, da {ore_min(m.da())} (max 16 h)"
        if m.s["fase"] == "rampa":
            return f"Rampa: setpoint {fmt(sp, 2)} °C, temperatura {fmt(T, 2)} °C, {ore_min(m.ora() - m.d['t_rampa'])} di 6 h"
        if m.s["fase"] == "arrivo":
            return f"Dopo la rampa: {fmt(T, 2)} °C, {ore_min(m.da())} di 1 h 30 min"
        return m.s["fase"]


class TestStressPompa(Test):
    id = "stress_pompa"
    nome = "Commutazioni della pompa"
    stima_h = 1.2
    durata = "circa 1 ora"
    descrizione = ("Ricrea il guasto storico con il firmware attuale: accende e spegne la pompa del glicole 25 volte "
                   "spostando il setpoint appena sotto e appena sopra la temperatura (rispettando i tempi minimi "
                   "dei relè) e conta i riavvii dell'ESP32 e le perdite del sensore dopo ogni commutazione.")
    criteri = ["Nessun riavvio dell'ESP32", "Sensore sempre utilizzabile", "Almeno 23 cicli completati"]
    CICLI = 25

    def avvia(self, m):
        m.d.update(cicli=0, mancati=0, t_inizio=m.ora())
        m.vai("accendi")

    def _setpoint(self, m, delta):
        T = m.num("temp")
        if T is None:
            return False
        m.d["rid"] = m.costante(round(T + delta, 2), "Collaudo stress pompa")
        return True

    def tick(self, m):
        f = m.s["fase"]
        pompa = m.v("freddo")
        if f == "accendi":
            if "rid" not in m.d:
                self._setpoint(m, -1.0)
            elif pompa == "on":
                m.d.pop("rid", None)
                m.vai("accesa")
            elif m.da() > 240:
                m.d["mancati"] += 1
                m.d.pop("rid", None)
                m.vai("accesa")
        elif f == "accesa" and m.da() >= 40:
            if self._setpoint(m, 0.3):
                m.vai("spegni")
        elif f == "spegni":
            if pompa == "off":
                m.d["cicli"] += 1
                m.vai("spenta")
            elif m.da() > 180:
                m.d["mancati"] += 1
                m.vai("spenta")
        elif f == "spenta" and m.da() >= 65:
            if m.d["cicli"] + m.d["mancati"] >= self.CICLI:
                self.valuta(m)
            else:
                m.d.pop("rid", None)
                m.vai("accendi")

    def valuta(self, m):
        met = m.metriche(m.d["t_inizio"], m.ora())
        riavvii = [r for r in m.s.get("riavvii", []) if r["t"] >= m.d["t_inizio"]]
        c = [controllo("Riavvii dell'ESP32", met["riavvii"] == 0, str(met["riavvii"]), "0"),
             controllo("Sensore non utilizzabile", met["guasto_min"] == 0, fmt(met["guasto_min"], 1, "min"), "0 min"),
             controllo("Cicli completati", m.d["cicli"] >= self.CICLI - 2, f"{m.d['cicli']} di {self.CICLI}",
                       f"≥ {self.CICLI - 2}", critico=False)]
        misure = {"Cicli della pompa": str(m.d["cicli"]), "Accensioni registrate in HA": str(met["avvii_freddo"]),
                  "Riavvii": self.descrivi_riavvii(riavvii), "Minuti senza dati": fmt(met["senza_dati_min"], 1)}
        if met["riavvii"]:
            misure["Riavvii per 100 commutazioni"] = fmt(100 * met["riavvii"] / max(2 * m.d["cicli"], 1), 1)
        m.esito(esito_da_controlli(c), c, misure)

    @staticmethod
    def descrivi_riavvii(riavvii):
        dettagli = []
        for r in riavvii:
            p = r.get("pompa")
            if p:
                dettagli.append(f"{fmt(r['t'] - p['t'], 1)} s dopo {p['rele']} {p['stato']} (causa {r['causa']})")
            else:
                dettagli.append(f"nessuna commutazione nei 2 minuti prima (causa {r['causa']})")
        return "; ".join(dettagli) or "nessuno"

    def descrivi(self, m):
        n = m.d.get("cicli", 0)
        visti = len([r for r in m.s.get("riavvii", []) if r["t"] >= m.d.get("t_inizio", 0)])
        return (f"Ciclo {n + 1} di {self.CICLI}: pompa {m.v('freddo')}, {fmt(m.num('temp'), 2)} °C, "
                f"riavvii finora {visti}")


def media_nel_tempo(punti, t0, t1):
    """Media pesata sul tempo di una serie numerica (vedi serie())."""
    somma = durata = 0.0
    for (ta, va), (tb, _) in zip(punti, punti[1:] + [(t1, None)]):
        a, b = max(ta, t0), min(tb, t1)
        if va is not None and b > a:
            somma += va * (b - a)
            durata += b - a
    return somma / durata if durata > 0 else None


def comando_con_ack(m, chiave, invia, timeout=60):
    """Invia una sola volta (request_id in m.d[chiave]) e restituisce lo stato dell'ack."""
    if chiave not in m.d:
        m.d[chiave] = invia()
        return "attesa"
    return m.stato_ack(m.d[chiave], timeout)


class TestModo(Test):
    """Cambia modo di controllo (stop → control_set → profilo), osserva, rimette la produzione e valuta."""

    def __init__(self, id, nome, modo, params, gradino, osservazione_h, descrizione, criteri):
        self.id, self.nome, self.modo, self.params = id, nome, modo, params
        self.gradino, self.osservazione_h = gradino, osservazione_h
        self.stima_h = osservazione_h + 0.2
        self.durata = f"circa {osservazione_h:g} h"
        self.descrizione, self.criteri = descrizione, criteri

    def avvia(self, m):
        T = m.num("temp")
        base = T if T is not None else (m.num("sp") or 10.0)
        m.d.update(T0=T, sp=round(base + self.gradino, 1), n=0, dev_p=[], dev_duty=[], i_max=0.0, d_min=0.0,
                   d_max=0.0, i_fine=None, modi=[])
        m.vai("stop")

    def tick(self, m):
        f = m.s["fase"]
        if f in ("stop", "rip_stop"):
            if m.v("fsm") not in ("RUNNING", "HOLDING") and f"rid_{f}" not in m.d:
                m.vai("ctrl" if f == "stop" else "rip_ctrl")
                return
            a = comando_con_ack(m, f"rid_{f}", lambda: m.invia("stop", {"reason": "collaudo_cambio_modo"}))
            if a != "attesa":
                m.vai("ctrl" if f == "stop" else "rip_ctrl")
        elif f in ("ctrl", "rip_ctrl"):
            if f == "ctrl":
                corpo = {"control_mode": self.modo, "params": dict(self.params)}
            else:
                corpo = {"control_mode": MODO_PRODUZIONE, "params": dict(PARAMETRI_PRODUZIONE)}
            a = comando_con_ack(m, f"rid_{f}", lambda: m.invia("control_set", corpo))
            if a == "attesa":
                return
            if a != "ok" and f == "ctrl":
                m.d["problema"] = f"F1 ha rifiutato il modo {self.modo} ({a})"
                m.vai("rip_ctrl")  # F1 è fermo: rimette subito la produzione e riparte
                return
            m.vai("profilo" if f == "ctrl" else "rip_profilo")
        elif f in ("profilo", "rip_profilo"):
            a = comando_con_ack(m, f"rid_{f}", lambda: m.costante(m.d["sp"], f"Collaudo {self.nome}"))
            if a == "attesa":
                return
            if f == "profilo":
                if a != "ok":
                    m.d["problema"] = f"profilo non accettato ({a})"
                    m.vai("rip_stop")
                    return
                m.d["t_oss"] = m.ora()
                m.vai("osservazione")
            else:
                self.valuta(m)
        elif f == "osservazione":
            self.campiona(m)
            if m.da() >= self.osservazione_h * 3600:
                m.d["t_fine"] = m.ora()
                m.vai("rip_stop")

    def campiona(self, m):
        """Coerenza dei termini pubblicati dal firmware (stesso messaggio esp/controllo)."""
        if m.v("modo") not in m.d["modi"]:
            m.d["modi"].append(m.v("modo"))
        e, p, i, d, u = (m.num(k) for k in ("errore", "p", "i", "d", "uscita"))
        if None in (e, p, i, d, u) or not m.on("controllo"):
            return
        m.d["n"] += 1
        lato = "caldo" if e >= 0 else "freddo"
        kp = m.num(f"kp_{lato}")
        if kp is not None and self.modo != "on_off":
            m.d["dev_p"].append(round(abs(p - kp * e), 2))
        m.d["i_max"] = max(m.d["i_max"], abs(i))
        m.d["d_min"], m.d["d_max"] = min(m.d["d_min"], d), max(m.d["d_max"], d)
        m.d["i_fine"] = i
        if self.modo != "on_off":
            atteso_f = 0.0 if (u >= 0 or abs(u) <= ZONA_MORTA_PCT) else max(abs(u), USCITA_MINIMA_PCT)
            atteso_c = 0.0 if (u <= 0 or abs(u) <= ZONA_MORTA_PCT) else max(abs(u), USCITA_MINIMA_PCT)
            df, dc = m.num("duty_freddo"), m.num("duty_caldo")
            if df is not None and dc is not None:
                m.d["dev_duty"].append(round(max(abs(df - min(atteso_f, 100)), abs(dc - min(atteso_c, 100))), 1))
        for k in ("dev_p", "dev_duty"):
            m.d[k] = m.d[k][-400:]

    @staticmethod
    def quota_fuori(valori, soglia):
        return sum(1 for v in valori if v > soglia) / len(valori) if valori else None

    def soglie_on_off(self, m, t0, t1):
        """Temperatura rispetto al setpoint quando la pompa si accende e si spegne (dallo storico)."""
        def st(k, conv):
            return serie(m.ctx.storia(E[k], t0, t1), t0, t1, conv)
        T, SP, FR = st("temp", numero), st("sp", numero), st("freddo", booleano)
        tT, tS = [p[0] for p in T], [p[0] for p in SP]
        on, off = [], []
        for (ta, va), (tb, vb) in zip(FR, FR[1:]):
            if va is None or vb is None or va == vb:
                continue
            # temperatura e relè arrivano nello stesso messaggio esp/state: si legge la T di quel messaggio
            Tv, Sv = valore_a(T, tT, tb + 1), valore_a(SP, tS, tb)
            if Tv is None or Sv is None:
                continue
            (on if vb else off).append(Tv - Sv)
        return on, off

    def valuta(self, m):
        t0 = m.d.get("t_oss")
        t1 = m.d.get("t_fine") or m.ora()
        if not t0:
            m.esito("fallito", note=m.d.get("problema") or "il modo non è stato attivato")
            return
        met = m.metriche(t0, t1)
        regime0 = max(t0, t1 - 3600)
        reg = m.metriche(regime0, t1)
        sp = m.d["sp"]
        c = [controllo(f"Modo attivo durante il test", m.d["modi"] and all(x == self.modo for x in m.d["modi"]),
                       ", ".join(str(x) for x in m.d["modi"]) or "—", self.modo)]
        misure = {"Setpoint": fmt(sp, 1, "°C"), "T iniziale": fmt(m.d.get("T0"), 2, "°C"),
                  "T min / max": f"{fmt(met['t_min'], 2)} / {fmt(met['t_max'], 2)} °C",
                  "Pompa accesa": fmt(met["duty_freddo"], 0, "%"), "Riscaldatore acceso": fmt(met["duty_caldo"], 0, "%"),
                  "Avvii pompa / riscaldatore all'ora": f"{fmt(met['avvii_freddo_h'], 1)} / {fmt(met['avvii_caldo_h'], 1)}"}
        if self.modo == "on_off":
            on, off = self.soglie_on_off(m, t0, t1)
            if len(on) >= 2 and len(off) >= 2:
                s_on, s_off = sum(on) / len(on), sum(off) / len(off)
                c.append(controllo("Isteresi effettiva (soglia di accensione − soglia di spegnimento)",
                                   s_on - s_off <= 0.25, fmt(s_on - s_off, 2, "°C"), "≤ 0,25 °C (con isteresi 0,5: ~0,5)"))
                c.append(controllo("La pompa si accende appena sopra il setpoint", 0 <= s_on <= 0.2,
                                   f"{fmt(s_on, 2)} °C sopra", "0 … +0,2 °C"))
                c.append(controllo("La pompa si spegne al setpoint", -0.2 <= s_off <= 0.05,
                                   f"{fmt(s_off, 2)} °C", "−0,2 … 0 °C"))
                misure["Accensioni / spegnimenti analizzati"] = f"{len(on)} / {len(off)}"
            else:
                c.append(controllo("Commutazioni sufficienti per misurare l'isteresi", False,
                                   f"{len(on)} accensioni, {len(off)} spegnimenti", "≥ 2 e ≥ 2"))
            c.append(controllo("Scostamento massimo", met["err_max"] <= 0.6, fmt(met["err_max"], 2, "°C"), "≤ 0,6 °C",
                               critico=False))
        else:
            q = self.quota_fuori(m.d["dev_p"], 0.3)
            c.append(controllo("P = Kp × errore (campioni coerenti)", q is not None and q <= 0.05,
                               f"{len(m.d['dev_p'])} campioni, {fmt(100 * q, 0) if q is not None else '—'} % fuori",
                               "≤ 5 % fuori di 0,3 punti"))
            qd = self.quota_fuori(m.d["dev_duty"], 1.0)
            c.append(controllo("Duty dei relè coerente con l'uscita", qd is not None and qd <= 0.05,
                               f"{fmt(100 * qd, 0) if qd is not None else '—'} % fuori", "≤ 5 % fuori di 1 punto"))
            if self.modo == "P":
                c.append(controllo("Termini I e D nulli", m.d["i_max"] < 0.01 and abs(m.d["d_min"]) < 0.01 and m.d["d_max"] < 0.01,
                                   f"I max {fmt(m.d['i_max'], 2)}, D {fmt(m.d['d_min'], 2)}…{fmt(m.d['d_max'], 2)}", "0"))
            else:
                c.append(controllo("Termine I entro ±50 % (anti-windup)", m.d["i_max"] <= 50.5, fmt(m.d["i_max"], 1, "%"), "≤ 50 %"))
            if self.modo == "PID":
                c.append(controllo("Termine D attivo", max(abs(m.d["d_min"]), m.d["d_max"]) >= 0.05,
                                   f"{fmt(m.d['d_min'], 2)} … {fmt(m.d['d_max'], 2)} %", "≠ 0"))
                c.append(controllo("Termine D senza picchi", max(abs(m.d["d_min"]), m.d["d_max"]) <= 30,
                                   f"{fmt(max(abs(m.d['d_min']), m.d['d_max']), 1)} %", "≤ 30 %", critico=False))
            elif self.modo == "PI":
                c.append(controllo("Termine D nullo", abs(m.d["d_min"]) < 0.01 and m.d["d_max"] < 0.01,
                                   f"{fmt(m.d['d_min'], 2)} … {fmt(m.d['d_max'], 2)}", "0"))
            # la pompa segue davvero il duty chiesto (finestra di 10 minuti)
            dty = media_nel_tempo(serie(m.ctx.storia(E["duty_freddo"], regime0, t1), regime0, t1, numero), regime0, t1)
            if dty is not None and dty >= 10:
                c.append(controllo("La pompa lavora quanto chiede il controllore (ultima ora)",
                                   abs(reg["duty_freddo"] - dty) <= 15, f"accesa {fmt(reg['duty_freddo'], 0)} %",
                                   f"{fmt(dty, 0)} % ± 15"))
            misure["Duty medio chiesto (ultima ora)"] = fmt(dty, 0, "%")
            T_fin = m.num("temp")
            e_reg = (reg["t_min"] + reg["t_max"]) / 2 - sp if reg["t_min"] is not None else None
            misure["Errore a regime (T − SP, ultima ora)"] = fmt(e_reg, 2, "°C")
            misure["Termine I a fine prova"] = fmt(m.d.get("i_fine"), 1, "%")
            if self.modo == "P":
                atteso = dty / (self.params["kp_cool"] or 1) if dty is not None else None
                misure["Errore a regime atteso dal duty (duty / Kp)"] = fmt(atteso, 2, "°C")
                c.append(controllo("Lavora in freddo: la temperatura scende verso il setpoint",
                                   T_fin is not None and m.d.get("T0") is not None and m.d["T0"] - T_fin >= 0.3 * abs(self.gradino),
                                   f"{fmt(m.d.get('T0'), 2)} → {fmt(T_fin, 2)} °C", f"≥ {fmt(0.3 * abs(self.gradino), 2)} °C"))
                if e_reg is not None and atteso is not None:
                    c.append(controllo("Errore a regime coerente con duty / Kp", abs(e_reg - atteso) <= 0.25,
                                       fmt(e_reg, 2, "°C"), f"{fmt(atteso, 2)} ± 0,25 °C", critico=False))
                c.append(controllo("Regime stabile (ultima ora)", reg["t_max"] - reg["t_min"] <= 0.4 if reg["t_min"] is not None else False,
                                   f"{fmt(reg['t_min'], 2)} … {fmt(reg['t_max'], 2)} °C", "escursione ≤ 0,4 °C", critico=False))
            else:
                arrivato = met["sotto_max"] is not None and met["t_min"] is not None and met["t_min"] <= sp + 0.3
                c.append(controllo("Raggiunge il setpoint (±0,3 °C)", arrivato, fmt(met["t_min"], 2, "°C"), f"≤ {fmt(sp + 0.3, 2)} °C"))
                c.append(controllo("Errore medio nell'ultima ora", (reg["err_medio"] or 9) <= 0.2, fmt(reg["err_medio"], 2, "°C"),
                                   "≤ 0,2 °C"))
                c.append(controllo("Regime stabile (ultima ora)", reg["t_max"] - reg["t_min"] <= 0.5 if reg["t_min"] is not None else False,
                                   f"{fmt(reg['t_min'], 2)} … {fmt(reg['t_max'], 2)} °C", "escursione ≤ 0,5 °C", critico=False))
                c.append(controllo("Avvii della pompa all'ora", met["avvii_freddo_h"] <= 7, fmt(met["avvii_freddo_h"], 1), "≤ 7",
                                   critico=False))
        c += controlli_comuni(met)
        m.esito(esito_da_controlli(c), c, misure, note=m.d.get("problema"))

    def descrivi(self, m):
        f = m.s["fase"]
        if f == "osservazione":
            return (f"{self.nome}: {ore_min(m.da())} di {self.osservazione_h:g} h, {fmt(m.num('temp'), 2)} °C "
                    f"(setpoint {fmt(m.d['sp'], 1)}), uscita {fmt(m.num('uscita'), 0)} %, "
                    f"P {fmt(m.num('p'), 1)} I {fmt(m.num('i'), 1)} D {fmt(m.num('d'), 2)}")
        return {"stop": "Cambio di modo: stop", "ctrl": f"Cambio di modo: {self.modo}", "profilo": "Cambio di modo: profilo",
                "rip_stop": "Ritorno alla produzione: stop", "rip_ctrl": "Ritorno alla produzione: on/off 0,5 °C",
                "rip_profilo": "Ritorno alla produzione: profilo"}.get(f, f)


DEFAULT = {"kp_heat": 25.0, "ki_heat": 0.03, "kd_heat": 0.0, "kp_cool": 25.0, "ki_cool": 0.03, "kd_cool": 0.0,
           "isteresi": 0.5}


class TestAllarmeFreddo(Test):
    id = "allarme_freddo"
    nome = "Allarme se il freddo si ferma"
    stima_h = 8.0
    durata = "4-16 h secondo la temperatura ambiente"
    descrizione = ("Con l'acqua fredda (2 °C dopo il cold crash, altrimenti fino a 10 °C) simula un guasto del "
                   "freddo: l'isteresi passa a 3 °C, quindi la pompa resta ferma finché la temperatura non sale di 3 °C. Verifica che scatti l'allarme 'temperatura fuori range' con "
                   "l'email, poi rimette l'isteresi a 0,5 °C e verifica il rientro. Misura anche quanto "
                   "velocemente si scalderebbe la birra con il freddo guasto.")
    criteri = ["Superata la soglia (1,5 °C) senza avvicinamento, l'allarme si attiva in HA",
               "L'email di allarme parte entro il tempo impostato (60 min) + 30 min",
               "Ripristinato il freddo, l'allarme rientra entro 6 h"]

    def avvia(self, m):
        T, sp = m.num("temp"), m.num("sp")
        if T is not None and sp is not None and sp <= 12 and abs(T - sp) <= 0.5:
            m.vai("stabile")
        else:
            # la prova serve con l'acqua sotto l'ambiente: si parte dalla temperatura attuale, al massimo 10 °C
            m.d["target"] = min(max(round(T if T is not None else 10.0), 2), 10)
            m.d["rid"] = m.costante(m.d["target"], f"Collaudo allarme {m.d['target']} C")
            m.vai("preparazione")

    def tick(self, m):
        f = m.s["fase"]
        T, sp = m.num("temp"), m.num("sp")
        if f == "preparazione":
            a = m.stato_ack(m.d["rid"], 120)
            if a not in ("ok", "attesa"):
                m.esito("fallito", note=f"F1 non ha accettato il profilo ({a})")
            elif a == "ok" and T is not None and abs(T - m.d["target"]) <= 0.5:
                m.vai("stabile")
            elif m.da() > 8 * 3600:
                m.esito("fallito", note=f"Non arriva a {m.d['target']} °C in 8 h")
        elif f == "stabile" and m.da() >= 1800:
            m.d["rid_guasto"] = m.controllo(isteresi=3.0)
            m.d["t_guasto"] = m.ora()
            m.d["T_guasto"] = T
            m.vai("deriva")
        elif f == "deriva":
            soglia = m.num("soglia") or 1.5
            if T is not None and sp is not None:
                m.d["T_max"] = max(m.d.get("T_max", T), T)
                if T - sp > soglia and "t_oltre" not in m.d:
                    m.d["t_oltre"], m.d["T_oltre"] = m.ora(), T
                if T - sp > 3.5:
                    self.ripristina(m, "la temperatura ha superato il setpoint di 3,5 °C")
                    return
            if m.on("allarme_t"):
                m.d["t_allarme"] = m.ora()
                m.vai("email")
            elif m.da() > 16 * 3600:
                self.ripristina(m, "allarme non scattato in 16 h")
        elif f == "email":
            em = m.email_dopo(m.d["t_guasto"], ("fuori range",))
            minuti = m.num("minuti_soglia") or 60
            if em:
                m.d["t_email"] = m.ora()
                self.ripristina(m, None)
            elif m.da() > (minuti + 45) * 60:
                self.ripristina(m, "email di allarme non partita")
        elif f == "rientro":
            if not m.on("allarme_t") and m.d.get("t_allarme"):
                m.d["t_rientro"] = m.ora()
                self.valuta(m)
            elif m.da() > 6 * 3600 or not m.d.get("t_allarme"):
                self.valuta(m)

    def ripristina(self, m, problema):
        m.d["problema"] = problema
        m.d["rid_ripristino"] = m.controllo(isteresi=0.5)
        m.d["t_ripristino"] = m.ora()
        m.vai("rientro")

    def valuta(self, m):
        minuti = m.num("minuti_soglia") or 60
        c = []
        if m.d.get("t_allarme"):
            ritardo = (m.d["t_email"] - m.d["t_allarme"]) if m.d.get("t_email") else None
            c.append(controllo("Allarme attivo in HA", True,
                               f"{ore_min(m.d['t_allarme'] - m.d['t_oltre'])} dopo la soglia" if m.d.get("t_oltre") else "sì",
                               "sì"))
            c.append(controllo("Email di allarme", ritardo is not None and ritardo <= (minuti + 30) * 60,
                               f"dopo {ore_min(ritardo)}" if ritardo is not None else "non partita",
                               f"entro {minuti:g} + 30 min"))
            c.append(controllo("Rientro dopo il ripristino del freddo", bool(m.d.get("t_rientro")),
                               ore_min(m.d["t_rientro"] - m.d["t_ripristino"]) if m.d.get("t_rientro") else "non rientrato",
                               "≤ 6 h"))
        else:
            c.append(controllo("Allarme scattato", False, "no", "sì"))
        deriva = None
        if m.d.get("t_oltre") and m.d.get("T_guasto") is not None:
            deriva = (m.d["T_oltre"] - m.d["T_guasto"]) / max((m.d["t_oltre"] - m.d["t_guasto"]) / 3600, 0.01)
        misure = {"Riscaldamento senza freddo": fmt(deriva, 2, "°C/h"),
                  "Temperatura massima": fmt(m.d.get("T_max"), 2, "°C"),
                  "Dal guasto all'allarme": ore_min(m.d["t_allarme"] - m.d["t_guasto"]) if m.d.get("t_allarme") else "—"}
        m.esito(esito_da_controlli(c), c, misure, note=m.d.get("problema"))

    def descrivi(self, m):
        T, sp = m.num("temp"), m.num("sp")
        return {
            "preparazione": f"Arrivo a {m.d.get('target')} °C: ora {fmt(T, 2)} °C, da {ore_min(m.da())} (max 8 h)",
            "stabile": f"Stabilizzazione a {fmt(sp, 1)} °C: {ore_min(m.da())} di 30 min",
            "deriva": f"Freddo sospeso: {fmt(T, 2)} °C (setpoint {fmt(sp, 1)}), in attesa dell'allarme da {ore_min(m.da())}",
            "email": f"Allarme attivo da {ore_min(m.da())}: attesa dell'email (parte dopo 60 min)",
            "rientro": f"Freddo ripristinato: {fmt(T, 2)} °C, attesa del rientro da {ore_min(m.da())}",
        }.get(m.s["fase"], m.s["fase"])


class TestCorrente(Test):
    id = "corrente"
    nome = "Mancanza di corrente"
    gruppo = "C"
    stima_h = 0.3
    durata = "il test attende fino a 72 h che tu tolga corrente"
    presenza = ("2 interventi di 1 minuto, a 5-10 minuti di distanza, quando vuoi entro 72 h: "
                "togli l'alimentazione all'ESP32, aspetta almeno 5 minuti, ridai alimentazione")
    descrizione = ("Verifica cosa succede con un blackout: HA deve avvisare che F1 è offline, e al ritorno della "
                   "corrente F1 deve ripartire da solo con lo stesso profilo e step, al setpoint giusto.")
    criteri = ["Email 'ESP32 offline' se il blackout dura più di 3 minuti",
               "Al ritorno: causa del reset 'poweron', stesso profilo e step, setpoint corretto",
               "Regolazione di nuovo attiva entro 3 minuti dal ritorno online"]

    def avvia(self, m):
        park = m.num("parcheggio") or 12.0
        sp = m.num("sp")
        if m.v("fsm") not in ("RUNNING", "HOLDING") or sp is None or abs(sp - park) > 0.05:
            m.d["rid"] = m.costante(park, "Collaudo attesa corrente")
        m.vai("attesa_spegnimento")
        m.ctx.servizio("persistent_notification", "create", notification_id="f1_collaudo_manuale",
                       title="F1 collaudo: tocca a te",
                       message=("Quando puoi (entro 72 ore) togli l'alimentazione all'ESP32 di F1, aspetta almeno "
                                "5 minuti e ridai alimentazione. Il resto lo verifica il collaudo."))

    def tick(self, m):
        f = m.s["fase"]
        if f == "attesa_spegnimento":
            if m.on("online"):
                if m.da() > 60:
                    m.d["prima"] = m.istantanea()
            elif m.d.get("prima"):
                m.d["t_off"] = m.ora()
                m.vai("spento")
            if m.da() > 72 * 3600:
                m.esito("interrotto", note="Nessun blackout nelle 72 ore: test non eseguito")
        elif f == "spento":
            up = m.num("uptime")
            if m.on("online") and up is not None and up < 600:
                m.d["t_on"], m.d["uptime_ritorno"] = m.ora(), up
                m.vai("ritorno")
            elif m.da() > 12 * 3600:
                m.esito("fallito", note="F1 non è tornato online entro 12 h dal blackout")
        elif f == "ritorno" and m.da() >= 180:
            prima, ora = m.d["prima"], m.istantanea()
            fuori = m.d["t_on"] - m.d["t_off"]
            c = [controllo("Causa del reset", ora["reset"] in ("poweron", "brownout"), str(ora["reset"]), "poweron")]
            for chiave, voce in (("fsm", "Stato"), ("profilo", "Profilo"), ("step", "Step"), ("modo", "Modo di controllo"),
                                 ("isteresi", "Isteresi")):
                c.append(controllo(f"{voce} dopo il blackout", prima[chiave] == ora[chiave], str(ora[chiave]), str(prima[chiave])))
            dsp = abs((ora["sp"] or 0) - (prima["sp"] or 0))
            c.append(controllo("Setpoint dopo il blackout", dsp <= 0.05, fmt(ora["sp"], 2, "°C"), fmt(prima["sp"], 2, "°C")))
            c.append(controllo("Regolazione di nuovo attiva", m.on("controllo"), m.v("controllo"), "on"))
            if fuori >= 5 * 60:
                em = m.email_dopo(m.d["t_off"], ("offline",))
                c.append(controllo("Email 'ESP32 offline'", bool(em), "inviata" if em else "non inviata", "inviata"))
            if ora["reset"] == "software":
                note = "Il reset risulta 'software': probabilmente non è stata tolta l'alimentazione"
            else:
                note = None
            m.ctx.servizio("persistent_notification", "dismiss", notification_id="f1_collaudo_manuale")
            m.esito(esito_da_controlli(c), c, {"Offline per": ore_min(fuori),
                                               "Avvio dopo il ritorno della corrente": f"{int(m.d['uptime_ritorno'])} s"}, note)

    def descrivi(self, m):
        return {"attesa_spegnimento": f"In attesa del blackout da {ore_min(m.da())} (fino a 72 h): togli corrente quando vuoi",
                "spento": f"F1 offline da {ore_min(m.da())}: ridai alimentazione dopo almeno 5 minuti",
                "ritorno": "F1 di nuovo online: verifica in corso (3 minuti)"}.get(m.s["fase"], m.s["fase"])


class TestFirmware64(Test):
    """Test che richiedono le funzioni di collaudo del firmware 6.4.0 (in preparazione)."""
    gruppo = "B"
    fw_min = "6.4.0"

    def __init__(self, id, nome, stima_h, durata, descrizione, criteri):
        self.id, self.nome, self.stima_h, self.durata = id, nome, stima_h, durata
        self.descrizione, self.criteri = descrizione, criteri

    def avvia(self, m):
        m.esito("non applicabile", note="Test disponibile con il firmware 6.4.0 e il motore di collaudo aggiornato")


class ControlloProduzione(Test):
    """Interno: porta F1 al controllo di produzione (on/off, isteresi 0,5 °C) e, a fine collaudo, al
    profilo di parcheggio."""

    def __init__(self, id, parcheggio):
        self.id, self.parcheggio = id, parcheggio
        self.nome = "Ripristino finale" if parcheggio else "Preparazione"

    def avvia(self, m):
        m.imposta_helper_produzione()
        if m.v("modo") == MODO_PRODUZIONE or m.v("fsm") not in ("RUNNING", "HOLDING"):
            m.d["riparti"] = False
            m.d["rid"] = m.controllo(isteresi=0.5)
            m.vai("controllo")
        else:
            m.d["riparti"] = True
            m.d["rid"] = m.invia("stop", {"reason": "collaudo_cambio_modo"})
            m.vai("stop")

    def tick(self, m):
        f = m.s["fase"]
        a = m.stato_ack(m.d.get("rid"), 60)
        if a == "attesa":
            return
        if f == "stop":
            m.d["rid"] = m.controllo(isteresi=0.5)
            m.vai("controllo")
        elif f == "controllo":
            if a != "ok":
                m.evento(f"Controllo di produzione non confermato da F1 ({a})")
                if not self.parcheggio:
                    m.fine_interno()
                    m._chiudi("interrotto", f"F1 non conferma il controllo di produzione ({a})")
                    return
            if m.d["riparti"]:
                m.d["rid"] = m.invia("start")
                m.vai("start")
            else:
                self.dopo(m)
        elif f == "start":
            self.dopo(m)
        elif f == "parcheggio":
            m.fine_interno()

    def dopo(self, m):
        if self.parcheggio:
            park = m.num("parcheggio") or 12.0
            m.d["rid"] = m.costante(park, f"Parcheggio {park:g} C")
            m.vai("parcheggio")
        else:
            m.fine_interno()

    def descrivi(self, m):
        return "Impostazione del controllo di produzione" if not self.parcheggio else "Ritorno al parcheggio"


CATALOGO = [
    TestComandi(),
    TestTenuta("lager", "Tenuta lager 10 °C", 10.0, 6, 4, 6.0,
               "Porta l'acqua a 10 °C e la tiene 4 ore con il controllo di produzione (on/off, isteresi 0,5 °C): "
               "precisione, commutazioni della pompa, continuità dei dati."),
    TestRiavvio(),
    TestStressPompa(),
    TestTenuta("diacetile", "Sosta del diacetile 17 °C", 17.0, 16, 3, 12.0,
               "Scalda fino a 17 °C con la resistenza (30-35 W) e tiene 3 ore. Misura la velocità di "
               "riscaldamento: serve per pianificare la sosta del diacetile.", essenziale=True),
    TestRampa(),
    TestModo("modo_on_off", "On/Off senza isteresi", "on_off", DEFAULT, 0.0, 2.5,
             "Tiene il setpoint con l'on/off puro (nessuna isteresi) e misura dallo storico a che temperatura la pompa "
             "si accende e si spegne: verifica che l'isteresi sia davvero zero (il parametro isteresi viene ignorato).",
             ["Isteresi effettiva ≤ 0,25 °C (con l'isteresi di produzione è ~0,5 °C)",
              "Pompa accesa appena sopra il setpoint, spenta al setpoint"]),
    TestModo("modo_p", "Proporzionale (P) in freddo", "P", DEFAULT, -1.5, 4.0,
             "Gradino di −1,5 °C in modo P con i parametri di default (Kp 25; Ki 0,03 deve essere ignorato). Verifica "
             "che il firmware calcoli P = Kp × errore, che I e D siano nulli, che la pompa lavori col duty chiesto e "
             "misura l'errore a regime tipico del P (circa duty / Kp).",
             ["P = Kp × errore", "I e D nulli", "Duty dei relè coerente con l'uscita", "La temperatura scende verso il setpoint"]),
    TestModo("modo_pi", "PI in freddo (parametri di default)", "PI", DEFAULT, -1.5, 5.0,
             "Gradino di −1,5 °C con il PI di default (Kp 25, Ki 0,03): deve arrivare al setpoint senza errore a regime, "
             "con l'integrale entro ±50 %.",
             ["Raggiunge il setpoint", "Errore medio ≤ 0,2 °C nell'ultima ora", "Coerenza di P, I e duty"]),
    TestModo("modo_pid", "PID in freddo", "PID", dict(DEFAULT, kd_heat=5000.0, kd_cool=5000.0), -1.5, 5.0,
             "Gradino di −1,5 °C con Kp 25 e Ki 0,03 di default e Kd 5000 (il Kd di default è 0, cioè il PID coinciderebbe "
             "col PI): verifica che il termine D sia attivo, senza picchi, e che la regolazione resti buona.",
             ["Raggiunge il setpoint", "Termine D attivo e senza picchi", "Errore medio ≤ 0,2 °C nell'ultima ora"]),
    TestTenuta("cold_crash", "Cold crash 2 °C", 2.0, 20, 4, 16.0,
               "Porta l'acqua a 2 °C e la tiene 4 ore: capacità massima del freddo, margine della pompa, "
               "nessun rischio di ghiaccio.", duty_freddo_max=85, t_min_limite=1.0),
    TestAllarmeFreddo(),
    TestFirmware64("salute_sensore", "Salute del sensore", 0.1, "tutto il collaudo",
                   "Conta per tutto il collaudo le letture non valide del DS18B20 (scollegato, reset per mancanza di "
                   "alimentazione, CRC, fuori scala) e quelle vicine a una commutazione dei relè.",
                   ["Nessuna lettura persa vicino alle commutazioni", "Letture non valide ≤ 0,1 %"]),
    TestFirmware64("stress_rele", "Commutazioni rapide e sensore", 0.8, "circa 45 minuti",
                   "Ricrea il guasto storico: commuta pompa, resistenza ed entrambe insieme decine di volte e legge "
                   "il sensore subito dopo ogni commutazione, per vedere se l'alimentazione del sensore cede.",
                   ["Nessun reset del sensore (85 °C) né perdita del bus dopo le commutazioni", "Nessun brownout"]),
    TestFirmware64("sensore_breve", "Sensore senza alimentazione (1 minuto)", 0.2, "circa 10 minuti",
                   "Toglie davvero l'alimentazione al DS18B20 (GPIO25) per 60 s: i relè devono spegnersi entro 20 s e la "
                   "regolazione riprendere da sola. Rivela anche un cablaggio che impedisce il reset del sensore.",
                   ["Relè spenti entro 20 s", "Regolazione ripresa entro 1 minuto"]),
    TestFirmware64("sensore_lungo", "Guasto prolungato del sensore", 0.4, "circa 20 minuti",
                   "Sensore senza alimentazione per 8 minuti: allarme del firmware, email, tentativi di recupero, "
                   "riavvio e ripresa della regolazione.",
                   ["Relè sempre spenti durante il guasto", "Email di allarme", "Ripresa automatica dopo il guasto"]),
    TestFirmware64("wifi", "WiFi assente per 20 minuti", 0.7, "circa 40 minuti",
                   "L'ESP32 si scollega dal WiFi per 20 minuti: deve continuare a regolare da solo, HA deve avvisare, "
                   "poi deve ricollegarsi.",
                   ["Regolazione entro 1 °C durante l'assenza", "Email 'offline'", "Riconnessione automatica"]),
    TestFirmware64("watchdog", "Blocco del programma", 0.2, "circa 10 minuti",
                   "Blocca il programma dell'ESP32: il watchdog deve riavviarlo entro 60 s e la regolazione riprendere.",
                   ["Riavvio entro 75 s con causa task_wdt", "Email 'riavvio anomalo'", "Profilo ripreso"]),
    TestCorrente(),
]
INTERNI = [ControlloProduzione("_preparazione", False), ControlloProduzione("_ripristino", True)]


# ============================================================================ motore
class Motore:
    def __init__(self, ctx, percorso_stato):
        self.ctx = ctx
        self.percorso = percorso_stato
        self.catalogo = {t.id: t for t in CATALOGO}
        self.interni = {t.id: t for t in INTERNI}
        self.s = self._carica()

    # ------------------------------------------------------------------ persistenza
    def _vuoto(self):
        return {"versione": VERSIONE, "stato": "inattivo", "coda": [], "corrente": None, "fase": None,
                "fase_t": 0.0, "test_t": 0.0, "dati": {}, "risultati": {}, "sessione_t": 0.0, "fine_t": 0.0,
                "esito_sessione": None, "motivo": "", "seq": 0, "ack": {}, "ack_recenti": [], "email": [],
                "eventi": []}

    def _carica(self):
        try:
            with open(self.percorso, encoding="utf-8") as f:
                s = json.load(f)
            base = self._vuoto()
            base.update(s)
            return base
        except (OSError, ValueError):
            return self._vuoto()

    def salva(self):
        tmp = self.percorso + ".tmp"
        with open(tmp, "w", encoding="utf-8") as f:
            json.dump(self.s, f, ensure_ascii=False)
        os.replace(tmp, self.percorso)

    # ------------------------------------------------------------------ lettura
    @property
    def d(self):
        return self.s["dati"]

    def ora(self):
        return self.ctx.ora()

    def v(self, chiave):
        return self.ctx.stato(E[chiave])

    def num(self, chiave):
        return numero(self.v(chiave))

    def on(self, chiave):
        return self.v(chiave) == "on"

    def da(self):
        return self.ora() - self.s["fase_t"]

    def istantanea(self):
        return {"fsm": self.v("fsm"), "profilo": self.v("profilo"), "step": self.v("step"), "sp": self.num("sp"),
                "modo": self.v("modo"), "isteresi": self.v("isteresi"), "ore": self.num("ore"),
                "reset": self.v("reset")}

    def metriche(self, t0, t1):
        def st(chiave, conv):
            return serie(self.ctx.storia(E[chiave], t0, t1), t0, t1, conv)
        met = calcola_metriche(st("temp", numero), st("sp", numero), st("freddo", booleano), st("caldo", booleano),
                               st("dati", booleano), st("guasto", booleano), t0, t1)
        met["riavvii"] = conta_riavvii(st("uptime", numero))
        return met

    # ------------------------------------------------------------------ comandi a F1
    def invia(self, cmd, corpo=None, ts=None):
        self.s["seq"] += 1
        rid = f"f1-coll-{cmd}-{int(self.ora()) % 100000}-{self.s['seq']}"
        payload = {"schema_version": "1.0", "request_id": rid, "ts": int(self.ora() if ts is None else ts)}
        payload.update(corpo or {})
        self.ctx.mqtt(CMD + cmd, json.dumps(payload))
        self.s["ack"][rid] = []
        self.s.setdefault("inviati", {})[rid] = self.ora()
        self._pota()
        return rid

    def mqtt_grezzo(self, cmd, testo):
        self.ctx.mqtt(CMD + cmd, testo)

    def costante(self, temp, nome):
        return self.profilo(nome, [{"step": "Collaudo", "temp": round(float(temp), 2), "duration_hours": 2160}])

    def profilo(self, nome, steps):
        nome = nome.replace("°", "")[:30]
        rid = self.invia("profile_set", {"profilo": nome, "avvio_epoch": 0, "steps": steps})
        self.ctx.servizio("input_text", "set_value", entity_id="input_text.f1_v6_profilo_attivo", value=nome)
        self.ctx.servizio("input_select", "select_option", entity_id="input_select.f1_v6_stato_controllo",
                          option="in_corso")
        self.ctx.servizio("input_number", "set_value", entity_id="input_number.f1_v6_avvio_epoch",
                          value=int(self.ora()))
        self.evento(f"Profilo '{nome}' inviato")
        return rid

    def controllo(self, isteresi):
        params = dict(PARAMETRI_PRODUZIONE, isteresi=isteresi)
        return self.invia("control_set", {"control_mode": MODO_PRODUZIONE, "params": params})

    def imposta_helper_produzione(self):
        """Allinea i parametri mostrati in HA a quelli di produzione (niente 'modifiche da applicare')."""
        s = self.ctx.servizio
        s("input_select", "select_option", entity_id="input_select.f1_v6_modo_controllo", option="on_off_isteresi")
        s("input_select", "select_option", entity_id="input_select.f1_v6_preset",
          option="On/Off, isteresi 0,5 °C (produzione)")
        for lato in ("risc", "frigo"):
            s("input_number", "set_value", entity_id=f"input_number.f1_v6_kp_{lato}", value=25)
            s("input_number", "set_value", entity_id=f"input_number.f1_v6_ki_{lato}", value=0.03)
            s("input_number", "set_value", entity_id=f"input_number.f1_v6_kd_{lato}", value=0)
        s("input_number", "set_value", entity_id="input_number.f1_v6_isteresi", value=0.5)

    # ------------------------------------------------------------------ ack ed email
    def su_ack(self, ack):
        rid = ack.get("request_id")
        ack = dict(ack, t=self.ora())
        if rid in self.s["ack"]:
            self.s["ack"][rid].append(ack)
        self.s["ack_recenti"] = (self.s["ack_recenti"] + [ack])[-30:]
        cor = self.s["corrente"]
        if cor:
            try:
                self._test(cor).su_ack(self, ack)
            except Exception:  # noqa: BLE001
                self.ctx.log(traceback.format_exc())
        self.salva()

    def acks(self, rid):
        return self.s["ack"].get(rid, [])

    def ultimo_ack(self, rid):
        a = self.acks(rid)
        return a[-1] if a else None

    def stato_ack(self, rid, timeout_s):
        """'ok' | 'attesa' | 'nessuna risposta' | 'errore: motivo'"""
        if not rid:
            return "ok"
        a = self.ultimo_ack(rid)
        if a is None:
            inviato = self.s.get("inviati", {}).get(rid, self.ora())
            return "attesa" if self.ora() - inviato < timeout_s else "nessuna risposta"
        return "ok" if a.get("status") == "ok" else f"errore: {a.get('reason_code')}"

    def ack_per_motivo(self, motivo, dopo_t):
        for a in reversed(self.s["ack_recenti"]):
            if a.get("t", 0) >= dopo_t and a.get("reason_code") == motivo:
                return a
        return None

    def su_servizio(self, dominio, servizio, dati):
        if dominio == "notify":
            titolo = str((dati or {}).get("title", ""))
            self.s["email"] = (self.s["email"] + [{"t": self.ora(), "titolo": titolo}])[-50:]
            self.salva()

    def email_dopo(self, t, parole):
        return [e["titolo"] for e in self.s["email"]
                if e["t"] >= t and any(p.lower() in e["titolo"].lower() for p in parole)]

    def _pota(self):
        for chiave in ("ack", "inviati"):
            d = self.s.get(chiave, {})
            if len(d) > 60:
                for k in list(d)[:-60]:
                    d.pop(k, None)

    # ------------------------------------------------------------------ fasi ed esiti
    def vai(self, fase):
        self.s["fase"] = fase
        self.s["fase_t"] = self.ora()
        self.salva()

    def esito(self, esito, controlli=None, misure=None, note=None):
        tid = self.s["corrente"]
        r = self.s["risultati"].setdefault(tid, {})
        r.update(esito=esito, controlli=controlli or [], misure=misure or {}, note=note, fine=self.ora())
        self.s["fase"] = "fine"
        t = self._test(tid)
        riga = f"{t.nome}: {esito}" + (f" — {note}" if note else "")
        falliti = [c["voce"] for c in (controlli or []) if not c["ok"]]
        if falliti:
            riga += " (" + ", ".join(falliti) + ")"
        self.evento(riga)
        self.ctx.servizio("logbook", "log", name="F1 Collaudo", message=riga)
        self.salva()

    def fine_interno(self):
        self.s["fase"] = "fine"

    def evento(self, testo):
        self.s["eventi"] = (self.s["eventi"] + [{"t": self.ora(), "testo": testo}])[-40:]
        self.ctx.log(testo)

    def _avviso(self, testo):
        self.ctx.servizio("persistent_notification", "create", notification_id="f1_collaudo_avviso",
                          title="F1 collaudo", message=testo)

    def _test(self, tid):
        return self.catalogo.get(tid) or self.interni[tid]

    # ------------------------------------------------------------------ sessione
    def comando(self, azione, dati=None):
        dati = dati or {}
        attiva = self.s["stato"] in ("in corso", "in chiusura")
        if azione == "avvia":
            if attiva:
                self._avviso("Un collaudo è già in corso: interrompilo prima di avviarne un altro.")
            elif self.v("modalita") != "Collaudo":
                self._avviso("Per avviare i test F1 deve essere in modalità Collaudo.")
            else:
                scelti = [t.id for t in CATALOGO if t.id in (dati.get("test") or [])]
                if not scelti:
                    self._avviso("Nessun test selezionato.")
                else:
                    self._avvia(scelti)
        elif azione == "inserisci" and self.s["stato"] == "in corso":
            nuovi = [t.id for t in CATALOGO if t.id in (dati.get("test") or []) and t.id not in self.s["coda"]
                     and t.id != self.s["corrente"]]
            dopo = dati.get("dopo")
            if dopo in self.s["coda"]:
                i = self.s["coda"].index(dopo) + 1
                self.s["coda"] = self.s["coda"][:i] + nuovi + self.s["coda"][i:]
            else:
                self.s["coda"] = nuovi + self.s["coda"]
            self.s["scelti"] = self.s.get("scelti", []) + [t for t in nuovi if t not in self.s.get("scelti", [])]
            for tid in nuovi:
                self.s["risultati"][tid] = {"esito": "in coda"}
            if nuovi:
                self.evento(f"Aggiunti al collaudo: {', '.join(self.catalogo[t].nome for t in nuovi)}")
        elif azione == "interrompi" and self.s["stato"] == "in corso":
            self._chiudi("interrotto", "interrotto dall'utente")
        elif azione == "salta" and self.s["stato"] == "in corso" and self.s["corrente"] in self.catalogo:
            self.esito("interrotto", note="saltato dall'utente")
            self._prossimo()
        elif azione == "azzera" and not attiva:
            self.s.update(risultati={}, stato="inattivo", eventi=[], esito_sessione=None, motivo="")
        self.salva()
        self.pubblica()

    def _avvia(self, scelti):
        if self.autoverifica().startswith("ERRORE"):
            self._avviso("Collaudo non avviato: " + self.s["autoverifica"])
            return
        self.s.update(stato="in corso", coda=["_preparazione"] + scelti, corrente=None, sessione_t=self.ora(),
                      fine_t=0.0, esito_sessione=None, motivo="", scelti=scelti)
        for tid in scelti:
            self.s["risultati"][tid] = {"esito": "in coda"}
        self.evento(f"Collaudo avviato: {', '.join(self.catalogo[t].nome for t in scelti)}")
        self.ctx.servizio("logbook", "log", name="F1 Collaudo", message=f"Collaudo avviato ({len(scelti)} test)")
        self._prossimo()

    def _prossimo(self):
        fw = self.v("firmware")
        while self.s["coda"]:
            tid = self.s["coda"].pop(0)
            t = self._test(tid)
            self.s.update(corrente=tid, dati={}, test_t=self.ora(), fase="inizio", fase_t=self.ora())
            if tid in self.catalogo:
                if versione(fw) < versione(t.fw_min):
                    self.s["risultati"][tid] = {}
                    self.esito("non applicabile", note=f"richiede il firmware {t.fw_min} (installato {fw})")
                    continue
                self.s["risultati"][tid] = {"esito": "in corso", "inizio": self.ora()}
                self.evento(f"Inizio: {t.nome}")
            t.avvia(self)
            if self.s["fase"] != "fine":
                self.salva()
                return
        self.s["corrente"] = None
        if self.s["stato"] == "in corso":
            self._chiudi("terminato", "tutti i test eseguiti")
        elif self.s["stato"] == "in chiusura":
            self._finale()

    def _chiudi(self, esito_sessione, motivo):
        cor = self.s["corrente"]
        if cor in self.catalogo and self.s["fase"] != "fine":
            self.esito("interrotto", note=motivo)
        for tid in self.s["coda"]:
            if tid in self.catalogo:
                self.s["risultati"][tid] = {"esito": "non eseguito", "note": motivo}
        self.s.update(stato="in chiusura", coda=["_ripristino"], esito_sessione=esito_sessione, motivo=motivo,
                      corrente=None)
        self._prossimo()

    def _finale(self):
        self.s.update(stato=self.s["esito_sessione"] or "terminato", fine_t=self.ora(), corrente=None, fase=None)
        self.ctx.servizio("input_select", "select_option", entity_id=E["modalita"], option="Produzione")
        v = self.verdetto()
        righe = []
        for tid in self.s.get("scelti", []):
            r = self.s["risultati"].get(tid, {})
            righe.append(f"- {self.catalogo[tid].nome}: {r.get('esito', '—')}"
                         + (f" ({r['note']})" if r.get("note") else ""))
        testo = (f"Collaudo {self.s['stato']} ({self.s['motivo']}). Verdetto: {v['testo']}.\n\n" + "\n".join(righe)
                 + "\n\nF1 è tornato in modalità Produzione, con on/off e isteresi 0,5 °C, al parcheggio.")
        self.ctx.servizio("persistent_notification", "create", notification_id="f1_collaudo_esito",
                          title="F1 collaudo concluso", message=testo)
        self.ctx.servizio("notify", "fermentazione_email", title=f"[COLLAUDO] F1 - {v['testo']}", message=testo)
        self.evento(f"Collaudo {self.s['stato']}: {v['testo']}")

    def _controlla_riavvii(self):
        """Registra ogni riavvio dell'ESP32 (uptime che torna indietro); segna quelli attesi dai test."""
        up = self.num("uptime")
        if up is None:
            return
        prec = self.s.get("ultimo_uptime")
        self.s["ultimo_uptime"] = up
        if prec is None or up >= prec:
            return
        cor, fase = self.s["corrente"], self.s["fase"]
        atteso = (cor == "riavvio" and fase in ("attesa", "assestamento")) or \
                 (cor == "corrente" and fase in ("spento", "ritorno"))
        boot = self.boot_da_storico() or (self.ora() - up)
        r = {"t": boot, "causa": self.v("reset"), "test": cor or "", "atteso": atteso,
             "pompa": self.ultima_commutazione(boot)}
        self.s["riavvii"] = (self.s.get("riavvii", []) + [r])[-50:]
        if not atteso:
            self.evento(f"RIAVVIO SPONTANEO dell'ESP32 (causa {r['causa']}) durante '{cor}'")

    def ultima_commutazione(self, prima_di, finestra=120):
        """Ultimo evento prima di un riavvio (nei `finestra` secondi): commutazione di un relè o cambio di setpoint.

        Un cambio di setpoint che richiede di spegnere o accendere la pompa conta come commutazione comandata:
        se l'ESP32 si resetta proprio quando apre il relè, lo 'spento' non arriva mai a Home Assistant.
        """
        migliore = None
        try:
            sp = serie(self.ctx.storia(E["sp"], prima_di - finestra, prima_di + 1), prima_di - finestra, prima_di + 1, numero)
            tt_ = serie(self.ctx.storia(E["temp"], prima_di - finestra, prima_di + 1), prima_di - finestra, prima_di + 1, numero)
            fr = serie(self.ctx.storia(E["freddo"], prima_di - finestra, prima_di + 1), prima_di - finestra, prima_di + 1, booleano)
            tempi_t, tempi_f = [p[0] for p in tt_], [p[0] for p in fr]
            for (ta, va), (tb, vb) in zip(sp, sp[1:]):
                if va is None or vb is None or vb == va or tb > prima_di + 1:
                    continue
                T = valore_a(tt_, tempi_t, tb)
                pompa = valore_a(fr, tempi_f, tb)
                azione = None
                if T is not None and pompa is True and vb > va and T <= vb:
                    azione = "spegnimento comandato della pompa"
                elif T is not None and pompa is False and vb < va and T > vb:
                    azione = "accensione comandata della pompa"
                if azione and (migliore is None or tb > migliore["t"]):
                    migliore = {"t": tb, "rele": azione, "stato": f"(setpoint {fmt(va, 2)} → {fmt(vb, 2)} °C)"}
        except Exception:  # noqa: BLE001
            pass
        for chiave, nome in (("freddo", "pompa"), ("caldo", "riscaldatore")):
            try:
                punti = self.ctx.storia(E[chiave], prima_di - finestra, prima_di + 1)
            except Exception:  # noqa: BLE001
                continue
            prec = None
            for tt, st in punti:
                if st not in ("on", "off"):
                    continue
                if prec is not None and st != prec and prima_di - finestra <= tt <= prima_di + 1:
                    if migliore is None or tt > migliore["t"]:
                        migliore = {"t": tt, "rele": nome, "stato": "accesa" if st == "on" else "spenta"}
                prec = st
        return migliore

    def boot_da_storico(self, finestra=600):
        """Istante del boot più recente: primo messaggio dopo il calo dell'uptime meno l'uptime dichiarato."""
        try:
            up = serie(self.ctx.storia(E["uptime"], self.ora() - finestra, self.ora()), self.ora() - finestra,
                       self.ora(), numero)
        except Exception:  # noqa: BLE001
            return None
        boot, prec = None, None
        for tt, v in up:
            if v is None:
                continue
            if prec is not None and v < prec:
                boot = tt - v
            prec = v
        return boot

    def ricalcola_riavvii(self):
        """Rilegge dallo storico tutti i riavvii della sessione con istante e attribuzione precisi."""
        t0 = self.s.get("sessione_t") or 0
        if not t0:
            return
        up = serie(self.ctx.storia(E["uptime"], t0, self.ora()), t0, self.ora(), numero)
        finestre_attese = []
        for tid in ("riavvio", "corrente"):
            r = self.s["risultati"].get(tid, {})
            if r.get("inizio"):
                finestre_attese.append((r["inizio"], r.get("fine") or self.ora()))
        vecchi = {round(r["t"] / 60): r for r in self.s.get("riavvii", [])}
        nuovi, prec = [], None
        for tt, v in up:
            if v is None:
                continue
            if prec is not None and v < prec:
                boot = tt - v
                v_old = vecchi.get(round(boot / 60)) or vecchi.get(round(boot / 60) - 1) or vecchi.get(round(boot / 60) + 1) or {}
                atteso = any(a <= boot <= b for a, b in finestre_attese)
                causa = v_old.get("causa") if v_old.get("causa") not in (None, "poweron?") else "poweron"
                nuovi.append({"t": boot, "causa": causa, "test": v_old.get("test", ""), "atteso": atteso,
                              "pompa": self.ultima_commutazione(boot)})
            prec = v
        self.s["riavvii"] = nuovi[-50:]
        r = self.s["risultati"].get("stress_pompa")
        if r and r.get("inizio") and r.get("fine") and r.get("misure"):
            nel_test = [x for x in nuovi if r["inizio"] <= x["t"] <= r["fine"]]
            r["misure"]["Riavvii"] = TestStressPompa.descrivi_riavvii(nel_test)
        self.salva()

    def recupera_riavvii(self):
        """Riavvii avvenuti durante la sessione ma non registrati (motore aggiornato a sessione in corso)."""
        t0 = self.s.get("sessione_t") or 0
        if not t0 or "riavvii" in self.s:
            return
        self.s["riavvii"] = []
        up = serie(self.ctx.storia(E["uptime"], t0, self.ora()), t0, self.ora(), numero)
        prec = None
        for tt, v in up:
            if v is None:
                continue
            if prec is not None and v < prec:
                boot = tt - v
                self.s["riavvii"].append({"t": boot, "causa": "poweron?", "test": "(da storico)", "atteso": False,
                                          "pompa": self.ultima_commutazione(boot)})
                self.evento(f"RIAVVIO SPONTANEO dell'ESP32 recuperato dallo storico ({ore_min(self.ora() - boot)} fa)")
            prec = v
        self.salva()

    def riavvii_spontanei(self, dopo_t=None):
        dopo_t = self.s["sessione_t"] if dopo_t is None else dopo_t
        return [r for r in self.s.get("riavvii", []) if not r["atteso"] and r["t"] >= dopo_t]

    def tick(self):
        try:
            self._controlla_riavvii()
            cor = self.s["corrente"]
            if self.s["stato"] in ("in corso", "in chiusura") and cor:
                if self.s["stato"] == "in corso" and self.v("modalita") != "Collaudo":
                    self._chiudi("interrotto", "F1 non è più in modalità Collaudo")
                elif not self._sicurezza():
                    self._test(cor).tick(self)
                    if self.s["fase"] == "fine":
                        self._prossimo()
        except Exception as ex:  # noqa: BLE001
            self.ctx.log(traceback.format_exc())
            cor = self.s["corrente"]
            if cor in self.catalogo:
                self.esito("fallito", note=f"errore interno del collaudo: {ex}")
            else:
                self.fine_interno()
            self._prossimo()
        self.salva()
        self.pubblica()

    def _sicurezza(self):
        """Limiti del collaudo: fuori da questi il collaudo si ferma e F1 torna al parcheggio."""
        if self.s["stato"] != "in corso" or self.s["corrente"] not in self.catalogo:
            return False
        T = self.num("temp")
        if T is not None and (T < LIMITE_BASSO_COLLAUDO or T > LIMITE_ALTO_COLLAUDO):
            self._chiudi("interrotto", f"temperatura {fmt(T, 2)} °C fuori dai limiti del collaudo")
            return True
        return False

    def autoverifica(self):
        """Legge lo storico dell'ultima ora: se fallisce, le valutazioni dei test fallirebbero dopo ore."""
        try:
            met = self.metriche(self.ora() - 3600, self.ora())
            self.s["autoverifica"] = (f"storico ok: T {met['t_min']}-{met['t_max']} °C, pompa {met['duty_freddo']} %, "
                                      f"{met['senza_dati_min']} min senza dati")
        except Exception as ex:  # noqa: BLE001
            self.s["autoverifica"] = f"ERRORE lettura storico: {ex}"
            self.ctx.log(traceback.format_exc())
        self.ctx.log(self.s["autoverifica"])
        return self.s["autoverifica"]

    # ------------------------------------------------------------------ verdetto ed entità
    def verdetto(self):
        essenziali = [t for t in CATALOGO if t.essenziale]
        falliti = [t.nome for t in essenziali if self.s["risultati"].get(t.id, {}).get("esito") == "fallito"]
        avvisi = [t.nome for t in essenziali if self.s["risultati"].get(t.id, {}).get("esito") == "avviso"]
        mancanti = [t.nome for t in essenziali
                    if self.s["risultati"].get(t.id, {}).get("esito") not in ("superato", "avviso", "fallito")]
        spontanei = self.riavvii_spontanei() if self.s["sessione_t"] else []
        if spontanei:
            falliti = falliti + [f"{len(spontanei)} riavvii spontanei dell'ESP32"]
        if falliti:
            stato, testo = "non pronto", f"NON pronto: {len(falliti)} problemi"
        elif mancanti:
            stato, testo = "incompleto", f"incompleto: {len(mancanti)} test da eseguire"
        else:
            stato, testo = "pronto", "pronto per la birra" + (f" ({len(avvisi)} avvisi)" if avvisi else "")
        return {"stato": stato, "testo": testo, "falliti": falliti, "avvisi": avvisi, "mancanti": mancanti}

    def stima_fine(self):
        if self.s["stato"] not in ("in corso", "in chiusura"):
            return None
        resto = sum(self.catalogo[t].stima_h for t in self.s["coda"] if t in self.catalogo)
        cor = self.s["corrente"]
        if cor in self.catalogo:
            resto += max(self.catalogo[cor].stima_h - (self.ora() - self.s["test_t"]) / 3600, 0.25)
        return self.ora() + resto * 3600

    def pubblica(self):
        cor = self.s["corrente"]
        t = self._test(cor) if cor else None
        try:
            fase = t.descrivi(self) if t else ""
        except Exception:  # noqa: BLE001
            fase = self.s["fase"] or ""
        fatti = [tid for tid in self.s.get("scelti", [])
                 if self.s["risultati"].get(tid, {}).get("esito") not in ("in coda", "in corso", None)]
        self.ctx.entita("sensor.f1_collaudo_sessione", self.s["stato"], {
            "friendly_name": "F1 Collaudo", "icon": "mdi:clipboard-check-multiple",
            "test": t.nome if t else "", "test_id": cor or "", "fase": fase,
            "inizio": self.s["sessione_t"] or None, "fine": self.s["fine_t"] or None,
            "stima_fine": self.stima_fine(), "completati": len(fatti), "totale": len(self.s.get("scelti", [])),
            "motivo": self.s["motivo"], "autoverifica": self.s.get("autoverifica"),
            "riavvii_spontanei": [f"{r['t']:.0f}|{r['causa']}|{r['test']}" for r in self.riavvii_spontanei()]
            if self.s["sessione_t"] else [],
            "eventi": [f"{e['t']:.0f}|{e['testo']}" for e in self.s["eventi"][-12:]],
            "versione_motore": VERSIONE})
        v = self.verdetto()
        self.ctx.entita("sensor.f1_collaudo_verdetto", v["stato"], {
            "friendly_name": "F1 Collaudo verdetto", "icon": "mdi:beer", "testo": v["testo"],
            "falliti": v["falliti"], "avvisi": v["avvisi"], "mancanti": v["mancanti"]})
        for tid, test in self.catalogo.items():
            r = self.s["risultati"].get(tid, {})
            attr = test.info()
            attr.update(friendly_name=f"F1 Collaudo {test.nome}", icon="mdi:test-tube",
                        controlli=r.get("controlli", []), misure=r.get("misure", {}), note=r.get("note"),
                        inizio=r.get("inizio"), fine=r.get("fine"))
            self.ctx.entita(f"sensor.f1_collaudo_{tid}", r.get("esito", "non eseguito"), attr)
