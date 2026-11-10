#!/usr/bin/env python3
"""Generate the carrier-board drawings (SVG) from the netlist below.

    python docs/hardware/gen_drawings.py

Edit the tables at the top, re-run, commit the SVGs. Stripboard: 7 holes
wide (cols 1-7), rows A.. lettered, 2.54 mm pitch, strips run along rows.
"""
import os

OUT = os.path.dirname(os.path.abspath(__file__))

COLS, ROWS = 7, 22                      # board: 7 x 22 holes (A..V)
ATOM_L = {"A": "GND", "B": "5V", "C": "G38", "D": "G39"}        # col 1
ATOM_R = {"A": "G8", "B": "G7", "C": "G6", "D": "G5", "E": "3V3"}  # col 7
ATOM_BODY_ROWS = "ABCDEFGHI"            # Atom case covers these rows
CUTS = ["A4", "B4", "C4", "D4", "J4", "K4"]
OPTO = [("J5", "1"), ("K5", "2"), ("K2", "3"), ("J2", "4")]      # PC817, dot = pin 1
JACKS = [  # (first row index of pin 2, name, opening side, col of pins 2-5, col of pin 1)
    (11, "PADDLE", "r", 4, 3),          # rows L-P, opening at col-7 edge
    (16, "KEY", "l", 4, 5),             # rows Q-U, opening at col-1 edge
]
RADIO_ROWS_HALF = "JK"                  # left half (cols 1-3) is radio side
RADIO_ROWS_FULL = "QRSTU"               # whole row is radio side
WIRES = [  # (from, to, label, is_resistor, lane) lane = column the vertical run is drawn along (x.5 = between)
    ("D6", "J6", "470 Ω", True, None),
    ("K6", "A3", "GND", False, 4.5),
    ("Q1", "J1", "key tip", False, 0.7),
    ("S1", "K1", "key sleeve", False, 0.35),
    ("L5", "A5", "dit", False, 5.5),
    ("P1", "D2", "dah", False, 2.5),
    ("N1", "A2", "pad GND", False, 1.5),
]
WIRE_COLORS = ["#1f5fa8", "#2a9d8f", "#c77d1a", "#8e44ad", "#d1495b", "#4f7f2a", "#1f5fa8"]

# fixed colours (SVG files have no theme)
BG, FG, MUTED, PAD = "#ffffff", "#1d1b17", "#6b665c", "#ffffff"
COPPER, COPPER_INK, CUT, RADIO, ATOM = "#c98a52", "#5a3516", "#c3362b", "#e9d8c4", "#dfe6ee"

P, M, MX = 26, 34, 64                   # pitch px, top margin, side margin


def row_letter(i): return chr(65 + i)
def row_idx(s): return ord(s[0]) - 65
def col_of(s): return int(s[1:])


class Grid:
    def __init__(self, mirror):
        self.mirror = mirror
        self.W = MX * 2 + (COLS - 1) * P
        self.H = M * 2 + (ROWS - 1) * P

    def X(self, c): return self.W - (MX + (c - 1) * P) if self.mirror else MX + (c - 1) * P
    def Y(self, r): return M + r * P
    def xy(self, s): return self.X(col_of(s)), self.Y(row_idx(s))

    def board(self, copper):
        W, H = self.W, self.H
        s = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W+90} {H+10}" width="{W+90}" height="{H+10}" '
             f'font-family="Menlo,Consolas,monospace" style="background:{BG}">']
        s.append(f'<rect x="4" y="4" width="{W-8}" height="{H-8}" fill="#2f6b3a" opacity=".25"/>')
        for r in range(ROWS):
            L = row_letter(r); y = self.Y(r)
            x1, x2 = sorted((self.X(1), self.X(7)))
            if copper:
                s.append(f'<rect x="{x1-9}" y="{y-7}" width="{x2-x1+18}" height="14" rx="7" fill="{COPPER}"/>')
            if L in RADIO_ROWS_HALF or L in RADIO_ROWS_FULL:
                a, b = sorted((self.X(1), self.X(7 if L in RADIO_ROWS_FULL else 3)))
                s.append(f'<rect x="{a-11}" y="{y-11}" width="{b-a+22}" height="22" fill="{RADIO}" opacity=".9" rx="4"/>')
            s.append(f'<text x="{W+2 if self.mirror else 12}" y="{y+4}" font-size="11" fill="{MUTED}">{L}</text>')
            for c in range(1, COLS + 1):
                x = self.X(c)
                if L + str(c) in CUTS:
                    s.append(f'<circle cx="{x}" cy="{y}" r="7" fill="{PAD}" stroke="{CUT}" stroke-width="2"/>'
                             f'<path d="M{x-4} {y-4}L{x+4} {y+4}M{x+4} {y-4}L{x-4} {y+4}" stroke="{CUT}" stroke-width="2"/>')
                else:
                    s.append(f'<circle cx="{x}" cy="{y}" r="3.2" fill="{PAD}" stroke="{COPPER_INK}" stroke-width=".8"/>')
        for c in range(1, COLS + 1):
            s.append(f'<text x="{self.X(c)-3}" y="{M-18}" font-size="10" fill="{MUTED}">{c}</text>')
        return s


