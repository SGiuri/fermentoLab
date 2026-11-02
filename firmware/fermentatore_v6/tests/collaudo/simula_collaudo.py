"""Simulazione offline del collaudo di F1: motore reale, fermentatore e Home Assistant simulati.

Uso:  python simula_collaudo.py            (tutti gli scenari)
      python simula_collaudo.py completo   (uno scenario)

Il modello: acqua in un fermentatore con camicia a glicole (pompa comandata dal relè del freddo),
resistenza da 33 W, scambio con l'ambiente. Il "firmware" risponde ai comandi come la 6.3.0
(ack, comandi vecchi, duplicati, cambio di modo solo da fermo, riavvio) e regola con on/off e
isteresi con memoria. Home Assistant è simulato per stati, storico, allarme di scostamento
(istantanee ogni 30 min, 60 min di permanenza) ed email.
"""
import json
import pathlib
import random
import sys
import tempfile
from collections import defaultdict

QUI = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(QUI.parents[1] / "hass_spec" / "appdaemon" / "collaudo_f1"))
import collaudo_motore as cm  # noqa: E402

PASSO = 10.0


class Mondo:
    def __init__(self, T0=11.0, ambiente=19.0, glicole=-4.0, ua_amb=3.0, ua_gli=12.0, caldo_w=33.0, litri=25.0,
                 bug_stop_vecchio=False, blackout=None, p_reset_pompa=0.0):
        self.t = 1_790_000_000.0
        self.T = self.Ts = T0
        self.amb, self.gli, self.ua_amb, self.ua_gli, self.caldo_w = ambiente, glicole, ua_amb, ua_gli, caldo_w
        self.cap = litri * 4186.0
        self.bug_stop_vecchio = bug_stop_vecchio
        self.p_reset_pompa = p_reset_pompa  # probabilità di reset 'poweron' a ogni commutazione della pompa
        self.caso = random.Random(7)
        self.blackout = blackout  # (inizio relativo all'avvio del test 'corrente', durata s)
        self.stati = {}
        self.storia_dati = defaultdict(list)
        self.visti = set()
        self.ack_in_arrivo = []
        self.email = []
        self.motore = None
        # firmware
        self.fsm = "RUNNING"
        self.profilo = {"nome": "lager_generica", "steps": [{"temp": 11.0, "ramp": False, "ore": 2160}],
                        "secondi": 3600.0}
        self.modo, self.ist = "on_off_hyst_sym", 0.5
        self.par = {"kp_heat": 25.0, "ki_heat": 0.03, "kd_heat": 0.0, "kp_cool": 25.0, "ki_cool": 0.03, "kd_cool": 0.0}
        self.azzera_pid()
        self.caldo = self.freddo = False
        self.t_cambio = self.t - 1000
        self.offline_fino = 0.0
        self.boot_t = self.t - 30000
        self.reset = "poweron"
        # HA
        self.snap = []
        self.t_snap = self.t
        self.cond_da = None
        self.email_allarme_inviata = False
        for e, v in {"input_select.f1_v6_modalita": "Collaudo", "input_number.f1_v6_soglia_scostamento": "1.5",
                     "input_number.f1_v6_minuti_scostamento": "60.0", "input_number.f1_collaudo_temp_parcheggio": "12.0",
                     "sensor.f1_v6_firmware": "6.3.0"}.items():
            self.imposta(e, v)
        self.aggiorna_ha()

    # ------------------------------------------------------------ contesto per il motore
    def ora(self):
        return self.t

    def stato(self, e):
        return self.stati.get(e)

    def storia(self, e, t0, t1):
        return [p for p in self.storia_dati[e] if p[0] <= t1]

    def mqtt(self, topic, payload):
        if self.t < self.offline_fino:
            return
        self.comando(topic.rsplit("/", 1)[1], payload)

    def servizio(self, dominio, servizio, **d):
        if dominio in ("input_select", "input_text", "input_number") and servizio in ("select_option", "set_value"):
            self.imposta(d["entity_id"], str(d.get("option", d.get("value"))))
        if dominio == "notify":
            self.email.append((self.t, d.get("title")))
            self.motore.su_servizio("notify", servizio, d)

    def entita(self, e, stato, attributi):
        self.stati[e] = stato
        self.stati[e + "#attr"] = attributi

    def log(self, testo):
        pass

    # ------------------------------------------------------------ HA
    def imposta(self, e, v):
        if self.stati.get(e) != v:
            self.stati[e] = v
            self.storia_dati[e].append((self.t, v))

    def ack(self, rid, cmd, status, reason=None):
        self.ack_in_arrivo.append((self.t + 1.0, {"request_id": rid, "command": cmd, "cmd": cmd, "status": status,
                                                  "reason_code": reason}))

    # ------------------------------------------------------------ firmware simulato
    def setpoint(self):
        # Dalla 6.4.0 il firmware avanza il profilo con un contatore locale (secondi accumulati
        # mentre il controllo è davvero in corso), non più con un epoch assoluto: nessuna
        # dipendenza da NTP, nessun salto dopo un blackout o un riavvio. Vedi avanza_profilo() nel
        # firmware e docs/11_TIMING_PROFILO.md.
        ore = self.profilo["secondi"] / 3600
        t_start, prec = 0.0, self.profilo["steps"][0]["temp"]
        for s in self.profilo["steps"]:
            if t_start <= ore < t_start + s["ore"]:
                return prec + (ore - t_start) / s["ore"] * (s["temp"] - prec) if s["ramp"] else s["temp"]
            prec, t_start = s["temp"], t_start + s["ore"]
        return self.profilo["steps"][-1]["temp"]

    def comando(self, cmd, testo):
        try:
            p = json.loads(testo)
        except ValueError:
            self.ack("n/a", cmd, "error", "invalid_json")
            return
        rid, ts = p.get("request_id"), p.get("ts", 0)
        if ts and self.t - ts > 600 and not (cmd == "stop" and self.bug_stop_vecchio):
            self.ack(rid, cmd, "error", "stale_command")
            return
        if cmd != "stop" and (rid, ts) in self.visti:
            self.ack(rid, cmd, "ok", "duplicate_ignored")
            return
        self.visti.add((rid, ts))
        if cmd == "profile_set":
            steps = []
            for s in p["steps"]:
                if not 0 <= s["temp"] <= 35:
                    self.ack(rid, cmd, "error", "step_temp_out_of_range")
                    return
                ramp = "ramp_hours" in s
                steps.append({"temp": s["temp"], "ramp": ramp, "ore": s["ramp_hours"] if ramp else s["duration_hours"]})
            self.profilo = {"nome": p["profilo"], "steps": steps, "secondi": 0.0}
            self.fsm = "RUNNING"
            self.azzera_pid()
            self.ack(rid, cmd, "ok", "profile_applied")
        elif cmd == "control_set":
            par = p.get("params", {})
            if not 0.1 <= par.get("isteresi", 0.5) <= 3.0:
                self.ack(rid, cmd, "error", "invalid_isteresi")
                return
            if any(par.get(k, 0) < 0 for k in ("kp_heat", "ki_heat", "kd_heat", "kp_cool", "ki_cool", "kd_cool")):
                self.ack(rid, cmd, "error", "invalid_pid_params")
                return
            if p["control_mode"] != self.modo and self.fsm != "STOPPED":
                self.ack(rid, cmd, "error", "mode_change_requires_stopped_state")
                return
            stesso = p["control_mode"] == self.modo
            self.modo, self.ist = p["control_mode"], par.get("isteresi", self.ist)
            self.par.update({k: v for k, v in par.items() if k in self.par})
            if not stesso:
                self.azzera_pid()
            self.ack(rid, cmd, "ok", "mode_already_active" if stesso else None)
        elif cmd == "stop":
            self.fsm = "STOPPED"
            self.caldo = self.freddo = False
            self.ack(rid, cmd, "ok")
        elif cmd == "start":
            self.fsm = "RUNNING"
            self.azzera_pid()
            self.ack(rid, cmd, "ok")
        elif cmd == "reboot":
            self.ack(rid, cmd, "ok")
            self.riavvia(15, "software")

    def azzera_pid(self):
        self.integrale = self.df = 0.0
        self.T_prec = None
        self.pid = {"errore": 0.0, "p": 0.0, "i": 0.0, "d": 0.0, "u": 0.0, "duty_f": 0.0, "duty_c": 0.0}
        self.t_finestra = self.t

    def riavvia(self, secondi_offline, causa):
        self.caldo = self.freddo = False
        self.offline_fino = self.t + secondi_offline
        self.boot_t = self.t + secondi_offline - 8
        self.reset = causa

    def regola(self):
        if self.t < self.offline_fino and self.reset in ("poweron",) and self.blackout_attivo():
            self.caldo = self.freddo = False
            return
        if self.fsm != "RUNNING":
            self.caldo = self.freddo = False
            return
        self.profilo["secondi"] = self.profilo.get("secondi", 0.0) + PASSO
        sp, h, T = self.setpoint(), self.ist, round(self.Ts / 0.0625) * 0.0625
        caldo, freddo = self.caldo, self.freddo
        self.pid.update(errore=sp - T, p=0.0, i=0.0, d=0.0)
        if self.modo == "on_off":
            caldo, freddo = T < sp, T > sp
        elif self.modo in ("P", "PI", "PID"):
            caldo, freddo = self.regola_pid(sp, T)
        if self.modo == "on_off_hyst_sym":
            if caldo and T >= sp:
                caldo = False
            elif not caldo and T <= sp - h:
                caldo = True
            if freddo and T <= sp:
                freddo = False
            elif not freddo and T >= sp + h:
                freddo = True
        if self.modo in ("on_off", "on_off_hyst_sym"):
            u = 100.0 if caldo else (-100.0 if freddo else 0.0)
            self.pid.update(u=u, duty_c=100.0 if caldo else 0.0, duty_f=100.0 if freddo else 0.0)
        dt = self.t - self.t_cambio
        for nome, nuovo in (("caldo", caldo), ("freddo", freddo)):
            vecchio = getattr(self, nome)
            if nuovo != vecchio and ((vecchio and dt >= 30) or (not vecchio and dt >= 60)):
                setattr(self, nome, nuovo)
                self.t_cambio = self.t
                if nome == "freddo" and self.caso.random() < self.p_reset_pompa:
                    self.aggiorna_ha()  # HA vede la commutazione, poi l'ESP32 riparte
                    self.riavvia(10, "poweron")
                    return

    def regola_pid(self, sp, T):
        e = sp - T
        lato = "heat" if e >= 0 else "cool"
        kp, ki, kd = self.par["kp_" + lato], self.par["ki_" + lato], self.par["kd_" + lato]
        if self.T_prec is not None:
            alfa = 2.718281828 ** (-PASSO / 300.0)
            self.df = alfa * self.df + (1 - alfa) * (T - self.T_prec) / PASSO
        self.T_prec = T
        p = kp * e
        d = -kd * self.df if self.modo == "PID" else 0.0
        if self.modo in ("PI", "PID"):
            prima = p + ki * self.integrale + d
            if not ((prima >= 100 and e > 0) or (prima <= -100 and e < 0)):
                self.integrale += e * PASSO
            if ki > 0:
                lim = 50.0 / ki
                self.integrale = max(-lim, min(lim, self.integrale))
            i = ki * self.integrale
        else:
            self.integrale, i = 0.0, 0.0
        u = max(-100.0, min(100.0, p + i + d))
        duty_c = duty_f = 0.0
        if abs(u) > 2.5:
            if u > 0:
                duty_c = max(abs(u), 5.0)
            else:
                duty_f = max(abs(u), 5.0)
        self.pid.update(errore=e, p=p, i=i, d=d, u=u, duty_c=duty_c, duty_f=duty_f)
        if self.t - self.t_finestra >= 600:
            self.t_finestra = self.t
        on_c, on_f = duty_c * 6.0, duty_f * 6.0   # secondi accesi nella finestra da 600 s
        nella = self.t - self.t_finestra
        return (on_c >= 30 and nella < on_c), (on_f >= 30 and nella < on_f)

    def blackout_attivo(self):
        return getattr(self, "_blackout_fino", 0) > self.t

    # ------------------------------------------------------------ passo di simulazione
    def passo(self):
        self.t += PASSO
        # blackout comandato dallo scenario
        if self.blackout and self.motore.s.get("corrente") == "corrente" and not getattr(self, "_blackout_fatto", False):
            inizio, durata = self.blackout
            if self.motore.da() >= inizio and self.motore.s["fase"] == "attesa_spegnimento":
                self._blackout_fino = self.t + durata
                self.riavvia(durata + 20, "poweron")
                self._blackout_fatto = True
        q = self.ua_amb * (self.amb - self.T)
        if self.caldo:
            q += self.caldo_w
        if self.freddo and self.T > self.gli:
            q -= self.ua_gli * (self.T - self.gli)
        self.T += q * PASSO / self.cap
        self.Ts += (self.T - self.Ts) * min(PASSO / 60.0, 1.0)
        self.regola()
        for i, (quando, a) in list(enumerate(self.ack_in_arrivo)):
            if quando <= self.t:
                self.motore.su_ack(a)
        self.ack_in_arrivo = [x for x in self.ack_in_arrivo if x[0] > self.t]
        self.aggiorna_ha()

    def aggiorna_ha(self):
        online = self.t >= self.offline_fino
        self.imposta("binary_sensor.f1_v6_online", "on" if online else "off")
        self.imposta("binary_sensor.f1_v6_dati_in_arrivo", "on" if online else "off")
        if not online:
            for e in ("sensor.f1_v6_temperatura", "sensor.f1_v6_setpoint", "sensor.f1_v6_uptime"):
                self.imposta(e, "unavailable")
            return
        q = round(round(self.Ts / 0.0625) * 0.0625, 2)
        sp = round(self.setpoint(), 2) if self.fsm != "STOPPED" else round(self.setpoint(), 2)
        self.imposta("sensor.f1_v6_temperatura", str(q))
        self.imposta("sensor.f1_v6_setpoint", str(sp))
        self.imposta("sensor.f1_v6_fsm", self.fsm)
        self.imposta("sensor.f1_v6_profilo", self.profilo["nome"])
        self.imposta("sensor.f1_v6_step_index", "0")
        self.imposta("sensor.f1_v6_ore_totali", f"{self.profilo['secondi'] / 3600:.1f}")
        self.imposta("sensor.f1_v6_modo_controllo_attivo", self.modo)
        self.imposta("sensor.f1_v6_isteresi_attiva", f"{self.ist}")
        self.imposta("binary_sensor.f1_v6_riscaldatore", "on" if self.caldo else "off")
        self.imposta("binary_sensor.f1_v6_refrigeratore", "on" if self.freddo else "off")
        self.imposta("binary_sensor.f1_v6_controllo_attivo", "on" if self.fsm == "RUNNING" else "off")
        attivo = self.fsm == "RUNNING"
        for chiave, ent, dec in (("errore", "errore", 3), ("p", "termine_p", 2), ("i", "termine_i", 2),
                                 ("d", "termine_d", 3), ("u", "uscita_controllore", 2)):
            self.imposta(f"sensor.f1_v6_{ent}", str(round(self.pid[chiave], dec) if attivo else 0.0))
        self.imposta("sensor.f1_v6_duty_refrigeratore", str(round(self.pid["duty_f"], 1) if attivo else 0.0))
        self.imposta("sensor.f1_v6_duty_riscaldatore", str(round(self.pid["duty_c"], 1) if attivo else 0.0))
        for g in ("kp", "ki", "kd"):
            self.imposta(f"sensor.f1_v6_{g}_freddo_attivo", str(self.par[g + "_cool"]))
            self.imposta(f"sensor.f1_v6_{g}_caldo_attivo", str(self.par[g + "_heat"]))
        self.imposta("binary_sensor.f1_v6_sensore_guasto", "off")
        self.imposta("sensor.f1_v6_causa_ultimo_reset", self.reset)
        self.imposta("sensor.f1_v6_uptime", str(int(self.t - self.boot_t)))
        # allarme di scostamento come nel pacchetto HA
        dev = abs(q - sp)
        if self.t - self.t_snap >= 1800:
            self.snap.append(dev)
            self.t_snap = self.t
        prima = self.snap[-2] if len(self.snap) >= 2 else None
        avvicina = prima is None or dev < prima - 0.1
        cond = self.fsm in ("RUNNING", "HOLDING") and dev > 1.5 and not avvicina
        self.imposta("binary_sensor.f1_v6_allarme_temperatura", "on" if cond else "off")
        if cond:
            self.cond_da = self.cond_da or self.t
            if self.t - self.cond_da >= 3600 and not self.email_allarme_inviata:
                self.email_allarme_inviata = True
                self.servizio("notify", "fermentazione_email", title="[ALLARME] F1 V6 - Temperatura fuori range")
        else:
            self.cond_da = None
            self.email_allarme_inviata = False
        if not online and self.stati.get("binary_sensor.f1_v6_online") == "off":
            pass


