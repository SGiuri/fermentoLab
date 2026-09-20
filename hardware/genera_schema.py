"""Genera hardware/schema_elettrico_f1.svg (schema di cablaggio di F1, ESP32 WROOM)."""
import sys

OUT = sys.argv[1]
OX = 90          # offset orizzontale globale
W, H = 1420, 900
o = []           # elementi SVG


def add(s):
    o.append(s)


def X(x):
    return x + OX


def line(x1, y1, x2, y2, cls="sig", extra=""):
    add('<line x1="%d" y1="%d" x2="%d" y2="%d" class="%s" %s/>' % (X(x1), y1, X(x2), y2, cls, extra))


def poly(pts, cls="sig", extra=""):
    add('<polyline points="%s" class="%s" fill="none" %s/>' % (" ".join("%d,%d" % (X(a), b) for a, b in pts), cls, extra))


def text(x, y, s, cls="t", anchor="start", extra=""):
    add('<text x="%d" y="%d" class="%s" text-anchor="%s" %s>%s</text>' % (X(x), y, cls, anchor, extra, s))


def box(x, y, w, h, cls="blk", rx=6):
    add('<rect x="%d" y="%d" width="%d" height="%d" rx="%d" class="%s"/>' % (X(x), y, w, h, rx, cls))


def dot(x, y, cls="dot"):
    add('<circle cx="%d" cy="%d" r="3.5" class="%s"/>' % (X(x), y, cls))


def gnd(x, y):
    line(x, y, x, y + 6, "gnd")
    line(x - 10, y + 6, x + 10, y + 6, "gnd")
    line(x - 6, y + 10, x + 6, y + 10, "gnd")
    line(x - 2, y + 14, x + 2, y + 14, "gnd")


def rail(x, y, label, cls, up=True):
    d = -9 if up else 9
    line(x, y, x, y + d, cls)
    line(x - 7, y + d, x + 7, y + d, cls)
    text(x, y + d + (-5 if up else 15), label, "tb", "middle")


def res_h(x1, x2, y, cls="sig", rec=False):
    line(x1, y, x1 + 8, y, cls)
    line(x2 - 8, y, x2, y, cls)
    add('<rect x="%d" y="%d" width="%d" height="12" class="comp%s"/>' % (X(x1 + 8), y - 6, x2 - x1 - 16, " rec" if rec else ""))


def res_v(x, y1, y2, cls="sig", rec=False):
    line(x, y1, x, y1 + 8, cls)
    line(x, y2 - 8, x, y2, cls)
    add('<rect x="%d" y="%d" width="12" height="%d" class="comp%s"/>' % (X(x - 6), y1 + 8, y2 - y1 - 16, " rec" if rec else ""))


def cap_v(x, y1, y2, cls="sig", rec=False):
    m = (y1 + y2) // 2
    line(x, y1, x, m - 3, cls)
    line(x, m + 3, x, y2, cls)
    line(x - 10, m - 3, x + 10, m - 3, "plate")
    line(x - 10, m + 3, x + 10, m + 3, "plate")


def pin(x, y, side, label, cls="pl"):
    if side == "L":
        text(x + 8, y + 4, label, cls, "start")
    else:
        text(x - 8, y + 4, label, cls, "end")


# ------------------------------------------------------------------ sfondo e titolo
add('<rect width="%d" height="%d" fill="#ffffff"/>' % (W, H))
text(-60, 34, "FermentaLab F1 — schema elettrico di cablaggio (ESP32 WROOM-32)", "h1")
text(-60, 56, "Revisione 2026-09-20 · un fermentatore: 1 sonda, 2 relè (caldo + freddo), OLED I²C · relè attivi LOW", "sub")

# ------------------------------------------------------------------ ESP32
ex, ey, ew, eh = 130, 80, 220, 600
box(ex - 30, ey, ew, eh, "mcu")
text(ex + 80, ey + 26, "ESP32 DevKit", "hb", "middle")
text(ex + 80, ey + 44, "(WROOM-32, 30/38 pin)", "sub", "middle")
R = ex - 30 + ew          # bordo destro (x=320)
L = ex - 30               # bordo sinistro (x=100)
pins_r = [(120, "3V3", "p33"), (160, "GND", "gnd"), (200, "GPIO21 · SDA", "sig"), (240, "GPIO22 · SCL", "sig"),
          (300, "GPIO33 · DS18B20 DQ", "sig"), (360, "GPIO25 · SENSOR_PWR", "sig"), (410, "GND", "gnd"),
          (480, "GPIO16 · relè CALDO", "sig"), (520, "GPIO17 · relè FREDDO", "sig")]
