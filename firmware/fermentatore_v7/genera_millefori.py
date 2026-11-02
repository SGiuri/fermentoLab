"""Schema della millefori a strisce del nodo sensori V7 (ESP32-S3 Super Mini).

Una sola descrizione del layout, due uscite:
  - SVG autonomo con colori fissi (per il repository): vista lato componenti + lato rame;
  - due frammenti <svg> con classi CSS (per la pagina, che li colora coi suoi token chiaro/scuro).

    python genera_millefori.py <svg_repo> <frammento_componenti> <frammento_rame>
"""
import sys

P = 26                      # passo del disegno (px) per 2,54 mm
COLONNE, STRISCE = 20, 13   # fori a..t, strisce 1..13
ML, MT = 46, 34             # margini per numeri delle strisce e lettere dei fori
LETTERE = 'abcdefghijklmnopqrst'

# --- layout (colonna 1 = a, striscia 1 in alto, USB-C del modulo in alto) ---
PIN_SX = (7, ['TX', 'RX', '1', '2', '3', '4', '5', '6', '7'])          # colonna g, strisce 2..10
PIN_DX = (13, ['5V', 'GND', '3V3', '13', '12', '11', '10', '9', '8'])  # colonna m, strisce 2..10
TAGLI = [(s, 10) for s in range(2, 11)] + [(6, 6)]                      # (striscia, colonna): j e f
CONNETTORI = [  # nome, colonna, prima striscia, lettere dall'alto
    ('F2', 2, 4, ['D', 'V', 'G']),
    ('F1', 4, 6, ['G', 'V', 'D']),
    ('CH', 2, 9, ['D', 'V', 'G']),
]
RESISTENZE = [(4, 4), (2, 7), (4, 9)]           # colonna, striscia superiore (l'altra gamba sotto)
PONTICELLI = [('W1', 15, 3, 11), ('W2', 1, 6, 11)]  # nome, colonna, da striscia, a striscia

COLORI = {  # per l'SVG autonomo (tema chiaro)
    'board': '#e9dcc0', 'copper': '#d9a066', 'hole': '#7a5a3a', 'cut': '#d23b2f',
    'pin': '#3a3a3a', 'module': '#2f5f8f', 'modfill': '#2f5f8f', 'header': '#262626',
    'htext': '#f2f2f2', 'res': '#c9b48a', 'resedge': '#6b5a3a', 'lead': '#8c8c8c',
    'wire': '#2463b5', 'text': '#1f1f1f', 'muted': '#6b6257', 'solder': '#9aa3ab', 'bg': '#ffffff',
}


class Disegno:
    def __init__(self, autonomo, specchio=False, dx=0, dy=0):
        self.autonomo, self.specchio, self.dx, self.dy = autonomo, specchio, dx, dy
        self.out = []

    def a(self, fill=None, stroke=None, sw=None, op=None):
        if self.autonomo:
            s = ''
            if fill: s += ' fill="%s"' % COLORI[fill]
            if stroke: s += ' stroke="%s"' % COLORI[stroke]
            if not fill: s += ' fill="none"'
        else:
            cls = []
            if fill: cls.append('f-' + fill)
            if stroke: cls.append('s-' + stroke)
            s = ' class="%s"' % ' '.join(cls) if cls else ''
            if not fill: s += ' fill="none"'
        if sw: s += ' stroke-width="%s"' % sw
        if op is not None: s += ' fill-opacity="%s"' % op
        return s

    def cx(self, col):
        c = COLONNE + 1 - col if self.specchio else col
        return self.dx + ML + (c - 0.5) * P

    def cy(self, s):
        return self.dy + MT + (s - 0.5) * P

    def add(self, x):
        self.out.append(x)

    def testo(self, x, y, t, ruolo='text', size=11, anchor='middle', weight=None, mono=False, alone=False):
        w = ' font-weight="%s"' % weight if weight else ''
        fam = ' font-family="IBM Plex Mono, Consolas, monospace"' if mono else ''
        # alone: un bordo del colore della scheda dietro le lettere, per leggerle sopra strisce e fori
        st = (self.a(fill=ruolo, stroke='board', sw=4) + ' paint-order="stroke" stroke-linejoin="round"') if alone else self.a(fill=ruolo)
        self.add('<text x="%.1f" y="%.1f" font-size="%s" text-anchor="%s"%s%s%s>%s</text>'
                 % (x, y, size, anchor, w, fam, st, t))