def esegui(nome, test, ore_max=100, riavvio_motore_a=None, avvio=None, **kw):
    cartella = tempfile.mkdtemp()
    percorso = str(pathlib.Path(cartella) / "stato.json")
    w = Mondo(**kw)
    w.motore = cm.Motore(w, percorso)
    # email 'offline' come l'automazione HA (3 minuti)
    w.motore.comando("avvia", dict(avvio or {}, test=test))
    t0 = w.t
    ultimo_tick = 0.0
    offline_da = None
    while w.t - t0 < ore_max * 3600:
        w.passo()
        if w.stati.get("binary_sensor.f1_v6_online") == "off":
            offline_da = offline_da or w.t
            if w.t - offline_da >= 180 and not getattr(w, "_email_offline", False):
                w._email_offline = True
                w.servizio("notify", "fermentazione_email", title="[ALLARME] F1 V6 - ESP32 offline")
        else:
            offline_da, w._email_offline = None, False
        if w.t - ultimo_tick >= 30:
            ultimo_tick = w.t
            w.motore.tick()
        if riavvio_motore_a and w.t - t0 >= riavvio_motore_a * 3600:
            w.motore = cm.Motore(w, percorso)  # come un riavvio di AppDaemon
            riavvio_motore_a = None
        if w.motore.s["stato"] in ("terminato", "interrotto"):
            break
    s = w.motore.s
    print(f"\n=== {nome}: sessione {s['stato']} ({s['motivo']}) in {(w.t - t0) / 3600:.1f} h, "
          f"verdetto: {w.motore.verdetto()['testo']}")
    for tid in test:
        r = s["risultati"].get(tid, {})
        durata = (r.get("fine", 0) - r.get("inizio", 0)) / 3600 if r.get("inizio") else 0
        print(f"  {tid:15s} {r.get('esito', '?'):15s} {durata:5.1f} h  {r.get('note') or ''}")
        for c in r.get("controlli", []):
            if not c["ok"]:
                print(f"      NO  {c['voce']}: {c['valore']} (limite {c['limite']})")
        if r.get("misure"):
            print("      " + "; ".join(f"{k}: {v}" for k, v in r["misure"].items()))
    print(f"  email: {[e[1] for e in w.email]}")
    print(f"  finale: T {w.T:.2f} °C, setpoint {w.setpoint():.2f}, modo {w.modo}, isteresi {w.ist}, "
          f"modalità {w.stati['input_select.f1_v6_modalita']}")
    return w


