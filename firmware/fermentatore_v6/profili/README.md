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
| `cerveza_especial.json` | 5 | 33 giorni | Lager chiara tipo *cerveza especial* spagnola (aggiunto il 2026-10-03 per la cotta di ottobre in F1): primaria 12 °C per 5 giorni, rampa di 1 giorno e diacetyl rest a 17 °C per 3, discesa in 3 giorni e lagerizzazione a 2 °C per 21. Sotto i ~4,5 °C F1 arriva solo abbassando il termostato del chiller |
| `cerveza_especial_ott26.json` | 3 | 57 giorni | Il calendario reale della cotta di ottobre 2026 (in F1 dal 02/10, in F2 dal 17/10; aggiunto il 2026-10-07), per la vista *Cotta*: primaria 12 °C fino al 07/10 11:47, diacetyl rest a 16 °C (Inkbird, a gradino) fino a lunedì 12/10 09:30, poi lagerizzazione fino a sabato 28/11, imbottigliata insieme alla witbier. Lagerizzazione a 4 °C e non a 2 dal 2026-10-23: col chiller condiviso la pompa dell'Inkbird restava sempre accesa senza scendere sotto i 3-4 °C. Le durate frazionarie fanno cadere i cambi su quelle ore partendo dal 02/10 17:38
| `witbier_WB06.json` | 6 | 36 giorni | Witbier con SafAle WB-06 (aggiunto il 2026-10-23 per la seconda cotta in F1): primaria 19 °C per 3 giorni, risalita libera in 1 giorno a 22 °C e finitura per 3, discesa in 1 giorno e cold crash a 6 °C (sopra il pavimento di ~4,5 °C del chiller) per 3, poi attesa a 6 °C fino a sabato 28/11, imbottigliata insieme alla lager. FG obiettivo indicativa 1.010. Sull'ESP32 c'è la prima versione (attesa di 7 giorni): dal 10/11 resta in HOLDING a 6 °C, che è la stessa cosa
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

Facoltativa, una chiave `"eventi"` con le date chiave della cotta (scarichi del cono, controlli,
imbottigliamento), in ordine di tempo: ciascuno ha `"evento"` (etichetta breve), `"step"` (nome di uno
step), `"giorni"` (quanti giorni dopo l'inizio di quello step) e `"nota"` (cosa fare). Legati agli step,
si spostano da soli se uno step precedente cambia durata. Li usa solo la vista *Cotta* di Home Assistant
(linee grigie nel piano completo e tabella "Date chiave", vedi
[`../docs/05_HOME_ASSISTANT_F1.md`](../docs/05_HOME_ASSISTANT_F1.md)); lo script di avvio legge solo
`"profilo"`, quindi il firmware non li riceve.

Facoltativa anche `"fg_obiettivo"`: la densità finale attesa (es. `1.010`). La vista *Cotta* la disegna
come linea orizzontale tratteggiata nel grafico della densità, accanto all'OG di `input_number.ispindel_01_og`;
il firmware non la riceve.