def base(d):
    """Scheda, strisce, fori, tagli, numeri e lettere."""
    d.add('<rect x="%d" y="%d" width="%d" height="%d" rx="6"%s/>'
          % (d.dx + ML - 6, d.dy + MT - 6, COLONNE * P + 12, STRISCE * P + 12, d.a(fill='board')))
    for s in range(1, STRISCE + 1):
        d.add('<rect x="%.1f" y="%.1f" width="%d" height="12" rx="3"%s/>'
              % (d.dx + ML + 2, d.cy(s) - 6, COLONNE * P - 4, d.a(fill='copper')))
        d.testo(d.dx + ML - 16, d.cy(s) + 4, str(s), 'muted', 10, mono=True)
    for c in range(1, COLONNE + 1):
        d.testo(d.cx(c), d.dy + MT - 12, LETTERE[c - 1], 'muted', 10, mono=True)
        for s in range(1, STRISCE + 1):
            d.add('<circle cx="%.1f" cy="%.1f" r="3"%s/>' % (d.cx(c), d.cy(s), d.a(fill='hole')))
    for s, c in TAGLI:
        x, y = d.cx(c), d.cy(s)
        d.add('<rect x="%.1f" y="%.1f" width="12" height="16"%s/>' % (x - 6, y - 8, d.a(fill='board')))
        d.add('<path d="M%.1f %.1fl10 10m0 -10l-10 10" stroke-linecap="round"%s/>'
              % (x - 5, y - 5, d.a(stroke='cut', sw=2.4)))


def lato_componenti(d):
    base(d)
    # resistenze verticali, a 4,7 kΩ
    for c, s in RESISTENZE:
        x, y1, y2 = d.cx(c), d.cy(s), d.cy(s + 1)
        d.add('<line x1="%.1f" y1="%.1f" x2="%.1f" y2="%.1f"%s/>' % (x, y1, x, y2, d.a(stroke='lead', sw=2)))
        d.add('<rect x="%.1f" y="%.1f" width="12" height="16" rx="5"%s/>'
              % (x - 6, y1 - 10, d.a(fill='res', stroke='resedge', sw=1)))
    # connettori 3 pin (VDD al centro)
    for nome, c, s0, lettere in CONNETTORI:
        x = d.cx(c)
        d.add('<rect x="%.1f" y="%.1f" width="18" height="%.1f" rx="2"%s/>'
              % (x - 9, d.cy(s0) - 11, 2 * P + 22, d.a(fill='header')))
        for i, l in enumerate(lettere):
            d.testo(x, d.cy(s0 + i) + 4, l, 'htext', 10, weight='600', mono=True)
    # modulo (trasparente: sotto ci sono i tagli della colonna j)
    x1, x2 = d.cx(PIN_SX[0]) - 0.6 * P, d.cx(PIN_DX[0]) + 0.6 * P
    y1, y2 = d.cy(2) - 1.0 * P, d.cy(10) + 0.55 * P
    xm = (x1 + x2) / 2
    d.add('<rect x="%.1f" y="%.1f" width="%.1f" height="%.1f" rx="5"%s/>'
          % (xm - 0.85 * P, y1 - 0.5 * P, 1.7 * P, 0.7 * P, d.a(fill='module', op=0.9)))
    d.add('<rect x="%.1f" y="%.1f" width="%.1f" height="%.1f" rx="6"%s/>'
          % (x1, y1, x2 - x1, y2 - y1, d.a(fill='modfill', stroke='module', sw=2, op=0.12)))
    d.testo(xm, y1 - 0.18 * P, 'USB-C', 'htext', 9, weight='600')
    d.testo(xm, y1 + 0.62 * P, 'ESP32-S3 Super Mini', 'text', 10, weight='600')
    for col, etichette in (PIN_SX, PIN_DX):
        for i, t in enumerate(etichette):
            x, y = d.cx(col), d.cy(2 + i)
            d.add('<rect x="%.1f" y="%.1f" width="9" height="9"%s/>' % (x - 4.5, y - 4.5, d.a(fill='pin')))
            sx = col == PIN_SX[0]
            usato = t in ('1', '2', '4', '5', '6', '7', 'GND')
            d.testo(x + (9 if sx else -9), y + 4, t, 'text' if usato else 'muted', 10,
                    'start' if sx else 'end', weight='600' if usato else None, mono=True)
    # ponticelli isolati
    for nome, c, s1, s2 in PONTICELLI:
        x = d.cx(c)
        d.add('<line x1="%.1f" y1="%.1f" x2="%.1f" y2="%.1f" stroke-linecap="round"%s/>'
              % (x, d.cy(s1), x, d.cy(s2), d.a(stroke='wire', sw=4)))
        for s in (s1, s2):
            d.add('<circle cx="%.1f" cy="%.1f" r="4.5"%s/>' % (x, d.cy(s), d.a(fill='wire')))
    # etichette
    d.testo(d.cx(2), d.cy(3) + 4, 'F2', 'text', 11, weight='700', alone=True)
    d.testo(d.cx(5) - 6, d.cy(7) + 4, 'F1', 'text', 11, 'start', weight='700', alone=True)
    d.testo(d.cx(2), d.cy(12) + 4, 'CH', 'text', 11, weight='700', alone=True)
    for c, s in RESISTENZE:
        d.testo(d.cx(c) + (10 if c == 4 else 9), d.cy(s) + 0.5 * P + 4, '4k7', 'muted', 9, 'start', mono=True, alone=True)
    d.testo(d.cx(16) - 6, d.cy(7) + 4, 'W1 · GND', 'text', 10, 'start', weight='600', alone=True)
    d.testo(d.cx(1), d.cy(5) + 4, 'W2', 'text', 10, weight='600', alone=True)
    d.testo(d.cx(16) - 6, d.cy(12) + 4, 'barra GND = striscia 11', 'text', 10, 'start', alone=True)


