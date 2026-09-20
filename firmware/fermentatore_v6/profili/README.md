# Catalogo dei profili di fermentazione

Copia versionata, presa il 2026-09-19, di `/config/profili_fermentazione/*.json` sul Lab
(`homeassistant_lab`). Sono i JSON che [`homeassistant/scripts_py/avvia_fermentazione_v6.py`](../../../homeassistant/scripts_py/avvia_fermentazione_v6.py)
(copia dello script che gira sul Lab) legge, converte (`duration_days`/`ramp_days` → `_hours`) e invia
al firmware con `ha/cmd/profile_set`. Vedi [`../docs/11_TIMING_PROFILO.md`](../docs/11_TIMING_PROFILO.md)
per come il firmware esegue questi profili e per i rischi di temporizzazione, e
[`../docs/10_PUNTI_APERTI.md`](../docs/10_PUNTI_APERTI.md) per il contesto di questo backup.

**Perché sono qui**: prima di questa copia esistevano solo sul disco del Lab, senza backup né
storico — un rischio indipendente dal firmware (se quella cartella si perde o viene toccata per
errore, le ricette numeriche delle birre vanno perse senza preavviso).

## File

| File | Step | Durata totale | Uso |
|---|---:|---:|---|
| `lager_generica.json` | 5 | 48 giorni | Lager generica: primaria 11 °C, rampa e sosta diacetile a 16 °C, discesa e lagerizzazione a 2 °C |
| `ale_generica.json` | 6 | 45 giorni | Ale generica: primaria 19 °C, diacetyl rest 21 °C, cold crash 4 °C, attesa imbottigliamento |
| `czech_pils_W34.json` | 6 | 76 giorni | Pils ceca (lievito W34/70): primaria 10 °C, diacetyl rest 16 °C, lagerizzazione a 1 °C |
| `ipa_dryhop.json` | 6 | 45 giorni | IPA con dry hop a 21 °C invece del diacetyl rest, poi cold crash |
| `stout_US04.json` | 6 | 44 giorni | Stout (US-04): primaria 20 °C, diacetyl rest 21 °C, cold crash 4 °C |
| `test_costante.json` | 1 | 2 giorni | Temperatura costante 14 °C — verifica rapida senza rampe |
| `test_rampa_v.json` | 4 | 7 giorni | Rampa su/giù 20→10→25 °C — verifica del calcolo della rampa |
| `test_wave_10_20m.json` | 9 | ~3 ore | Onda veloce 12-16 °C, tenute ~20 min e rampe ~10 min: utile per collaudi che devono esercitare una rampa in minuti invece che in giorni (vedi `docs/11_TIMING_PROFILO.md` §6) |

## Come restano sincronizzati col Lab

Questa è una copia, non la fonte che il Lab usa in produzione: lo script del Lab continua a leggere
da `/config/profili_fermentazione/`. Se una ricetta cambia sul Lab (o qui), copiare manualmente nella
direzione giusta e verificare con `sha256sum`. Non c'è oggi un sync automatico — è un miglioramento
futuro possibile, non necessario per il backup/versionamento che questa cartella risolve.

## Formato

Ogni file ha una chiave `"profilo"`: una lista di step, ciascuno con `"step"` (nome), `"temp"`
(°C) e **uno fra** `"duration_days"` (tenuta costante) o `"ramp_days"` (rampa lineare verso `temp`
a partire dalla temperatura dell'ultimo step). Lo script del Lab converte i giorni in ore
(`duration_hours`/`ramp_hours`) prima di inviarli: il firmware accetta solo le ore, vedi
[`../docs/04_CONTRATTO_HASS.md`](../docs/04_CONTRATTO_HASS.md) §3.4 (`ha/cmd/profile_set`).
