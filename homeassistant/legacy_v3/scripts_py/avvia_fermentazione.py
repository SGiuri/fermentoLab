#!/usr/bin/env python3
"""
avvia_fermentazione.py
----------------------
Legge il profilo di fermentazione JSON da /config/profili_fermentazione/,
converte duration_days/ramp_days in duration_hours/ramp_hours,
aggiunge il timestamp di avvio e pubblica il payload su MQTT.

Uso:
  python3 avvia_fermentazione.py <profilo_nome> <avvio_epoch> [isteresi] [allarme_timeout_minuti]

Esempio:
  python3 avvia_fermentazione.py lager_generica 1745000000 0.3 5
"""

import sys
import json
import os

PROFILI_DIR = "/config/profili_fermentazione"
MQTT_HOST   = "127.0.0.1"
MQTT_PORT   = 1883
MQTT_TOPIC  = "fermentazione/f1/cmd/profilo"


def load_profile(nome: str) -> dict:
    path = os.path.join(PROFILI_DIR, f"{nome}.json")
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def convert_profile(raw: dict, nome: str, avvio_epoch: int,
                    isteresi: float, allarme_timeout: int) -> dict:
    steps = []
    for step in raw["profilo"]:
        s = {"step": step["step"], "temp": step["temp"]}
        if "duration_days" in step:
            s["duration_hours"] = round(step["duration_days"] * 24, 4)
        if "ramp_days" in step:
            s["ramp_hours"] = round(step["ramp_days"] * 24, 4)
        steps.append(s)

    return {
        "profilo":               nome,
        "avvio_epoch":           avvio_epoch,
        "steps":                 steps,
        "isteresi":              isteresi,
        "min_cycle_minutes":     5,
        "allarme_timeout_minuti": allarme_timeout,
    }


def publish_mqtt(payload: dict) -> None:
    import paho.mqtt.publish as mqtt_pub
    mqtt_pub.single(
        topic=MQTT_TOPIC,
        payload=json.dumps(payload),
        hostname=MQTT_HOST,
        port=MQTT_PORT,
        qos=1,
        retain=False,
    )


def main():
    if len(sys.argv) < 3:
        print("Uso: avvia_fermentazione.py <profilo> <avvio_epoch> [isteresi] [allarme_timeout]")
        sys.exit(1)

    nome           = sys.argv[1]
    avvio_epoch    = int(float(sys.argv[2]))
    isteresi       = float(sys.argv[3]) if len(sys.argv) > 3 else 0.3
    allarme_timeout = int(sys.argv[4])  if len(sys.argv) > 4 else 5

    try:
        raw     = load_profile(nome)
        payload = convert_profile(raw, nome, avvio_epoch, isteresi, allarme_timeout)
        publish_mqtt(payload)
        print(f"OK: Profilo '{nome}' pubblicato su {MQTT_TOPIC}")
    except FileNotFoundError:
        print(f"ERRORE: Profilo '{nome}' non trovato in {PROFILI_DIR}/")
        sys.exit(2)
    except KeyError as e:
        print(f"ERRORE: Campo mancante nel JSON del profilo: {e}")
        sys.exit(3)
    except Exception as e:
        print(f"ERRORE: {e}")
        sys.exit(4)


if __name__ == "__main__":
    main()