for y, lab, c in pins_r:
    line(R - 8, y, R, y, c)
    pin(R, y, "R", lab)
line(R, 410, R + 12, 410, "gnd")
gnd(R + 12, 410)
for y, lab in [(540, "EN"), (600, "VIN (5 V)"), (650, "GND")]:
    line(L, y, L + 8, y, "sig")
    pin(L, y, "L", lab)

# EN – condensatore verso GND
line(L - 60, 540, L, 540, "sig")
line(L - 60, 540, L - 60, 566, "sig")
cap_v(L - 60, 566, 600, "sig")
gnd(L - 60, 600)
text(L - 76, 566, "C EN–GND", "t", "end")
text(L - 76, 580, "(in prova su F1)", "sub", "end")
# VIN
line(L - 30, 600, L, 600, "p5")
rail(L - 30, 600, "+5 V", "p5", up=False)
# GND
line(L - 30, 650, L, 650, "gnd")
gnd(L - 30, 650)

# ------------------------------------------------------------------ OLED
ox, oy = 440, 90
box(ox, oy, 190, 170, "blk")
text(ox + 95, oy - 8, "OLED SSD1306 128×64 · I²C 0x3C", "hb", "middle")
for y, lab in [(120, "VCC"), (160, "GND"), (200, "SDA"), (240, "SCL")]:
    pin(ox, y, "L", lab)
line(R, 120, ox, 120, "p33")
line(R, 160, ox, 160, "gnd")
line(R, 200, ox, 200, "sig")
line(R, 240, ox, 240, "sig")
text(ox + 182, oy + 60, "0,96\" · 4 pin", "sub", "end")
text(ox + 182, oy + 76, "pull-up I²C sul modulo", "sub", "end")

# ------------------------------------------------------------------ sonda DS18B20
gx, gy = 440, 292
add('<rect x="%d" y="%d" width="450" height="152" rx="8" class="grp"/>' % (X(gx), gy))
text(gx + 6, gy - 6, "Sonda DS18B20 inox · bus 1-Wire alimentato da GPIO (recupero con power-cycle)", "hb")
px = 700
box(px, 310, 150, 120, "blk")
text(px + 75, 328, "DS18B20", "hb", "middle")
text(px + 75, 342, "3 fili, cavo ≤ 3 m", "sub", "middle")
for y, lab in [(360, "DQ (giallo)"), (395, "VDD (rosso)"), (425, "GND (nero)")]:
    pin(px, y, "L", lab)
# DQ: GPIO33 -> R 100 ohm -> nodo -> pin DQ
line(R, 300, 470, 300, "sig")
res_h(470, 520, 300, "sig", True)
text(495, 322, "100 Ω", "t", "middle")
poly([(520, 300), (580, 300), (580, 360), (px, 360)], "sig")
dot(580, 360)
# VDD commutato: GPIO25 -> pin VDD, pull-up e condensatore
poly([(R, 360), (R + 20, 360), (R + 20, 395), (px, 395)], "sig")
dot(580, 395)
res_v(580, 360, 395, "sig", False)
text(592, 376, "4,7 kΩ", "t")
text(592, 389, "pull-up", "sub")
dot(500, 395)
cap_v(500, 395, 425, "sig", True)
gnd(500, 425)
text(486, 414, "100 nF", "t", "end")
poly([(px, 425), (px - 30, 425)], "gnd")
gnd(px - 30, 425)

# ------------------------------------------------------------------ modulo relè
rx, ry = 440, 468
box(rx, ry, 200, 200, "blk")
text(rx + 100, ry - 8, "Modulo 2 relè 5 V · attivo LOW", "hb", "middle")
text(rx + 100, ry + 178, "tipo opto-isolato, 10 A / 250 V AC", "sub", "middle")
for y, lab in [(480, "IN1 (caldo)"), (520, "IN2 (freddo)"), (580, "VCC"), (625, "GND")]:
    pin(rx, y, "L", lab)
