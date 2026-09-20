# Modalità di accesso al Home Assistant Lab

Scritto il 2026-09-20 per agenti AI (Claude Code o altri) che devono lavorare sul Lab di FermentaLab,
e come istruzioni per collegarsi in modo analogo a un altro Home Assistant. Tutto quello che è marcato
**[verificato]** è stato eseguito davvero il 2026-09-20 alle 08:38 CEST; il resto è indicato come
non verificato.

**Nessun segreto è scritto qui.** Token e chiavi vivono fuori dal repo (vedi §6). Anche gli indirizzi
sono segnaposto (`<LAB_IP>`, `<IP_ISTANZA_PERSONALE>`, `<CHIAVE_SSH_LAB>`, `<CHIAVE_SSH_PERSONALE>`): i
valori reali del proprietario stanno nel file locale `MODALITA_ACCESSO_LAB.local.md`, che non è versionato
(vedi `.gitignore`). Se non lo trovi, chiedi al proprietario i valori prima di collegarti.

---

## 1. Regole non negoziabili

1. **Due istanze, una sola è usabile.**
   - **Lab (usabile):** `http://<LAB_IP>:8123` — server MCP `homeassistant_lab`.
   - **Personale (vietata):** `http://<IP_ISTANZA_PERSONALE>:8123` — server MCP `homeassistant` (tool `mcp__homeassistant__*`).
   Non chiamare mai i suoi tool e non mandarle comandi, salvo che il proprietario lo dica esplicitamente
   in quel momento. Prima di ogni chiamata HA controlla l'URL.
2. **Il broker MQTT del Lab è lo stesso del hardware reale (F1, HiveMQ Cloud).** Un comando pubblicato da
   HA su `fermentazione/v6/f1/ha/cmd/*` arriva al dispositivo fisico. Per i test al banco va bene. Non
   abilitare mai MQTT discovery su quel broker: potrebbe finire nell'istanza personale.
3. **Backup prima di modificare** configurazione, package, dashboard o registro entità (§4).
4. **Il token è admin/owner.** Non stamparlo, non scriverlo in file, log, memoria o commit.
5. **Conferma prima di azioni difficili da annullare** (riavvii del core, cancellazioni, comandi al
   dispositivo, flash del firmware).

---

## 2. I tre canali di accesso al Lab

| Canale | Cosa fa | Limiti |
|---|---|---|
| **A. MCP** (`mcp__homeassistant_lab__*`) | Leggere stato live e comandare entità esposte ad Assist | Vede solo le entità *esposte ad Assist*: F1 e collaudo non lo sono. Nessun accesso a file, add-on, backup |
| **B. REST API** (token Bearer) | Leggere tutti gli stati e la storia, chiamare qualsiasi servizio, validare la configurazione | Gli endpoint Supervisor rispondono 401. Nessun accesso ai file |
| **C. SSH/SFTP** (`root@<LAB_IP>`) | File di `/config`, add-on AppDaemon, comando `ha` (core, add-on, backup, log) | Solo dal PC autorizzato, con la chiave dedicata |

Regola pratica: **B per leggere e comandare, C per scrivere file e gestire backup/add-on, A solo per le
entità esposte ad Assist.** Per F1 e collaudo l'MCP non basta.

### A. MCP

- Configurato in `~/.claude.json`, chiave `mcpServers.homeassistant_lab` **per ogni percorso di progetto**:
  `{"type":"http","url":"http://<LAB_IP>:8123/api/mcp","headers":{"Authorization":"Bearer <TOKEN>"}}`.
- **Attenzione ai rinominamenti di cartella:** la configurazione è legata al percorso del progetto. Esistono
  voci per `…/TermostatoFermentatore_V2/esp32_firmware`, `G:/…/FermentaLab` e `g:/…/FermentaLab` (maiuscola
  e minuscola contano). Dopo uno spostamento, se i tool non compaiono, aggiungi la voce per il nuovo percorso.
- I tool sono *deferred*: prima carica lo schema con `ToolSearch` (`select:mcp__homeassistant_lab__homeassistant__GetLiveContext,…`).
- Tool disponibili: `GetLiveContext` (stato, filtri `name`/`domain`/`area`), `llm__GetDateTime`,
  `intent__HassTurnOn/HassTurnOff`, `climate__HassClimateSetTemperature`, `todo__*`, `assist_satellite__HassBroadcast`.
- **[verificato]** `GetDateTime` risponde; `GetLiveContext` con `name="f1"` risponde "No exposed entities matched".

### B. REST API

