"""Draws the art of the KATECHON trade mode into assets/:

- katechon_goods_icons.png: one row of line glyphs, one per group of goods, in
  the order of the enum `goods_glyph` in src/gui/gui_map_hud_trade.hpp. White on
  transparent; the game tints them.
- katechon_trade_disc.png: the dark face of a good's medallion.
- katechon_trade_ring.png: the rim of a medallion, white, tinted by the game.
- katechon_trade_gauge.png: a row of 51 arcs from 0 to 100 %, white, clockwise
  from the top, for the gauges of the vulnerabilities.
"""
import math, os
from PIL import Image, ImageDraw, ImageFilter

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'assets')
SS = 8

def arc(cx, cy, rx, ry, a0, a1, n=40):
    return [(cx + rx * math.cos(math.radians(a0 + (a1 - a0) * i / n)), cy + ry * math.sin(math.radians(a0 + (a1 - a0) * i / n))) for i in range(n + 1)]

def gear(cx, cy, R, r, teeth=8):
    pts = []
    for i in range(teeth):
        a = 2 * math.pi * i / teeth; w = 2 * math.pi / teeth
        for da, rad in ((-0.30, r), (-0.18, R), (0.18, R), (0.30, r)):
            pts.append((cx + rad * math.cos(a + da * w), cy + rad * math.sin(a + da * w)))
    return pts

def leaf(x, y, side):
    # a grain kernel leaning away from the stem
    s = 1 if side > 0 else -1
    return [(x, y), (x + s * 2.4, y - 1.6), (x + s * 3.8, y - 4.6), (x + s * 1.6, y - 3.4), (x, y)]

# ('line', points, closed) | ('fill', points) | ('dot', x, y, r) on a 24-unit grid
GLYPHS = [
    ('grain', [('line', [(12, 21), (12, 5)], False), ('line', leaf(12, 9.5, -1), True), ('line', leaf(12, 9.5, 1), True),
               ('line', leaf(12, 14, -1), True), ('line', leaf(12, 14, 1), True), ('line', leaf(12, 18.4, -1), True), ('line', leaf(12, 18.4, 1), True)]),
    ('food', [('line', arc(12, 13.6, 7, 6.8, -60, 240), False), ('line', [arc(12, 13.6, 7, 6.8, 240, 240, 1)[0], (12, 8.6), arc(12, 13.6, 7, 6.8, -60, -60, 1)[0]], False),
              ('line', [(12, 8.6), (13.2, 5.6), (15.6, 3.8)], False)]),
    ('oil', [('line', [(12, 3.2)] + arc(12, 15.2, 6.4, 6.4, -28, 208) + [(12, 3.2)], True), ('line', arc(12, 15.2, 3.3, 3.3, 100, 170, 12), False)]),
    ('coal', [('line', [(4.5, 16.5), (6.5, 9.5), (11.5, 6.5), (17.5, 8.5), (19.5, 14.5), (15.5, 19), (8, 19.5)], True), ('line', [(9, 12), (12.5, 10.5), (15, 13)], False)]),
    ('metal', [('line', [(3.5, 18), (20.5, 18), (17.3, 10.5), (6.7, 10.5)], True), ('line', [(8.2, 10.5), (9.5, 6), (14.5, 6), (15.8, 10.5)], False)]),
    ('timber', [('line', [(4, 7.5), (16.6, 7.5)], False), ('line', [(4, 16.5), (16.6, 16.5)], False), ('line', arc(4, 12, 1.4, 4.5, 90, 270, 16), False),
                ('line', arc(17, 12, 3.4, 4.5, 0, 360), True), ('line', arc(17, 12, 1.1, 1.5, 0, 360, 16), True)]),
    ('textile', [('line', [(8.5, 4), (4, 7), (6, 10.5), (8, 9.5), (8, 20), (16, 20), (16, 9.5), (18, 10.5), (20, 7), (15.5, 4)] + arc(12, 4, 3.5, 2.4, 0, 180, 16)[1:], True)]),
    ('machinery', [('line', gear(12, 12, 8.6, 6.4), True), ('line', arc(12, 12, 2.8, 2.8, 0, 360), True)]),
    ('electronics', [('line', [(7, 7), (17, 7), (17, 17), (7, 17)], True), ('line', [(10, 10), (14, 10), (14, 14), (10, 14)], True),
                     ('line', [(10, 3.5), (10, 7)], False), ('line', [(14, 3.5), (14, 7)], False), ('line', [(10, 17), (10, 20.5)], False), ('line', [(14, 17), (14, 20.5)], False),
                     ('line', [(3.5, 10), (7, 10)], False), ('line', [(3.5, 14), (7, 14)], False), ('line', [(17, 10), (20.5, 10)], False), ('line', [(17, 14), (20.5, 14)], False)]),
    ('chemicals', [('line', [(9, 3.2), (15, 3.2)], False), ('line', [(10.2, 3.2), (10.2, 9.4)] + arc(12, 15, 6.2, 6.2, 237, -57, 40) + [(13.8, 3.2)], False),
                   ('line', [(6.4, 15.6), (17.6, 15.6)], False)]),
    ('arms', [('line', [(9, 20.5), (9, 9.5), (12, 3.8), (15, 9.5), (15, 20.5)], True), ('line', [(9, 16.5), (15, 16.5)], False)]),
    ('ships', [('line', arc(12, 4.8, 1.7, 1.7, 0, 360, 20), True), ('line', [(12, 6.5), (12, 20.5)], False), ('line', [(8, 9.5), (16, 9.5)], False),
               ('line', arc(12, 13.5, 7, 7, 15, 165), False), ('line', [(17.5, 13.2), (18.8, 15.3), (20.4, 13.6)], False), ('line', [(6.5, 13.2), (5.2, 15.3), (3.6, 13.6)], False)]),
    ('furniture', [('line', [(7.5, 3.5), (7.5, 20.5)], False), ('line', [(7.5, 12.5), (17, 12.5), (17, 20.5)], False), ('line', [(7.5, 3.5), (10.5, 3.5)], False),
                   ('line', [(7.5, 8), (10.5, 8)], False)]),
]

