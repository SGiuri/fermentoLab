#!/usr/bin/env python3
"""
test_e2e.py  — Test E2E per Fermentatore V6 via MQTT

Prerequisiti:
    pip install paho-mqtt python-dotenv

Credenziali (in ordine di priorità):
    1. Variabili d'ambiente: MQTT_SERVER, MQTT_USER, MQTT_PASS, MQTT_PORT (default 8883)
    2. File .env nella stessa directory dello script

ATTENZIONE: invia comandi reali (stop/start/profile_set) al device indicato.
Su un fermentatore in uso interrompe il controllo della temperatura: usare solo
su un banco prova e confermare con --banco-prova.

Utilizzo:
    python test_e2e.py --banco-prova      # tutti i test
    python test_e2e.py --device f2        # altro device
    python test_e2e.py --no-tls           # broker locale senza TLS
    python test_e2e.py --timeout 20       # timeout ACK (secondi)
"""

import argparse
import json
import os
import sys
import time
import threading
import uuid
from typing import Optional

# Carica .env se presente nella directory dello script
_script_dir = os.path.dirname(os.path.abspath(__file__))
_env_file = os.path.join(_script_dir, ".env")
if os.path.exists(_env_file):
    with open(_env_file) as _f:
        for _line in _f:
            _line = _line.strip()
            if _line and not _line.startswith("#") and "=" in _line:
                _k, _v = _line.split("=", 1)
                os.environ.setdefault(_k.strip(), _v.strip())

try:
    import paho.mqtt.client as mqtt
except ImportError:
    print("ERRORE: paho-mqtt non installato. Esegui: pip install paho-mqtt")
    sys.exit(1)

# ─────────────────────────────────────────────
# Topic helpers
# ─────────────────────────────────────────────
def cmd_topic(device_id: str, cmd: str) -> str:
    return f"fermentazione/v6/{device_id}/ha/cmd/{cmd}"

def ack_topic(device_id: str) -> str:
    return f"fermentazione/v6/{device_id}/esp/ack"

def stato_topic(device_id: str) -> str:
    return f"fermentazione/v6/{device_id}/esp/state"

def availability_topic(device_id: str) -> str:
    return f"fermentazione/v6/{device_id}/esp/availability"

