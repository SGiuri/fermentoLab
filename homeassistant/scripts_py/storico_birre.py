#!/usr/bin/env python3
"""storico_birre.py — schede delle birre fatte, per la dashboard "Storico birre".

Una scheda per birra in /config/storico_birre/<id>.json (formato in firmware/fermentatore_v6/storico_birre/README.md).
Lanciato da shell_command (pacchetto storico_birre.yaml): stampa JSON su stdout, che HA legge con
response_variable. Su stdout solo ASCII (gli accenti escono come escape JSON): non dipende dalla codifica del
container. Nessuna dipendenza esterna.

Comandi:
  elenco                          le schede, dalla più recente: id, nome, etichetta, inizio, stato
  leggi <id>                      la scheda intera ({} se non c'è)
  nota <id> <testo_b64> [quando]  aggiunge una nota (testo in base64, UTF-8; quando "YYYY-MM-DDTHH:MM", assente = ora)
  elimina_ultima_nota <id>        toglie l'ultima nota aggiunta
  ripristina_profilo <id>         riscrive il profilo della scheda in /config/profili_fermentazione
  chiudi <id> [fine] [stato_b64]  a fine birra cristallizza la scheda: curve e letture copiate, fine vera
                                  (fine "YYYY-MM-DDTHH:MM", assente = ora; stato in base64, assente = "conclusa")

Il testo della nota arriva in base64 perché shell_command, con i template, divide gli argomenti come una shell:
virgolette e apostrofi nel testo spezzerebbero il comando.
"""

import base64
import datetime
import json
import os
import re
import sys

CARTELLA = os.environ.get("STORICO_DIR", "/config/storico_birre")
PROFILI = os.environ.get("PROFILI_DIR", "/config/profili_fermentazione")
# Curve cristallizzate: in www, perché la dashboard le scarica da /local (servito senza login).
CURVE = os.environ.get("CURVE_DIR", "/config/www/storico_birre")
# ha_url e ha_token, gli stessi di avvia_fermentazione_v6.py.
CONFIG_HA = os.environ.get("CONFIG_HA", "/config/scripts_py/mqtt_broker.json")
ID_VALIDO = re.compile(r"^[A-Za-z0-9_-]{1,80}$")


def esci(errore: str):
    print(json.dumps({"errore": errore}))
    sys.exit(1)


def percorso(id_birra: str) -> str:
    if not ID_VALIDO.match(id_birra):
        esci(f"id non valido: {id_birra}")
    return os.path.join(CARTELLA, id_birra + ".json")


def carica(id_birra: str) -> dict:
    p = percorso(id_birra)
    if not os.path.isfile(p):
        esci(f"scheda non trovata: {id_birra}")
    with open(p, encoding="utf-8") as f:
        return json.load(f)


def salva(id_birra: str, scheda: dict):
    # Scrittura atomica, con la versione precedente in <id>.json.bak.
    p = percorso(id_birra)
    tmp = p + ".tmp"
    with open(tmp, "w", encoding="utf-8", newline="\n") as f:
        f.write(json.dumps(scheda, ensure_ascii=False, indent=2) + "\n")
    if os.path.isfile(p):
        os.replace(p, p + ".bak")
    os.replace(tmp, p)


def etichetta(scheda: dict) -> str:
    inizio = scheda.get("inizio", "")
    try:
        data = datetime.datetime.fromisoformat(inizio).strftime("%d/%m/%Y")
    except ValueError:
        data = "?"
    return f"{data} · {scheda.get('nome', scheda.get('id', '?'))}"


def cmd_elenco():
    birre = []
    if os.path.isdir(CARTELLA):
        for nome in sorted(os.listdir(CARTELLA)):
            if not nome.endswith(".json") or not ID_VALIDO.match(nome[:-5]):
                continue
            try:
                with open(os.path.join(CARTELLA, nome), encoding="utf-8") as f:
                    s = json.load(f)
            except (OSError, ValueError):
                continue
            birre.append({"id": nome[:-5], "nome": s.get("nome", nome[:-5]), "etichetta": etichetta(s),
                          "inizio": s.get("inizio", ""), "stato": s.get("stato", "")})
    birre.sort(key=lambda b: b["inizio"], reverse=True)
    print(json.dumps({"birre": birre}))