def pin(g, hole, label, side):
    x, y = g.xy(hole)
    t = f'<circle cx="{x}" cy="{y}" r="5.5" fill="{ATOM}" stroke="{FG}" stroke-width="1.2"/>'
    if label:
        t += (f'<text x="{x-10 if side=="l" else x+10}" y="{y+4}" font-size="10" '
              f'text-anchor="{"end" if side=="l" else "start"}" fill="{FG}" font-weight="600">{label}</text>')
    return t


def numbered(g, hole, n, filled=False):
    x, y = g.xy(hole)
    return (f'<circle cx="{x}" cy="{y}" r="5" fill="{FG if filled else PAD}" stroke="{FG}" stroke-width="1.2"/>'
            f'<text x="{x}" y="{y+3.5}" font-size="8" text-anchor="middle" fill="{BG if filled else FG}" font-weight="600">{n}</text>')


def parts(g, bodies):
    t = []
    xl, xr = min(g.X(1), g.X(7)) - 13, max(g.X(1), g.X(7)) + 13      # board edges
    if bodies:
        y0, y1 = g.Y(0) - 20, g.Y(len(ATOM_BODY_ROWS) - 1) + 16
        t.append(f'<rect x="{xl-3}" y="{y0}" width="{xr-xl+6}" height="{y1-y0}" fill="{ATOM}" opacity=".45" '
                 f'stroke="{FG}" stroke-dasharray="4 3" rx="6"/>'
                 f'<text x="{(xl+xr)/2}" y="{g.Y(6)+4}" font-size="10" text-anchor="middle" fill="{FG}">Atom body</text>'
                 f'<text x="{(xl+xr)/2}" y="{g.Y(0)-24}" font-size="8" text-anchor="middle" fill="{MUTED}">▲ USB-C</text>')
    for L, n in ATOM_L.items(): t.append(pin(g, L + "1", n, "r" if g.mirror else "l"))
    for L, n in ATOM_R.items(): t.append(pin(g, L + "7", n, "l" if g.mirror else "r"))
    # opto
    kx1, kx2 = sorted((g.X(2), g.X(5))); ky = g.Y(row_idx(OPTO[0][0]))
    if bodies:
        t.append(f'<rect x="{kx1-10}" y="{ky-12}" width="{kx2-kx1+20}" height="{P+24}" fill="{PAD}" stroke="{FG}" '
                 f'stroke-width="1.2" rx="3" opacity=".95"/>'
                 f'<text x="{(kx1+kx2)/2}" y="{ky+P/2+4}" font-size="9" text-anchor="middle" fill="{FG}">PC817</text>')
    else:
        t.append(f'<text x="{(kx1+kx2)/2}" y="{ky-10}" font-size="8" text-anchor="middle" fill="{MUTED}">PC817 pins</text>')
    for hole, n in OPTO: t.append(numbered(g, hole, n, filled=(n == "1")))
    # jacks: body is 14.1 mm deep from its opening edge, 12 mm wide (5 rows)
    depth = 14.1 / 2.54 * P
    for r0, name, side, pcol, p1col in JACKS:
        out = ("r" if side == "l" else "l") if g.mirror else side
        y0, y1 = g.Y(r0) - 14, g.Y(r0 + 4) + 14
        if out == "l": bx0, bx1, ax = xl, xl + depth, xl - 9
        else:          bx0, bx1, ax = xr - depth, xr, xr + 9
        if bodies:
            t.append(f'<rect x="{bx0}" y="{y0}" width="{bx1-bx0}" height="{y1-y0}" fill="{PAD}" stroke="{FG}" '
                     f'stroke-width="1.2" stroke-dasharray="4 3" rx="4" opacity=".92"/>')
            nx = bx1 - 6 if out == "l" else bx0 + 6
            t.append(f'<text x="{nx}" y="{y0+12}" font-size="9" text-anchor="{"end" if out=="l" else "start"}" fill="{FG}">{name}</text>')
        t.append(f'<text x="{ax}" y="{(y0+y1)/2+4}" font-size="11" text-anchor="middle" fill="{FG}">{"◀" if out=="l" else "▶"}</text>')
        if not bodies:
            t.append(f'<text x="{ax}" y="{(y0+y1)/2+16}" font-size="7" text-anchor="middle" fill="{MUTED}">{name}</text>')
        for i, n in zip((0, 1, 3, 4), "2345"):
            t.append(numbered(g, row_letter(r0 + i) + str(pcol), n))
        t.append(numbered(g, row_letter(r0 + 2) + str(p1col), "1", filled=True))
    return t