# ─────────────────────────────────────────────
# Test runner
# ─────────────────────────────────────────────
class E2ERunner:
    def __init__(self, broker: str, port: int, user: str, password: str,
                 device_id: str, tls: bool, timeout: float):
        self.broker = broker
        self.port = port
        self.user = user
        self.password = password
        self.device_id = device_id
        self.timeout = timeout
        self._passed = 0
        self._failed = 0

        self._ack_event = threading.Event()
        self._stato_event = threading.Event()
        self._last_ack: Optional[dict] = None
        self._last_stato: Optional[dict] = None
        self._availability: Optional[str] = None

        self._client = mqtt.Client(client_id=f"e2e_test_{uuid.uuid4().hex[:8]}", protocol=mqtt.MQTTv311)
        if tls:
            self._client.tls_set()
        if user:
            self._client.username_pw_set(user, password)
        self._client.on_connect    = self._on_connect
        self._client.on_message    = self._on_message
        self._client.on_disconnect = self._on_disconnect

    def connect(self) -> bool:
        self._connected_event = threading.Event()
        self._client.connect(self.broker, self.port, keepalive=30)
        self._client.loop_start()
        ok = self._connected_event.wait(timeout=15)
        if not ok:
            print(f"  ERRORE: timeout connessione a {self.broker}:{self.port}")
        return ok

    def disconnect(self):
        self._client.loop_stop()
        self._client.disconnect()

    # ── Callbacks ──────────────────────────────
    def _on_connect(self, client, userdata, flags, rc):
        if rc == 0:
            client.subscribe(ack_topic(self.device_id), qos=1)
            client.subscribe(stato_topic(self.device_id), qos=0)
            client.subscribe(availability_topic(self.device_id), qos=1)
            self._connected_event.set()

    def _on_disconnect(self, client, userdata, rc):
        pass

    def _on_message(self, client, userdata, msg):
        if msg.topic == availability_topic(self.device_id):
            self._availability = msg.payload.decode()
            return
        try:
            payload = json.loads(msg.payload.decode())
        except Exception:
            return
        if msg.topic == ack_topic(self.device_id):
            self._last_ack = payload
            self._ack_event.set()
        elif msg.topic == stato_topic(self.device_id):
            self._last_stato = payload
            self._stato_event.set()

    # ── Helpers ────────────────────────────────
    def _send_cmd(self, cmd: str, payload: dict) -> str:
        req_id = f"e2e_{uuid.uuid4().hex[:12]}"
        payload["request_id"] = req_id
        payload["schema_version"] = "1.0"
        # ts recente: il firmware >= 6.2.0 rifiuta i comandi senza ts appena dopo la
        # sottoscrizione (probabili retained) e quelli con ts vecchio
        payload["ts"] = int(time.time())
        self._ack_event.clear()
        self._last_ack = None
        self._client.publish(cmd_topic(self.device_id, cmd),
                             json.dumps(payload), qos=1)
        return req_id

    def _wait_ack(self, req_id: str) -> Optional[dict]:
        if not self._ack_event.wait(timeout=self.timeout):
            return None
        ack = self._last_ack
        if ack and ack.get("request_id") == req_id:
            return ack
        return None

    def _assert(self, name: str, condition: bool, detail: str = ""):
        if condition:
            self._passed += 1
            print(f"  [PASS] {name}")
        else:
            self._failed += 1
            print(f"  [FAIL] {name}" + (f"  ({detail})" if detail else ""))

    # ── Test cases ─────────────────────────────
    def tc01_stop(self):
        """TC01: stop — il device risponde con ACK ok"""
        req_id = self._send_cmd("stop", {})
        ack = self._wait_ack(req_id)
        self._assert("TC01 stop ACK ricevuto", ack is not None,
                     "timeout" if ack is None else "")
        if ack:
            self._assert("TC01 stop ACK status=ok", ack.get("status") == "ok",
                         f"status={ack.get('status')!r}")
            self._assert("TC01 stop ACK schema_version",
                         ack.get("schema_version") == "1.0",
                         f"schema_version={ack.get('schema_version')!r}")

    def tc02_stato_heartbeat(self):
        """TC02: stato MQTT — il device pubblica stato con campi obbligatori"""
        self._stato_event.clear()
        self._last_stato = None
        # Aspetta fino a STATUS_INTERVAL_MS (max 65 s nel fw, usiamo timeout esteso)
        got = self._stato_event.wait(timeout=max(self.timeout, 70))
        self._assert("TC02 stato ricevuto", got, "timeout")
        if got and self._last_stato:
            s = self._last_stato
            for field in ("temp_reale", "setpoint", "stato_fsm", "riscaldatore",
                          "refrigeratore", "sensore_ok", "uptime_s", "firmware",
                          "fermenter_id"):
                self._assert(f"TC02 stato campo '{field}'",
                             field in s, f"manca nel payload")

    def tc03_start(self):
        """TC03: start — il device risponde con ACK ok"""
        req_id = self._send_cmd("start", {})
        ack = self._wait_ack(req_id)
        self._assert("TC03 start ACK ricevuto", ack is not None,
                     "timeout" if ack is None else "")
        if ack:
            self._assert("TC03 start ACK status=ok", ack.get("status") == "ok",
                         f"status={ack.get('status')!r}")

    def tc04_profile_set(self):
        """TC04: profile_set — il device accetta un profilo minimo"""
        # Formato piatto letto da parsaProfiloJSON; avvia=False: salva senza avviare
        profile_payload = {
            "profilo": "e2e_test_lager",
            "avvio_epoch": int(time.time()),
            "avvia": False,
            "steps": [
                {"step": "fermentazione", "temp": 10.0, "duration_hours": 168}
            ]
        }
        req_id = self._send_cmd("profile_set", profile_payload)
        ack = self._wait_ack(req_id)
        self._assert("TC04 profile_set ACK ricevuto", ack is not None,
                     "timeout" if ack is None else "")
        if ack:
            self._assert("TC04 profile_set ACK status=ok",
                         ack.get("status") == "ok",
                         f"status={ack.get('status')!r} reason={ack.get('reason_code')!r}")

    def tc05_stop_after_profile(self):
        """TC05: stop dopo profile_set — il device si ferma senza perdere il profilo"""
        req_id = self._send_cmd("stop", {})
        ack = self._wait_ack(req_id)
        self._assert("TC05 stop (post-profilo) ACK ricevuto", ack is not None,
                     "timeout" if ack is None else "")
        if ack:
            self._assert("TC05 stop (post-profilo) ACK status=ok",
                         ack.get("status") == "ok",
                         f"status={ack.get('status')!r}")

    # ── Entry point ────────────────────────────
    def run_all(self):
        print(f"\n=== Fermentatore V6 E2E Test  (device={self.device_id}, broker={self.broker}:{self.port}) ===\n")
        if not self.connect():
            print("Connessione fallita. Test annullati.")
            return 1

        # Breve attesa per la sottoscrizione (availability è retained)
        time.sleep(2)
        self._assert("TC00 availability online (retained)", self._availability == "online",
                     f"availability={self._availability!r}")

        try:
            print("[TC01] stop baseline")
            self.tc01_stop();             time.sleep(1)
            print("[TC02] stato heartbeat")
            self.tc02_stato_heartbeat()
            print("[TC03] start")
            self.tc03_start();            time.sleep(1)
            print("[TC04] profile_set")
            self.tc04_profile_set();      time.sleep(1)
            print("[TC05] stop post-profilo")
            self.tc05_stop_after_profile()
        finally:
            self.disconnect()

        total = self._passed + self._failed
        print(f"\n{'='*55}")
        print(f"Risultato: {self._passed}/{total} PASS  {self._failed}/{total} FAIL")
        print('='*55)
        return 0 if self._failed == 0 else 1