def cmd_leggi(id_birra: str):
    # Scheda assente = scheda vuota, senza errore: all'avvio di HA la si legge prima che il menu sia pronto.
    if not os.path.isfile(percorso(id_birra)):
        print("{}")
        return
    print(json.dumps(carica(id_birra)))


def cmd_nota(id_birra: str, testo_b64: str, quando: str = ""):
    try:
        testo = base64.b64decode(testo_b64).decode("utf-8").strip()
    except (ValueError, UnicodeDecodeError):
        esci("testo non leggibile")
    if not testo:
        esci("nota vuota")
    quando = quando.strip().replace("T", " ")[:16]
    if quando:
        try:
            datetime.datetime.strptime(quando, "%Y-%m-%d %H:%M")
        except ValueError:
            esci(f"data non valida: {quando}")
    else:
        quando = datetime.datetime.now().strftime("%Y-%m-%d %H:%M")
    scheda = carica(id_birra)
    note = scheda.setdefault("note", [])
    # Si conserva l'ordine di inserimento (serve a "elimina l'ultima"); la dashboard ordina per data.
    note.append({"quando": quando, "testo": testo, "aggiunta": datetime.datetime.now().strftime("%Y-%m-%d %H:%M")})
    salva(id_birra, scheda)
    print(json.dumps({"ok": True, "note": len(note)}))


def cmd_elimina_ultima_nota(id_birra: str):
    scheda = carica(id_birra)
    note = scheda.get("note", [])
    if not note:
        esci("nessuna nota")
    tolta = note.pop()
    salva(id_birra, scheda)
    print(json.dumps({"ok": True, "tolta": tolta}))


def cmd_ripristina_profilo(id_birra: str):
    # Il profilo della scheda torna un file in profili_fermentazione. Se c'è già un file con quel nome e un
    # contenuto diverso non lo tocca: scrive <nome>_<id>.json.
    scheda = carica(id_birra)
    prof = scheda.get("profilo") or {}
    nome = prof.get("nome", "")
    if not ID_VALIDO.match(nome) or not prof.get("profilo"):
        esci("la scheda non ha un profilo")
    contenuto = {k: prof[k] for k in ("fg_obiettivo", "profilo", "eventi") if k in prof}
    testo = json.dumps(contenuto, ensure_ascii=False, indent=2) + "\n"
    dest = os.path.join(PROFILI, nome + ".json")
    if os.path.isfile(dest):
        with open(dest, encoding="utf-8") as f:
            try:
                uguale = json.load(f) == contenuto
            except ValueError:
                uguale = False
        if uguale:
            print(json.dumps({"ok": True, "file": nome, "scritto": False}))
            return
        nome = f"{nome}_{id_birra}"[:80]
        dest = os.path.join(PROFILI, nome + ".json")
    with open(dest, "w", encoding="utf-8", newline="\n") as f:
        f.write(testo)
    print(json.dumps({"ok": True, "file": nome, "scritto": True}))


