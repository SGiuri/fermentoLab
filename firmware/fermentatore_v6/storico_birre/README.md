# Storico birre — le schede

Una scheda JSON per ogni birra fatta, mostrata dalla dashboard **Storico birre** di Home Assistant
(`hass_spec/dashboards/storico_birre_dashboard.yaml`, pacchetto `hass_spec/packages/storico_birre.yaml`,
script `homeassistant/scripts_py/storico_birre.py`).

**Le copie vere sono nel Lab**, in `/config/storico_birre/<id>.json`: le note aggiunte dalla dashboard finiscono lì.
I file in questa cartella sono istantanee; per aggiornarle si ricopiano dal Lab.

## Formato

| Chiave | Contenuto |
|---|---|
| `id` | Uguale al nome del file senza `.json`: lettere, cifre, `_` e `-` |
| `nome`, `stato`, `stile`, `lievito`, `fermentatore`, `regolazione` | Testo libero |
| `inizio`, `fine` | ISO 8601 con fuso (`2026-10-02T17:38:00+02:00`). La finestra dei grafici va dall'una all'altra |
| `fine_prevista` | `true` finché la fine è solo prevista: se la si supera, i grafici arrivano a ora |
| `og`, `fg_ricetta` | Numeri o `null` |
| `profilo` | Il profilo usato per il piano: `nome` (file in `profili/`), `inizio` del piano, `fg_obiettivo`, `profilo` (step) ed `eventi`, come nei file dei profili |
| `serie` | Le curve misurate: `entity`, `nome`, `asse` (`temp` o `sg`), `colore`, `spessore`, `tratteggio`, `gradini`, `dal`, `al` (assente = fino a ora) |
| `letture` | Letture manuali della densità: `quando` (`YYYY-MM-DD HH:MM`, ora locale), `strumento`, `letta`, `temp`, `sg` (corretta), `og`, `nota` |
| `letture_live` | Facoltativo: entità con le letture registrate nella dashboard Cotta (es. `sensor.f1_cotta_densita`); finché c'è, la scheda mostra quelle |
| `date` | Date chiave: `quando`, `evento`. Sono anche le linee verticali dei grafici |
| `note` | Cosa è successo e cosa si è deciso: `quando`, `testo` (ed `aggiunta`, se scritta dalla dashboard) |

Le curve misurate non sono nella scheda: la dashboard le legge dalle statistiche orarie di Home Assistant, che non
vengono cancellate, solo tra `dal` e `al`. Una stessa entità può aver misurato birre diverse in periodi diversi.
Se un'entità viene rinominata o cancellata, la sua curva sparisce: prima di farlo, annotarlo nelle schede.

## Nuova birra

1. Copiare una scheda in `/config/storico_birre/` con un nuovo `id` (es. `2026-12_stout_us04`) e cambiare i dati.
2. Entro un'ora compare nel menu (o subito, lanciando l'evento `storico_birre_aggiornato`).

A fine birra: togliere `letture_live` dopo aver copiato le letture in `letture`, mettere la `fine` vera e
`fine_prevista: false`.