- Base: `http://<LAB_IP>:8123`, header `Authorization: Bearer <TOKEN>`.
- Il token è già nella configurazione MCP. Leggilo in uno script **senza mai stamparlo**:

```python
import json, os, re, urllib.request
cfg = json.load(open(os.path.expanduser('~/.claude.json'), encoding='utf-8'))
def find(o):
    if isinstance(o, dict):
        for k, v in o.items():
            if k == 'homeassistant_lab': yield v
            yield from find(v)
    elif isinstance(o, list):
        for i in o: yield from find(i)
tok = re.search(r'[\w\-]{5,}\.[\w\-]{5,}\.[\w\-]{5,}', json.dumps(next(find(cfg)))).group(0)
BASE = 'http://<LAB_IP>:8123'   # controlla che sia il Lab, mai <IP_ISTANZA_PERSONALE>
def api(path, method='GET', body=None):
    req = urllib.request.Request(BASE + path, method=method,
        data=json.dumps(body).encode() if body is not None else None,
        headers={'Authorization': 'Bearer ' + tok, 'Content-Type': 'application/json'})
    return json.load(urllib.request.urlopen(req, timeout=30))
```

- Endpoint: `GET /api/config`, `GET /api/states`, `GET /api/history/period/<ISO>?filter_entity_id=…`,
  `POST /api/services/<dominio>/<servizio>` (comandi), `POST /api/config/core/check_config` (validazione).
- **[verificato]** `/api/config` → HA 2026.9.2 RUNNING; `/api/states` → 370 entità;
  `check_config` → `valid`, senza errori né warning.
- **Windows:** lancia Python con `PYTHONIOENCODING=utf-8`. Senza, `print` dei nomi con `°` o `→` va in
  `UnicodeEncodeError` (cp1252) e la lettura si interrompe a metà.

### C. SSH / SFTP

- `ssh -i ~/.ssh/<CHIAVE_SSH_LAB> root@<LAB_IP>`, servito dall'add-on *Advanced SSH & Web Terminal*
  (slug `a0d7b954_ssh`, SFTP attivo). **Usa solo `<CHIAVE_SSH_LAB>`. Non usare `<CHIAVE_SSH_PERSONALE>`.**
- Schema di comando che funziona da script:

```bash
ssh -i ~/.ssh/<CHIAVE_SSH_LAB> -o BatchMode=yes -o ConnectTimeout=8 root@<LAB_IP> '<comandi>' < /dev/null
```

  `< /dev/null` è obbligatorio: con stdin chiuso o lasciato aperto, gli output lunghi fanno bloccare la
  sessione. Per verificare un file letto o scritto usa `sha256sum` e confronta gli hash, non riversare il
  contenuto a schermo.
- Percorsi utili sul Lab:
  - `/config/packages/` — package YAML (`fermentatore_v6_f1_{entita,allarmi,laboratorio,collaudo}.yaml`, `fermentatore_v6_f2_allarmi.yaml`)
  - `/config/mqtt.yaml`, `/config/configuration.yaml`, `/config/scripts_py/`
  - `/addon_configs/a0d7b954_appdaemon/apps/collaudo_f1/` — motore di collaudo (`collaudo_app.py`,
    `collaudo_motore.py`) e stato `collaudo_stato.json`. Caricare il `.py` fa ricaricare AppDaemon, e il
    motore riprende dal JSON.
- Comandi `ha` utili: `ha core info`, `ha core check`, `ha addons`, `ha addons logs a0d7b954_appdaemon`,
  `ha backups list`, `ha backups new --name "<nome>"`, `ha supervisor info`.
- **[verificato]** SSH ok; creazione e cancellazione di un file di prova in `/config` (scrittura ok);
  `ha addons`, `ha backups list`, `ha addons logs`, `ha core check`, `ha supervisor info` tutti ok
  (Supervisor healthy).
- **Non verificato oggi:** `scp`/`sftp` per caricare file (i caricamenti dei giorni scorsi sono stati fatti via
  SFTP, ma non ho ripetuto il test), `ha backups new`, riavvii, `reload_all`.

### Flusso di modifica consigliato

1. `ha backups new --name "prima di <cosa> (AAAA-MM-GG)"` e annota lo slug.
2. Carica il file (SFTP o `ssh … 'cat > percorso' < file`) e verifica con `sha256sum`.
3. `POST /api/config/core/check_config` → deve dire `valid`.
4. Ricarica solo quello che serve (`automation.reload`, `script.reload`, `homeassistant.reload_all`) o, se
   indispensabile, riavvia il core.
5. Rileggi lo stato dalle entità (REST) e i log (`ha addons logs`, `/api/error_log`).

---

## 3. Da dove nascono i file (repo)