def cmd_chiudi(id_birra: str, fine: str = "", stato_b64: str = ""):
    # Cristallizza la scheda a fine birra: le curve misurate (statistiche orarie) vanno in un file servito da
    # /local, le letture della dashboard Cotta entrano nella scheda, la fine diventa quella vera. Da qui la
    # pagina della birra non dipende più da entità o statistiche vive: si possono solo aggiungere note.
    import asyncio
    import time
    import aiohttp  # c'è nel container di HA; gli altri comandi restano senza dipendenze

    scheda = carica(id_birra)
    if fine:
        try:
            t_fine = datetime.datetime.strptime(fine.replace("T", " ")[:16], "%Y-%m-%d %H:%M").astimezone()
        except ValueError:
            esci(f"data non valida: {fine}")
    else:
        t_fine = datetime.datetime.now().astimezone()
    stato = "conclusa"
    if stato_b64:
        try:
            stato = base64.b64decode(stato_b64).decode("utf-8").strip() or stato
        except (ValueError, UnicodeDecodeError):
            esci("stato non leggibile")
    with open(CONFIG_HA, encoding="utf-8") as f:
        cfg = json.load(f)
    url = cfg.get("ha_url", "http://localhost:8123").rstrip("/")
    token = cfg["ha_token"]

    async def scarica():
        curve, letture_live = {}, None
        async with aiohttp.ClientSession(headers={"Authorization": f"Bearer {token}"}) as sessione:
            async with sessione.ws_connect(url + "/api/websocket") as ws:
                await ws.receive_json()
                await ws.send_json({"type": "auth", "access_token": token})
                if (await ws.receive_json()).get("type") != "auth_ok":
                    raise RuntimeError("autenticazione websocket rifiutata")
                for n, s in enumerate(scheda.get("serie", []), start=1):
                    da = datetime.datetime.fromisoformat(s["dal"])
                    al = datetime.datetime.fromisoformat(s["al"]) if s.get("al") else t_fine
                    if al <= da:
                        continue
                    await ws.send_json({"id": n, "type": "recorder/statistics_during_period",
                                        "start_time": da.isoformat(), "end_time": al.isoformat(),
                                        "statistic_ids": [s["entity"]], "period": "hour", "types": ["mean"]})
                    while True:
                        r = await ws.receive_json()
                        if r.get("id") == n:
                            break
                    cifre = 4 if s.get("asse") == "sg" else 2
                    # Come la dashboard dal vivo: il punto a metà dell'ora, o al suo inizio per i setpoint a gradini.
                    curve[s["entity"] + "|" + s["dal"]] = [
                        [int(p["start"] if s.get("gradini") else (p["start"] + p["end"]) / 2), round(p["mean"], cifre)]
                        for p in (r.get("result") or {}).get(s["entity"], []) if p.get("mean") is not None]
            if scheda.get("letture_live"):
                async with sessione.get(url + "/api/states/" + scheda["letture_live"]) as r:
                    if r.status == 200:
                        letture_live = (await r.json()).get("attributes", {})
        return curve, letture_live

    curve, live = asyncio.run(scarica())
    ora = datetime.datetime.now().astimezone()
    os.makedirs(CURVE, exist_ok=True)
    with open(os.path.join(CURVE, id_birra + "_curve.json"), "w", encoding="utf-8", newline="\n") as f:
        json.dump({"id": id_birra, "cristallizzata": ora.isoformat(timespec="seconds"), "curve": curve}, f)

    if live is not None:
        righe = [{"quando": datetime.datetime.fromtimestamp(r["ts"] / 1000).strftime("%Y-%m-%d %H:%M"),
                  "strumento": r.get("strumento", ""), "letta": r.get("letta"), "temp": r.get("temp", 0),
                  "sg": r.get("sg"), "og": bool(r.get("og")), "nota": r.get("nota", "")}
                 for r in live.get("letture", []) if isinstance(r.get("ts"), (int, float))]
        if righe:
            scheda["letture"] = righe
        if scheda.get("og") is None and isinstance(live.get("og"), (int, float)):
            scheda["og"] = live["og"]
    scheda.pop("letture_live", None)
    for s in scheda.get("serie", []):
        s.setdefault("al", t_fine.isoformat(timespec="seconds"))
    scheda["fine"] = t_fine.isoformat(timespec="seconds")
    scheda["fine_prevista"] = False
    scheda["stato"] = stato
    scheda["cristallizzata"] = ora.isoformat(timespec="seconds")
    # ?v=: un'altra chiusura cambia l'indirizzo, e il browser non usa la copia vecchia.
    scheda["curve"] = f"/local/storico_birre/{id_birra}_curve.json?v={int(time.time())}"
    salva(id_birra, scheda)
    print(json.dumps({"ok": True, "punti": {k: len(v) for k, v in curve.items()},
                      "letture": len(scheda.get("letture", []))}))


def main(argv):
    comandi = {"elenco": (cmd_elenco, 0, 0), "leggi": (cmd_leggi, 1, 1), "nota": (cmd_nota, 2, 3),
               "elimina_ultima_nota": (cmd_elimina_ultima_nota, 1, 1),
               "ripristina_profilo": (cmd_ripristina_profilo, 1, 1), "chiudi": (cmd_chiudi, 1, 3)}
    if not argv or argv[0] not in comandi:
        esci("comando sconosciuto")
    f, minimo, massimo = comandi[argv[0]]
    args = argv[1:]
    if not minimo <= len(args) <= massimo:
        esci("argomenti sbagliati")
    f(*args)


if __name__ == "__main__":
    main(sys.argv[1:])
