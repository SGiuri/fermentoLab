"""Genera i pacchetti Home Assistant F2 a partire da quelli F1.

Uso:  python genera_f2.py
F1 e F2 hanno topic, unique_id ed entity_id con lo stesso schema: cambia solo l'identificativo.
"""
import pathlib
import re

QUI = pathlib.Path(__file__).parent

for sorgente in sorted(QUI.glob("fermentatore_v6_f1_*.yaml")):
    if any(s in sorgente.name for s in ("_collaudo", "_ispindel")):
        continue  # collaudo e monitor iSpindel: solo F1
    destinazione = QUI / sorgente.name.replace("_f1_", "_f2_")
    testo = sorgente.read_text(encoding="utf-8")
    testo = testo.replace(
        "# Generato da genera_f2.py: modificare questo file e rigenerare la versione F2.",
        f"# FILE GENERATO da genera_f2.py a partire da {sorgente.name}: non modificare a mano.",
    )
    # f1 / F1 come identificativo isolato o dentro nomi composti (f1_v6, v6_f1_, v6/f1/, ferm_v6_f1_)
    testo = re.sub(r"(?<![0-9A-Za-z])f1(?![0-9])", "f2", testo)
    testo = re.sub(r"(?<![0-9A-Za-z])F1(?![0-9])", "F2", testo)
    testo = re.sub(r"_f1(?=[_/.\b])", "_f2", testo)
    testo = re.sub(r"_f1\b", "_f2", testo)

    assert "f1" not in testo.replace(sorgente.name, ""), f"riferimenti f1 residui in {destinazione.name}"
    destinazione.write_text(testo, encoding="utf-8", newline="\n")
    print(f"scritto {destinazione.name}")
