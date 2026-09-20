"""App AppDaemon del collaudo di F1: collega il motore (collaudo_motore.py) a Home Assistant.

Eventi ascoltati:
- f1_collaudo_comando {azione: avvia|interrompi|salta|azzera, test: [...]}  dagli script della dashboard
- f1_v6_ack {payload: "<json>"}   inoltrato dall'automazione che ascolta fermentazione/v6/f1/esp/ack
- call_service (dominio notify)   per verificare che le email di allarme partano
Tick ogni 30 s. Lo stato del collaudo è salvato in collaudo_stato.json accanto a questo file.
"""
import datetime
import json
import os
import time

import appdaemon.plugins.hass.hassapi as hass

import collaudo_motore


class _Contesto:
    def __init__(self, app):
        self.app = app

    def ora(self):
        return time.time()

    def stato(self, entity_id):
        return self.app.get_state(entity_id)

    def storia(self, entity_id, t0, t1):
        inizio = datetime.datetime.fromtimestamp(t0, datetime.timezone.utc)
        fine = datetime.datetime.fromtimestamp(t1, datetime.timezone.utc)
        dati = self.app.get_history(entity_id=entity_id, start_time=inizio, end_time=fine) or []
        punti = []
        for gruppo in dati:
            for s in gruppo if isinstance(gruppo, list) else [gruppo]:
                quando = s.get("last_changed") or s.get("last_updated")
                if not quando:
                    continue
                if isinstance(quando, str):
                    quando = datetime.datetime.fromisoformat(quando.replace("Z", "+00:00"))
                punti.append((quando.timestamp(), s.get("state")))
        punti.sort(key=lambda p: p[0])
        return punti

    def mqtt(self, topic, payload):
        self.app.call_service("mqtt/publish", topic=topic, payload=payload, qos=1, retain=False)

    def servizio(self, dominio, servizio, **dati):
        self.app.call_service(f"{dominio}/{servizio}", **dati)

    def entita(self, entity_id, stato, attributi):
        self.app.set_state(entity_id, state=stato, attributes=attributi, replace=True)

    def log(self, testo):
        self.app.log(testo)


class CollaudoF1(hass.Hass):
    def initialize(self):
        percorso = os.path.join(os.path.dirname(os.path.abspath(__file__)), "collaudo_stato.json")
        self.motore = collaudo_motore.Motore(_Contesto(self), percorso)
        self.listen_event(self._comando, "f1_collaudo_comando")
        self.listen_event(self._ack, "f1_v6_ack")
        self.listen_event(self._servizio, "call_service")
        self.run_every(self._tick, "now+10", 30)
        self.motore.autoverifica()
        self.motore.recupera_riavvii()
        self.motore.ricalcola_riavvii()
        self.motore.pubblica()
        self.log(f"Collaudo F1 pronto (motore {collaudo_motore.VERSIONE}), stato: {self.motore.s['stato']}")

    def _tick(self, kwargs):
        self.motore.tick()

    def _comando(self, evento, dati, kwargs):
        self.motore.comando(dati.get("azione", ""), dati)

    def _ack(self, evento, dati, kwargs):
        payload = dati.get("payload")
        try:
            ack = json.loads(payload) if isinstance(payload, str) else dict(payload or {})
        except ValueError:
            return
        self.motore.su_ack(ack)

    def _servizio(self, evento, dati, kwargs):
        if dati.get("domain") == "notify":
            self.motore.su_servizio("notify", dati.get("service"), dati.get("service_data") or {})