line(R, 480, rx, 480, "sig")
line(R, 520, rx, 520, "sig")
line(rx - 22, 580, rx, 580, "p5")
rail(rx - 22, 580, "+5 V", "p5", up=False)
line(rx - 22, 625, rx, 625, "gnd")
gnd(rx - 22, 625)
rr = rx + 200
for y, lab in [(490, "K1 COM"), (520, "K1 NO"), (570, "K2 COM"), (600, "K2 NO")]:
    pin(rr, y, "R", lab)

# ------------------------------------------------------------------ carichi 230 V
def mains_row(y, kind):
    line(rr, y, 720, y, "mains")
    return


# K1: caldo
line(rr, 490, 700, 490, "mains")
add('<rect x="%d" y="484" width="40" height="12" class="comp rec"/>' % X(700))
text(720, 478, "FU1 T500 mA", "t", "middle")
line(740, 490, 900, 490, "mains")
dot(900, 490, "dotm")
text(912, 494, "L", "tm")
line(rr, 520, 700, 520, "mains")
add('<rect x="%d" y="514" width="150" height="12" class="comp"/>' % X(700))
text(775, 545, "Fascia riscaldante 30–35 W", "t", "middle")
line(850, 520, 900, 520, "mains")
dot(900, 520, "dotm")
text(912, 524, "N", "tm")
# K2: freddo
line(rr, 570, 700, 570, "mains")
add('<rect x="%d" y="564" width="40" height="12" class="comp rec"/>' % X(700))
text(720, 558, "FU2 T500 mA", "t", "middle")
line(740, 570, 900, 570, "mains")
dot(900, 570, "dotm")
text(912, 574, "L", "tm")
line(rr, 600, 700, 600, "mains")
add('<rect x="%d" y="594" width="150" height="12" class="comp"/>' % X(700))
text(775, 625, "Pompa glicole 10–15 W", "t", "middle")
line(850, 600, 900, 600, "mains")
dot(900, 600, "dotm")
text(912, 604, "N", "tm")
# snubber RC in parallelo alla pompa
poly([(690, 600), (690, 640), (710, 640)], "mains")
add('<rect x="%d" y="634" width="46" height="12" class="comp rec"/>' % X(710))
add('<line x1="%d" y1="640" x2="%d" y2="640" class="mains"/>' % (X(756), X(766)))
line(766, 634, 766, 646, "plate")
line(772, 634, 772, 646, "plate")
poly([(772, 640), (782, 640), (862, 640), (862, 600)], "mains")
dot(690, 600)
dot(862, 600)
text(672, 668, "snubber RC 100 Ω + 100 nF X2 (275 V AC)", "sub")
text(672, 682, "in parallelo alla pompa, non sui contatti", "sub")

# ------------------------------------------------------------------ ingresso rete + alimentatore
mx = 980
box(mx, 440, 300, 130, "mainsbox")
text(mx + 150, 460, "Ingresso rete 230 V ~ 50 Hz", "hb", "middle")
text(mx + 12, 486, "L", "tm")
line(mx + 26, 482, mx + 60, 482, "mains")
add('<rect x="%d" y="476" width="46" height="12" class="comp rec"/>' % X(mx + 60))
line(mx + 106, 482, mx + 140, 482, "mains")
dot(mx + 140, 482, "dotm")
text(mx + 152, 486, "→ tutti i punti «L»", "t")
text(mx + 83, 500, "FU0 T2 A", "t", "middle")
text(mx + 12, 512, "N", "tm")
line(mx + 26, 508, mx + 140, 508, "mains")
dot(mx + 140, 508, "dotm")
text(mx + 152, 512, "→ tutti i punti «N»", "t")
text(mx + 12, 542, "PE", "tm")
line(mx + 26, 538, mx + 140, 538, "mains")
dot(mx + 140, 538, "dotm")
text(mx + 152, 542, "→ cassa metallica", "t")