def wires(g):
    t = []
    for i, (a, b, label, res, lane) in enumerate(WIRES):
        ax, ay = g.xy(a); bx, by = g.xy(b)
        col = WIRE_COLORS[i % len(WIRE_COLORS)]
        if res:
            t.append(f'<path d="M{ax} {ay}L{bx} {by}" stroke="{col}" stroke-width="3" fill="none"/>')
            mx, my = (ax + bx) / 2, (ay + by) / 2
            t.append(f'<rect x="{mx-6}" y="{my-13}" width="12" height="26" fill="{BG}" stroke="{col}" stroke-width="2" rx="2"/>')
            lx, ly = mx + 9, my + 3
        else:
            xm = g.X(lane)
            t.append(f'<path d="M{ax} {ay}L{xm} {ay}L{xm} {by}L{bx} {by}" stroke="{col}" stroke-width="2.5" fill="none" '
                     f'stroke-linejoin="round" opacity=".95"/>')
            lx, ly = xm + 4, (ay + by) / 2 - 4
        t.append(f'<circle cx="{ax}" cy="{ay}" r="3.5" fill="{col}"/><circle cx="{bx}" cy="{by}" r="3.5" fill="{col}"/>')
        t.append(f'<text x="{lx}" y="{ly}" font-size="9" fill="{col}" font-weight="600">{label}</text>')
    return t


def schematic():
    return f'''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 520 300" width="520" height="300" font-family="Menlo,Consolas,monospace" font-size="11" fill="{FG}" stroke="{FG}" stroke-width="1.5" style="background:{BG}">
<text x="14" y="30" stroke="none" font-weight="600">AtomS3 Lite</text>
<rect x="14" y="40" width="70" height="150" fill="none" stroke-dasharray="4 3"/>
<text x="22" y="70" stroke="none">G5</text><line x1="84" y1="66" x2="120" y2="66"/>
<text x="22" y="120" stroke="none">G8</text><line x1="84" y1="116" x2="120" y2="116"/>
<text x="22" y="145" stroke="none">G39</text><line x1="84" y1="141" x2="120" y2="141"/>
<text x="22" y="178" stroke="none">GND</text><line x1="84" y1="174" x2="120" y2="174"/>
<rect x="120" y="59" width="36" height="14" fill="{PAD}"/><text x="122" y="54" stroke="none" font-size="10">470 Ω</text>
<line x1="156" y1="66" x2="200" y2="66"/>
<rect x="200" y="46" width="90" height="80" fill="{PAD}"/>
<text x="212" y="40" stroke="none" font-weight="600">PC817</text>
<line x1="200" y1="66" x2="220" y2="66"/><polygon points="220,58 236,66 220,74" fill="{FG}"/><line x1="236" y1="58" x2="236" y2="74"/>
<line x1="236" y1="66" x2="236" y2="106"/><line x1="236" y1="106" x2="200" y2="106"/>
<text x="204" y="62" stroke="none" font-size="9">1</text><text x="204" y="118" stroke="none" font-size="9">2</text>
<line x1="244" y1="74" x2="256" y2="86" stroke-width="1"/><line x1="244" y1="84" x2="256" y2="96" stroke-width="1"/>
<line x1="262" y1="70" x2="262" y2="104"/><line x1="262" y1="78" x2="276" y2="66"/><line x1="262" y1="96" x2="276" y2="108"/>
<line x1="276" y1="66" x2="290" y2="66"/><line x1="276" y1="108" x2="290" y2="108"/>
<text x="280" y="62" stroke="none" font-size="9">4</text><text x="280" y="120" stroke="none" font-size="9">3</text>
<line x1="120" y1="174" x2="180" y2="174"/><line x1="180" y1="174" x2="180" y2="106"/><line x1="180" y1="106" x2="200" y2="106"/>
<line x1="290" y1="66" x2="380" y2="66"/><line x1="290" y1="108" x2="380" y2="108"/>
<rect x="380" y="50" width="80" height="70" fill="none" stroke-dasharray="4 3"/>
<text x="388" y="44" stroke="none" font-weight="600">KEY jack → radio</text>
<text x="388" y="70" stroke="none">tip  (key)</text><text x="388" y="112" stroke="none" font-size="10">sleeve (radio GND)</text>
<line x1="300" y1="20" x2="300" y2="290" stroke="{CUT}" stroke-dasharray="6 4" stroke-width="1"/>
<text x="306" y="150" stroke="none" fill="{CUT}" font-size="10">no connection across this line</text>
<rect x="120" y="200" width="150" height="80" fill="none" stroke-dasharray="4 3"/>
<text x="126" y="194" stroke="none" font-weight="600">PADDLE jack</text>
<line x1="120" y1="116" x2="150" y2="116"/><line x1="150" y1="116" x2="150" y2="220"/><text x="156" y="224" stroke="none">tip  = dit</text>
<line x1="120" y1="141" x2="140" y2="141"/><line x1="140" y1="141" x2="140" y2="245"/><text x="156" y="249" stroke="none">ring = dah</text>
<line x1="180" y1="174" x2="180" y2="270"/><line x1="180" y1="270" x2="150" y2="270"/><text x="156" y="266" stroke="none" font-size="10">sleeve = GND</text>
<text x="14" y="292" stroke="none" font-size="10" fill="{MUTED}">Optional: 10 k from G8 and G39 to 3V3, 100 nF to GND, for RF-rich sites.</text>
</svg>
'''