MODI = ["modo_on_off", "modo_p", "modo_pi", "modo_pid"]
FASE_A = ["comandi", "lager", "riavvio", "stress_pompa", "diacetile", "rampa"] + MODI + ["cold_crash", "allarme_freddo"]

def controlla_ordine():
    """L'ordine di esecuzione non dipende da quello di scelta: segue ORDINE_ESECUZIONE, poi il catalogo."""
    assert set(FASE_A) == set(cm.ORDINE_ESECUZIONE), "ORDINE_ESECUZIONE e Fase A devono elencare gli stessi test"
    altri = [t.id for t in cm.CATALOGO if t.id not in cm.ORDINE_ESECUZIONE]
    ottenuto = cm.ordina_per_esecuzione(list(reversed(FASE_A + altri)))
    assert ottenuto == cm.ORDINE_ESECUZIONE + altri, ottenuto
    print("\n=== Ordine di esecuzione: " + " > ".join(ottenuto[:len(cm.ORDINE_ESECUZIONE)]) + f"  (+ {len(altri)} test dei gruppi B e C in coda)")


SCENARI = {
    "ordine": controlla_ordine,
    "completo": lambda: esegui("Fase A completa + blackout", FASE_A + ["corrente"], blackout=(600, 400)),
    "riavvio_motore": lambda: esegui("Riavvio di AppDaemon a metà", ["lager", "diacetile"], riavvio_motore_a=5.0),
    "freddo_debole": lambda: esegui("Chiller debole", ["cold_crash", "allarme_freddo"], ua_gli=4.0),
    "bug_firmware": lambda: esegui("Firmware che esegue uno STOP vecchio", ["comandi"], bug_stop_vecchio=True),
    "modi": lambda: esegui("Modi di controllo in freddo a 10 °C", MODI, T0=10.0),
    "pompa_reset": lambda: esegui("Reset dell'ESP32 al 10 % delle commutazioni della pompa",
                                  ["lager", "stress_pompa"], p_reset_pompa=0.1),
    "stress_50": lambda: esegui("stress_pompa con 50 cicli (cicli_stress)", ["stress_pompa"],
                                avvio={"cicli_stress": "50"}, p_reset_pompa=0.05),
    "solo_b": lambda: esegui("Solo test della 6.5.0 con firmware 6.3.0", ["stress_rele", "wifi"]),
}

if __name__ == "__main__":
    scelti = sys.argv[1:] or list(SCENARI)
    for n in scelti:
        SCENARI[n]()