def draw_glyph(d, parts, k, stroke):
    for part in parts:
        if part[0] == 'line':
            p = [(x * k, y * k) for x, y in part[1]]
            if part[2]:
                p = p + [p[0]]
            w = stroke * k; r = w / 2
            d.line(p, fill=255, width=int(round(w)), joint='curve')
            for x, y in p:
                d.ellipse((x - r, y - r, x + r, y + r), fill=255)
        elif part[0] == 'fill':
            d.polygon([(x * k, y * k) for x, y in part[1]], fill=255)
        else:
            d.ellipse(((part[1] - part[3]) * k, (part[2] - part[3]) * k, (part[1] + part[3]) * k, (part[2] + part[3]) * k), fill=255)

def white(mask):
    w = Image.new('L', mask.size, 255)
    return Image.merge('RGBA', (w, w, w, mask))

def goods_icons(cell=64):
    k = cell * SS / 24.0
    strip = Image.new('L', (cell * len(GLYPHS), cell), 0)
    for i, (name, parts) in enumerate(GLYPHS):
        big = Image.new('L', (cell * SS, cell * SS), 0)
        draw_glyph(ImageDraw.Draw(big), parts, k, 1.6)
        strip.paste(big.resize((cell, cell), Image.LANCZOS), (i * cell, 0))
    white(strip).save(os.path.join(OUT, 'katechon_goods_icons.png'))

def disc(size=128):
    # a dark face lit from the upper left, with a soft shadow under it
    s = size * 2
    img = Image.new('RGBA', (s, s), (0, 0, 0, 0))
    shadow = Image.new('L', (s, s), 0)
    ImageDraw.Draw(shadow).ellipse((s * 0.10, s * 0.14, s * 0.90, s * 0.94), fill=150)
    shadow = shadow.filter(ImageFilter.GaussianBlur(s * 0.04))
    img.paste((0, 0, 0, 255), (0, 0), shadow)
    face = Image.new('RGBA', (s, s), (0, 0, 0, 0))
    px = face.load()
    cx, cy, r = s * 0.5, s * 0.5, s * 0.40
    lx, ly = s * 0.36, s * 0.32
    for y in range(s):
        for x in range(s):
            if (x - cx) ** 2 + (y - cy) ** 2 <= r * r:
                t = min(1.0, math.hypot(x - lx, y - ly) / (r * 1.45))
                c0, c1 = (45, 34, 26), (20, 15, 11)
                px[x, y] = tuple(int(c0[j] + (c1[j] - c0[j]) * t) for j in range(3)) + (255,)
    img = Image.alpha_composite(img, face)
    img.resize((size, size), Image.LANCZOS).save(os.path.join(OUT, 'katechon_trade_disc.png'))

def ring(size=128):
    s = size * SS
    m = Image.new('L', (s, s), 0)
    d = ImageDraw.Draw(m)
    w = s * 0.045
    d.ellipse((s * 0.10 + w / 2, s * 0.10 + w / 2, s * 0.90 - w / 2, s * 0.90 - w / 2), outline=255, width=int(w))
    white(m.resize((size, size), Image.LANCZOS)).save(os.path.join(OUT, 'katechon_trade_ring.png'))

def gauge(size=152, frames=51):
    strip = Image.new('L', (size * frames, size), 0)
    s = size * 4
    stroke = s * 0.066
    box = (stroke, stroke, s - stroke, s - stroke)
    for f in range(frames):
        part = f / (frames - 1)
        m = Image.new('L', (s, s), 0)
        if part > 0:
            d = ImageDraw.Draw(m)
            d.arc(box, -90, -90 + 360 * part, fill=255, width=int(stroke))
            if part < 1:
                rr = (s - 2 * stroke) / 2 - stroke / 2
                for a in (-90, -90 + 360 * part):
                    x = s / 2 + rr * math.cos(math.radians(a)); y = s / 2 + rr * math.sin(math.radians(a))
                    d.ellipse((x - stroke / 2, y - stroke / 2, x + stroke / 2, y + stroke / 2), fill=255)
        strip.paste(m.resize((size, size), Image.LANCZOS), (f * size, 0))
    white(strip).save(os.path.join(OUT, 'katechon_trade_gauge.png'))

goods_icons()
disc()
ring()
gauge()
print(len(GLYPHS), 'goods glyphs, disc, ring, gauge ->', os.path.normpath(OUT))