# ─────────────────────────────────────────────
# CLI
# ─────────────────────────────────────────────
def main():
    p = argparse.ArgumentParser(description="Fermentatore V6 — E2E MQTT test")
    p.add_argument("--broker",    default=os.environ.get("MQTT_SERVER", ""),
                   help="Hostname broker MQTT (o env MQTT_SERVER)")
    p.add_argument("--port",      type=int,
                   default=int(os.environ.get("MQTT_PORT", "8883")))
    p.add_argument("--user",      default=os.environ.get("MQTT_USER", ""))
    p.add_argument("--password",  default=os.environ.get("MQTT_PASS", ""))
    p.add_argument("--device",    default=os.environ.get("MQTT_DEVICE_ID", "f1"),
                   help="Device ID (default f1)")
    p.add_argument("--no-tls",    action="store_true", help="Disabilita TLS")
    p.add_argument("--timeout",   type=float, default=10.0,
                   help="Timeout attesa ACK in secondi (default 10)")
    p.add_argument("--banco-prova", action="store_true",
                   help="Conferma che il device non sta controllando una fermentazione reale")
    args = p.parse_args()

    if not args.banco_prova:
        print("ERRORE: il test invia stop/start/profile_set al device. Aggiungere --banco-prova"
              " solo se il device NON sta controllando una fermentazione.")
        sys.exit(2)

    if not args.broker:
        print("ERRORE: broker non specificato. Usa --broker o imposta MQTT_SERVER.")
        sys.exit(1)

    runner = E2ERunner(
        broker=args.broker,
        port=args.port,
        user=args.user,
        password=args.password,
        device_id=args.device,
        tls=not args.no_tls,
        timeout=args.timeout,
    )
    sys.exit(runner.run_all())


if __name__ == "__main__":
    main()