Dal 2026-09-20 la radice `FermentaLab/` è il repository git e contiene `firmware/`, `homeassistant/`,
`hardware/` e questo file.

| Cosa | Dove |
|---|---|
| Package e dashboard HA | `firmware/fermentatore_v6/hass_spec/packages/`, `…/dashboards/` |
| Motore di collaudo | `firmware/fermentatore_v6/hass_spec/appdaemon/collaudo_f1/` |
| Documentazione V6 | `firmware/fermentatore_v6/docs/00_INDICE.md` (collaudo: `06_COLLAUDO_F1.md`) |
| Configurazione HA del Lab (`configuration.yaml`, `scripts.yaml`, `mqtt.yaml`…) e script di avvio | `homeassistant/`, `homeassistant/scripts_py/avvia_fermentazione_v6.py` |
| Catalogo profili di fermentazione | `firmware/fermentatore_v6/profili/` |
| Vecchia configurazione V3 (storica) | `homeassistant/legacy_v3/` |

**Attenzione:** `CLAUDE.md` nella radice cita ancora i percorsi `fermentatore_v6/…` e `obsolete/…`, che ora
stanno sotto `firmware/`. Fidati del filesystem, non di quei percorsi.

---

## 4. Stato del Lab al 2026-09-20 08:38 CEST [verificato]

- HA core 2026.9.2, Supervisor 2026.09.2 healthy, `check_config` valido.
- Add-on rilevanti avviati: AppDaemon (app `collaudo_f1`), Advanced SSH & Web Terminal.
- Backup rilevanti: `c67e2dfe` (prima di F1 Laboratorio), `b46d74dd` (prima della migrazione F1),
  `c5a39a1c` (prima degli allarmi), più uno automatico del 2026-09-20 03:32.
- F1: online, firmware 6.3.0, RSSI −61, `sensor.f1_v6_riavvii_spontanei` = 5, uptime 11902 s
  (ha riavviato alle ~03:20 UTC). `input_select.f1_v6_modalita` = "Collaudo".
- Collaudo in corso. Superati: comandi, lager, diacetile, riavvio. **Falliti: stress pompa, rampa, On/Off.**
  In coda: PI, PID, cold crash, allarme freddo. Verdetto: "non pronto".
- **Fusi orari:** i log di AppDaemon sono in ora locale (CEST); stati e storico di HA sono in UTC.

---

## 5. Bug riscontrato: F1 si riavvia da solo