def lato_rame(d):
    base(d)
    punti = set()
    for col, et in (PIN_SX, PIN_DX):
        punti.update((col, 2 + i) for i in range(len(et)))
    for _, c, s0, l in CONNETTORI:
        punti.update((c, s0 + i) for i in range(len(l)))
    for c, s in RESISTENZE:
        punti.update({(c, s), (c, s + 1)})
    for _, c, s1, s2 in PONTICELLI:
        punti.update({(c, s1), (c, s2)})
    for c, s in sorted(punti):
        d.add('<circle cx="%.1f" cy="%.1f" r="5.5"%s/>' % (d.cx(c), d.cy(s), d.a(fill='solder')))
    d.testo(d.cx(10), d.cy(1) + 4, '9 tagli in colonna j', 'text', 10, weight='600', alone=True)
    d.testo(d.cx(6), d.cy(12) + 4, 'taglio f6', 'text', 10, weight='600', alone=True)
    d.add('<path d="M%.1f %.1fL%.1f %.1f"%s/>' % (d.cx(6), d.cy(12) - 6, d.cx(6), d.cy(6) + 9, d.a(stroke='muted', sw=1)))


W = ML + COLONNE * P + 16
H = MT + STRISCE * P + 16


def svg(d, w, h, label):
    return ('<svg viewBox="0 0 %d %d" role="img" aria-label="%s" xmlns="http://www.w3.org/2000/svg" '
            'font-family="IBM Plex Sans, Segoe UI, sans-serif">%s</svg>' % (w, h, label, ''.join(d.out)))


def main():
    repo, f_comp, f_rame = sys.argv[1:4]
    # SVG autonomo: le due viste affiancate, coi titoli
    d = Disegno(True, dy=26)
    d.add('<rect width="%d" height="%d"%s/>' % (2 * W + 30, H + 40, d.a(fill='bg')))
    d.testo(ML, 18, 'Lato componenti (strisce viste in trasparenza)', 'text', 13, 'start', weight='700')
    lato_componenti(d)
    r = Disegno(True, specchio=True, dx=W + 30, dy=26)
    r.testo(W + 30 + ML, 18, 'Lato rame, specchiato: tagli e saldature', 'text', 13, 'start', weight='700')
    lato_rame(r)
    d.out += r.out
    open(repo, 'w', encoding='utf-8').write(
        '<?xml version="1.0" encoding="UTF-8"?>\n<!-- Generato da genera_millefori.py: nodo sensori V7, ESP32-S3 Super Mini -->\n'
        + svg(d, 2 * W + 30, H + 40, 'Millefori del nodo sensori S3, lato componenti e lato rame') + '\n')
    c = Disegno(False)
    lato_componenti(c)
    open(f_comp, 'w', encoding='utf-8').write(svg(c, W, H, 'Millefori lato componenti: modulo S3, tre connettori, tre resistenze, due ponticelli di massa'))
    m = Disegno(False, specchio=True)
    lato_rame(m)
    open(f_rame, 'w', encoding='utf-8').write(svg(m, W, H, 'Millefori lato rame specchiato: dieci tagli e i punti di saldatura'))
    print('ok', W, H)


main()