box(mx, 600, 300, 120, "blk")
text(mx + 150, 620, "Alimentatore AC-DC 5 V, ≥ 1 A", "hb", "middle")
text(mx + 150, 636, "da incasso (es. HLK-5M05 / IRM-05-5)", "sub", "middle")
text(mx + 12, 668, "AC: L, N (dopo FU0)", "t")
line(mx + 300, 680, mx + 322, 680, "p5")
rail(mx + 322, 680, "+5 V", "p5", up=True)
line(mx + 300, 705, mx + 322, 705, "gnd")
gnd(mx + 322, 705)
text(mx + 288, 684, "+5 V", "pl", "end")
text(mx + 288, 709, "GND", "pl", "end")

# ------------------------------------------------------------------ legenda e note
lx, ly = -60, 730
box(lx, ly, 880, 150, "leg")
text(lx + 14, ly + 22, "Legenda", "hb")
def leg(y, cls, label, dash=False):
    add('<line x1="%d" y1="%d" x2="%d" y2="%d" class="%s"/>' % (X(lx + 14), y, X(lx + 54), y, cls))
    text(lx + 64, y + 4, label, "t")
leg(ly + 42, "sig", "segnale 3,3 V")
leg(ly + 60, "p33", "alimentazione 3,3 V")
leg(ly + 78, "p5", "alimentazione 5 V")
leg(ly + 96, "gnd", "massa comune (GND)")
leg(ly + 114, "mains", "rete 230 V (pericolo)")
add('<rect x="%d" y="%d" width="40" height="10" class="comp rec"/>' % (X(lx + 14), ly + 124))
text(lx + 64, ly + 133, "componente tratteggiato = raccomandato / da verificare su F1", "t")
text(lx + 410, ly + 46, "Massa comune obbligatoria tra ESP32, relè, sonda e OLED.", "t")
text(lx + 410, ly + 62, "Il pull-up 4,7 kΩ va su GPIO25 (VDD commutato) per", "t")
text(lx + 410, ly + 78, "rendere efficace il power-cycle software del sensore.", "t")
text(lx + 410, ly + 100, "Il chiller NON passa da questa scatola: presa separata,", "t")
text(lx + 410, ly + 116, "termostato proprio, sempre acceso.", "t")
text(lx + 410, ly + 138, "Contatti NO: a riposo (boot, reset, ESP scollegato) i carichi sono spenti.", "t")

svg = ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 %d %d" width="%d" height="%d" font-family="Segoe UI, Arial, sans-serif">'
       '<style>'
       '.h1{font-size:20px;font-weight:700;fill:#111}.sub{font-size:12px;fill:#555}.t{font-size:12px;fill:#222}'
       '.tb{font-size:12px;font-weight:700;fill:#b00020}.hb{font-size:13px;font-weight:700;fill:#111}.pl{font-size:12px;fill:#222}'
       '.tm{font-size:13px;font-weight:700;fill:#8b3a00}'
       '.sig{stroke:#1a5fb4;stroke-width:2;fill:none}.p33{stroke:#e66100;stroke-width:2}.p5{stroke:#c01c28;stroke-width:2}'
       '.gnd{stroke:#222;stroke-width:2;fill:none}.mains{stroke:#8b3a00;stroke-width:3.2;fill:none}.plate{stroke:#222;stroke-width:2.4}'
       '.blk{fill:#f6f8fa;stroke:#333;stroke-width:1.6}.mcu{fill:#eef3fb;stroke:#1a5fb4;stroke-width:2}'
       '.mainsbox{fill:#fff4ea;stroke:#8b3a00;stroke-width:1.6}.leg{fill:#fafafa;stroke:#bbb;stroke-width:1}'
       '.grp{fill:none;stroke:#9aa;stroke-width:1.2;stroke-dasharray:6 4}'
       '.comp{fill:#fff;stroke:#222;stroke-width:1.6}.rec{stroke-dasharray:4 3}'
       '.dot{fill:#1a5fb4}.dotm{fill:#8b3a00}'
       '</style>' % (W, H, W, H)) + "".join(o) + "</svg>"
open(OUT, "w", encoding="utf-8", newline="\n").write(svg)
print("scritto", OUT, len(svg), "byte")