Il collaudo di F1 (firmware 6.3.0, banco, serbatoio d'acqua) ha registrato riavvii spontanei dell'ESP32.
I sintomi sono **due fenomeni con cause diverse**, non uno solo.

**Fenomeno 1 — reset dopo la commutazione della pompa (2026-09-18).** Causa riportata `poweron`, offline
meno di 15 s, 0,7–4 s dopo una transizione del relè della pompa di raffreddamento: 18/09 11:25, 23:50, 23:59
(3 riavvii in 26 cicli della pompa). Sospetto: disturbo elettrico (extratensione induttiva) sull'alimentazione o
sul pin EN. È già in `docs/06_COLLAUDO_F1.md §7`.

**Fenomeno 2 — reset con causa `wdt` (2026-09-20, nuovo).** Dal log di AppDaemon (ora locale CEST):

| Ora | Test in corso | Esito |
|---|---|---|
| 00:21:59 | rampa 17→10 °C | RIAVVIO SPONTANEO, causa `wdt` |
| 01:41:00 | rampa 17→10 °C | RIAVVIO SPONTANEO, causa `wdt` |
| 05:20:29 | On/Off senza isteresi | RIAVVIO SPONTANEO, causa `wdt` |

- I test **rampa** e **On/Off senza isteresi** sono finiti "fallito" con motivi *"Riavvii spontanei
  dell'ESP32, Sensore non utilizzabile"*. Anche **stress pompa** è "fallito" (19/09 06:09 UTC), ma la
  causa non è stata verificata oggi.
- Nel firmware la stringa `wdt` corrisponde a `ESP_RST_WDT` (`reset_reason_str()` in `fermentatore_v6.ino:210`),
  cioè un watchdog **diverso** da `task_wdt` e `int_wdt`, che hanno stringhe loro.
- Il sensore `sensor.f1_v6_riavvii_spontanei` vale 5 al 2026-09-20 06:38 UTC.

**Cosa NON è dimostrato:** che i due fenomeni abbiano la stessa origine, e che il `wdt` dipenda dal
software. Un reset `wdt` può nascere da un blocco del programma ma anche da un'alimentazione instabile.
Il legame con "Sensore non utilizzabile" fa sospettare il ramo del sensore (`sensor_watchdog`, lettura
DS18B20, power-cycle su `PIN_SENSOR_PWR`), ma è un'ipotesi.

**Da fare, in ordine:**
1. Cercare nei log del firmware (`esp/log`, storico HA) cosa succedeva nei secondi prima di 00:21, 01:41 e 05:20.
2. Verificare se i reset `wdt` coincidono con un recupero del sensore o con una commutazione dei relè.
3. Misurare l'alimentazione (tensione e picchi su 3V3/EN) durante una commutazione, prima di attribuire il
   problema al software.
4. Non rilanciare il collaudo completo finché la causa non è chiara: i test falliti per riavvio non
   dicono nulla sul controllo.

Scadenza: la birra entra il 2026-10-03.

---

## 6. Come collegarsi a un altro Home Assistant

Vale per qualsiasi altra istanza. **Prima chiedi al proprietario quale istanza è, se è consentito toccarla
e se è in sola lettura o anche in scrittura.** Non dedurlo dall'IP.

### 6.1 Server MCP (stato live e comandi)
1. In HA: *Impostazioni → Dispositivi e servizi → Aggiungi integrazione → "Model Context Protocol Server"*.
2. *Impostazioni → Assistenti vocali → Esponi*: seleziona solo le entità che l'agente deve vedere.
3. *Profilo → Sicurezza → Token di accesso di lunga durata → Crea*. Copialo subito, non si rivede.
   Se possibile usa un **utente dedicato non amministratore** invece di quello del proprietario.
4. Registra il server, **con un nome che dica di quale istanza si tratta**:
   `claude mcp add --transport http <nome_istanza> http://<IP>:8123/api/mcp --header "Authorization: Bearer <TOKEN>" --scope project`
   (`--scope user` lo rende globale: sconsigliato per istanze sensibili).
5. Aggiungi subito in memoria una regola "quale server è quale", come questa: Lab sì, personale no.

### 6.2 REST API
Stesso token del punto 6.1. Prova con `GET /api/config` (versione e stato) e `POST /api/config/core/check_config`.
Con un utente non admin alcuni endpoint daranno 401 o 403: è voluto.

### 6.3 SSH/SFTP (solo se servono file, backup o add-on)
1. Installa *Advanced SSH & Web Terminal* dallo store degli add-on.
2. Sul PC: `ssh-keygen -t ed25519 -f ~/.ssh/id_ed25519_<nome_istanza>`; incolla la chiave **pubblica** in
   `authorized_keys` nella configurazione dell'add-on, attiva SFTP e avvia l'add-on.
3. Usa una chiave dedicata per istanza, non riusare quelle esistenti.
4. L'istanza è raggiungibile dal PC solo se sono sulla stessa rete o su Tailscale: il Lab si raggiunge
   con l'IP Tailscale `100.x.x.x`.

### 6.4 Test di ingresso (esegui tutto prima di fidarti del collegamento)
1. `GET /api/config` → versione e `RUNNING`; confronta l'URL con quello che il proprietario ha autorizzato.
2. `GET /api/states` → conta le entità.
3. `POST /api/config/core/check_config` → `valid`.
4. Via SSH: `echo ok`, `ha core info`, `ha supervisor info` (deve dire `healthy: true`), `ha backups list`.
5. Scrittura: crea e cancella un file di prova in `/config`, senza toccare altro.
6. **Crea un backup** con `ha backups new` prima della prima modifica vera e annotane lo slug.

### 6.5 Da riportare nella memoria dell'agente
URL e ruolo dell'istanza (lab, personale, produzione), quali canali funzionano, quale chiave SSH e quale
server MCP usare, gli slug dei backup fatti, le entità che comandano hardware reale.
Non salvare mai il token o le chiavi.

---

## 7. Trappole già incontrate

- **Buffer VS Code non aggiornati** hanno già sovrascritto modifiche fatte da fuori editor. Prima di
  modificare un file che potrebbe essere aperto, avvisa; prima di flashare verifica versione e `git status`.
- `config.h` è UTF-8 con BOM e CRLF: conserva entrambi.
- L'entità dello script di avvio è `script.f1_v6_avvia_unificato` ma il suo nome di servizio è diverso
  (`f1_v6_avvia_unified`): non "correggerlo".
- L'OTA verso F1 funziona solo dall'hotspot del telefono (HotIoT): la rete aziendale lo blocca, e l'ESP
  sceglie l'hotspot solo all'avvio.
- Nessun file di questo repo deve contenere token, password o chiavi private (`secrets.h` è ignorato da git).