def case_plan():
    k = 4; cw, ch = 24 * k, 62 * k
    bx, by = 10 + (24 - 17.8) / 2 * k, 10 + 2 * k
    pj, kj = by + 11 * 2.54 * k, by + 16 * 2.54 * k
    return f'''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {cw+150} {ch+40}" width="{cw+150}" height="{ch+40}" font-family="Menlo,Consolas,monospace" style="background:{BG}">
<rect x="10" y="10" width="{cw}" height="{ch}" fill="{PAD}" stroke="{FG}" stroke-width="2" rx="6"/>
<rect x="{bx}" y="{by}" width="{17.8*k}" height="{55.9*k}" fill="{COPPER}" opacity=".45"/>
<rect x="10" y="10" width="{24*k}" height="{24*k}" fill="{ATOM}" stroke="{FG}" stroke-width="1.5" rx="6"/>
<text x="{10+12*k}" y="{10+12*k+4}" font-size="9" text-anchor="middle" fill="{FG}">Atom flush</text>
<rect x="{10+8*k}" y="6" width="{8*k}" height="6" fill="{ATOM}" stroke="{FG}"/><text x="{10+cw+8}" y="14" font-size="9" fill="{MUTED}">USB-C out the end</text>
<rect x="{bx+3.7*k}" y="{pj}" width="{14.1*k}" height="{12*k}" fill="{ATOM}" stroke="{FG}"/><text x="{bx+10.7*k}" y="{pj+6*k+3}" font-size="8" text-anchor="middle" fill="{FG}">PADDLE ▶</text>
<rect x="{bx}" y="{kj}" width="{14.1*k}" height="{12*k}" fill="{RADIO}" stroke="{FG}"/><text x="{bx+7*k}" y="{kj+6*k+3}" font-size="8" text-anchor="middle" fill="{FG}">◀ KEY</text>
<circle cx="{10+cw}" cy="{pj+6*k}" r="7" fill="{PAD}" stroke="{FG}"/><circle cx="10" cy="{kj+6*k}" r="7" fill="{PAD}" stroke="{FG}"/>
<text x="{10+cw/2}" y="{ch+32}" font-size="10" text-anchor="middle" fill="{MUTED}">24 mm</text>
<text x="{cw+30}" y="{10+ch/2}" font-size="10" fill="{MUTED}">≈ 62 mm long</text>
<text x="{cw+30}" y="{10+ch/2+14}" font-size="10" fill="{MUTED}">≈ 17 mm tall</text>
</svg>
'''


def write(name, body):
    with open(os.path.join(OUT, name), "w", encoding="utf-8", newline="\n") as f:
        f.write(body)
    print("wrote", name)


if __name__ == "__main__":
    g = Grid(mirror=False)
    write("copper-side.svg", "\n".join(g.board(True) + parts(g, False) + wires(g) + ["</svg>"]) + "\n")
    g = Grid(mirror=True)
    write("component-side.svg", "\n".join(g.board(False) + parts(g, True) + ["</svg>"]) + "\n")
    write("schematic.svg", schematic())
    write("case-plan.svg", case_plan())
