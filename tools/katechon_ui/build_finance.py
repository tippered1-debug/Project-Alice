"""Draws the art of the KATECHON state finances sheet into assets/.

The treasury hall: lacquered navy panels inside one thin gilt line with
engraved scrollwork in the corners, gilt buttons, a metal slider knob, the
medallions of the figures, a strip of engraved gold icons and the backdrop of
the sheet. Everything is drawn four times larger and reduced, so that edges
come out smooth. Plates are cut by the game in nine pieces (corner sizes are
noted below), so they stretch without bending their corners.

The header band keeps the painted treasury of the reference design (columns,
the archive, the desk with its books and candle; kept as finance_hall.png
beside this script, cut from the design when it is given) and continues it to
the left with a dark forest drawn here.

    python3 tools/katechon_ui/build_finance.py [design.png]
"""
import math
import os
import random
import sys

from PIL import Image, ImageChops, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
ASSETS = os.path.normpath(os.path.join(HERE, '..', '..', 'assets'))
SS = 4

GOLD = (199, 163, 106)
GOLD_LIGHT = (232, 205, 150)
GOLD_DEEP = (150, 112, 62)
GOLD_DIM = (112, 88, 54)
NAVY = (17, 29, 44)
NAVY_RAISED = (27, 40, 58)
NAVY_DEEP = (9, 15, 27)
EDGE = (5, 8, 14)
PAGE = (22, 21, 30)  # the wall behind the panels; plates are laid on it, since the game draws them without alpha


def canvas(w, h, fill=(0, 0, 0, 0)):
	return Image.new('RGBA', (w * SS, h * SS), fill)


def done(img, name, quiet=False):
	w, h = img.size
	img = img.resize((w // SS, h // SS), Image.LANCZOS)
	img.save(os.path.join(ASSETS, name), optimize=True)
	if not quiet:
		print(name, img.size)
	return img


def gradient(size, top, bottom):
	w, h = size
	g = Image.new('RGBA', size)
	d = ImageDraw.Draw(g)
	for y in range(h):
		t = y / max(1, h - 1)
		d.line([(0, y), (w, y)], fill=tuple(int(top[i] + (bottom[i] - top[i]) * t) for i in range(3)) + (255,))
	return g


def gilt(mask, light=GOLD_LIGHT, deep=GOLD_DEEP, shade=True):
	"""Colours a white mask as engraved gold: light above, deep below, a dark cut under every stroke."""
	w, h = mask.size
	g = gradient((w, h), light, deep)
	out = Image.new('RGBA', (w, h), (0, 0, 0, 0))
	if shade:
		cut = Image.new('RGBA', (w, h), (6, 8, 12, 0))
		shifted = ImageChops.offset(mask, 0, max(1, h // 64))
		cut.putalpha(shifted.point(lambda v: int(v * 0.55)))
		out.alpha_composite(cut)
	g.putalpha(mask)
	out.alpha_composite(g)
	return out


def grain(img, box, amount, seed, step=SS):
	rnd = random.Random(seed)
	x0, y0, x1, y1 = box
	layer = Image.new('RGBA', (x1 - x0, y1 - y0), (0, 0, 0, 0))
	d = ImageDraw.Draw(layer)
	for y in range(0, y1 - y0, step):
		for x in range(0, x1 - x0, step):
			v = rnd.randint(-amount, amount)
			if v:
				d.rectangle([x, y, x + step - 1, y + step - 1], fill=(255, 255, 255, v) if v > 0 else (0, 0, 0, -v))
	img.alpha_composite(layer, (x0, y0))


# ---- the icons ----
#
# Each icon is drawn on a 100 x 100 field into a white mask, then gilded.

CELL = 96


class Pen:
	def __init__(self, size):
		self.size = size
		self.k = size / 100.0
		self.mask = Image.new('L', (size, size), 0)
		self.d = ImageDraw.Draw(self.mask)

	def p(self, x, y):
		return (x * self.k, y * self.k)

	def pts(self, pts):
		return [self.p(x, y) for x, y in pts]

	def w(self, width):
		return max(1, int(round(width * self.k)))

	def line(self, pts, width=6, fill=255):
		pts = self.pts(pts)
		self.d.line(pts, fill=fill, width=self.w(width), joint='curve')
		r = width * self.k / 2
		for x, y in (pts[0], pts[-1]):
			self.d.ellipse([x - r, y - r, x + r, y + r], fill=fill)

	def poly(self, pts, fill=255, outline=None, width=6):
		self.d.polygon(self.pts(pts), fill=fill)
		if outline is not None:
			self.line(list(pts) + [pts[0]], width, outline)

	def rect(self, x0, y0, x1, y1, fill=255, outline=None, width=6, radius=0):
		box = [*self.p(x0, y0), *self.p(x1, y1)]
		if outline is not None:
			self.d.rounded_rectangle(box, radius=radius * self.k, fill=fill, outline=outline, width=self.w(width))
		else:
			self.d.rounded_rectangle(box, radius=radius * self.k, fill=fill)

	def ellipse(self, x0, y0, x1, y1, fill=None, outline=255, width=6):
		box = [*self.p(x0, y0), *self.p(x1, y1)]
		self.d.ellipse(box, fill=fill, outline=outline, width=self.w(width) if outline is not None else 0)

	def circle(self, cx, cy, r, fill=None, outline=255, width=6):
		self.ellipse(cx - r, cy - r, cx + r, cy + r, fill, outline, width)

	def arc(self, cx, cy, rx, ry, a0, a1, width=6, fill=255):
		box = [*self.p(cx - rx, cy - ry), *self.p(cx + rx, cy + ry)]
		self.d.arc(box, a0, a1, fill=fill, width=self.w(width))

	def erase(self):
		return 0


def coin_side(pen, cx, cy, rx, ry, depth):
	"""A coin seen from the side and above: an ellipse with its rim below."""
	pen.d.rectangle([*pen.p(cx - rx, cy), *pen.p(cx + rx, cy + depth)], fill=255)
	pen.ellipse(cx - rx, cy + depth - ry, cx + rx, cy + depth + ry, fill=255, outline=None)
	pen.ellipse(cx - rx, cy - ry, cx + rx, cy + ry, fill=0, outline=255, width=5)
	pen.ellipse(cx - rx * 0.62, cy - ry * 0.5, cx + rx * 0.62, cy + ry * 0.5, fill=None, outline=255, width=3)
	for i in range(1, 6):  # the milled edge
		x = cx - rx + 2 * rx * i / 6
		pen.d.line([pen.p(x, cy + 2), pen.p(x, cy + depth - 1)], fill=0, width=pen.w(2))


def icon_coin_stack(pen):
	for i, y in enumerate((70, 58, 46, 34)):
		coin_side(pen, 40, y, 26, 8, 9)
	for y in (72, 60):
		coin_side(pen, 72, y, 18, 6, 8)


def icon_scales(pen):
	pen.line([(50, 12), (50, 80)], 6)
	pen.line([(16, 26), (84, 26)], 6)
	pen.circle(50, 15, 5, fill=255)
	pen.rect(30, 80, 70, 88, radius=2)
	pen.rect(38, 74, 62, 81, radius=2)
	for cx in (24, 76):
		pen.line([(cx, 27), (cx - 13, 56)], 3)
		pen.line([(cx, 27), (cx + 13, 56)], 3)
		pen.poly([(cx - 16, 56), (cx + 16, 56), (cx + 10, 64), (cx - 10, 64)])


def icon_coins(pen):
	pen.circle(36, 58, 26, fill=0, outline=255, width=6)
	pen.circle(36, 58, 17, outline=255, width=3)
	pen.circle(64, 42, 26, fill=0, outline=255, width=6)
	pen.circle(64, 42, 17, outline=255, width=3)
	# a crown on the coin in front
	pen.poly([(54, 48), (54, 36), (59, 41), (64, 32), (69, 41), (74, 36), (74, 48)])


def facade(pen, x0, x1, top, bottom, columns=4, pediment=True):
	w = x1 - x0
	if pediment:
		pen.poly([(x0 - 4, top + 14), (x0 + w / 2, top), (x1 + 4, top + 14)])
		pen.poly([(x0 + 8, top + 11), (x0 + w / 2, top + 5), (x1 - 8, top + 11)], fill=0)
	pen.rect(x0 - 2, top + 15, x1 + 2, top + 20)
	span = (x1 - x0 - 8) / (columns - 1)
	for i in range(columns):
		cx = x0 + 4 + span * i
		pen.rect(cx - 3.5, top + 22, cx + 3.5, bottom - 8)
	pen.rect(x0 - 4, bottom - 8, x1 + 4, bottom - 3)
	pen.rect(x0 - 8, bottom - 3, x1 + 8, bottom + 2)


def icon_facade(pen):
	facade(pen, 22, 78, 14, 84)


def icon_document(pen):
	pen.poly([(24, 10), (62, 10), (78, 26), (78, 90), (24, 90)], fill=0, outline=255, width=6)
	pen.line([(62, 10), (62, 26), (78, 26)], 5)
	for y in (40, 52, 64, 76):
		pen.line([(34, y), (68 if y != 76 else 54, y)], 5)


def icon_bank_coin(pen):
	facade(pen, 26, 74, 24, 86, columns=3)
	pen.circle(50, 12, 9, fill=0, outline=255, width=5)


def icon_progressivity(pen):
	shield(pen, 50, 10, 34, 88, fill=False)
	for i, h in enumerate((14, 24, 34)):
		x = 36 + i * 11
		pen.rect(x, 66 - h, x + 7, 66)


def person(pen, cx, cy, scale=1.0, crown=False):
	r = 9 * scale
	pen.circle(cx, cy - 16 * scale, r, fill=255, outline=None)
	pen.d.pieslice([*pen.p(cx - 18 * scale, cy - 2 * scale), *pen.p(cx + 18 * scale, cy + 34 * scale)], 180, 360, fill=255)
	if crown:
		y = cy - 16 * scale - r
		pen.poly([(cx - 10 * scale, y + 2), (cx - 10 * scale, y - 8 * scale), (cx - 5 * scale, y - 3 * scale), (cx, y - 11 * scale),
			(cx + 5 * scale, y - 3 * scale), (cx + 10 * scale, y - 8 * scale), (cx + 10 * scale, y + 2)])


def icon_person_one(pen):
	person(pen, 50, 52, 1.5)


def icon_person_two(pen):
	person(pen, 36, 56, 1.25)
	person(pen, 64, 56, 1.25)


def icon_person_crown(pen):
	person(pen, 50, 58, 1.45, crown=True)


def icon_book(pen):
	# an open book on a reading stand
	for side in (-1, 1):
		pts = [(50, 30), (50 + side * 40, 22), (50 + side * 40, 72), (50, 80)]
		pen.poly(pts, fill=0, outline=255, width=6)
		for i in range(3):
			y = 38 + i * 11
			pen.line([(50 + side * 10, y + 2), (50 + side * 32, y - 3)], 3)
	pen.line([(50, 30), (50, 80)], 5)
	pen.line([(30, 90), (70, 90)], 5)


def shield(pen, cx, top, half, bottom, fill=True):
	pts = []
	pts.append((cx - half, top))
	pts.append((cx + half, top))
	for i in range(0, 21):
		t = i / 20
		x = cx + half * (1 - t)
		y = top + (bottom - top) * (0.45 + 0.55 * math.sin(t * math.pi / 2))
		pts.append((x, y if t < 1 else bottom))
	for i in range(20, -1, -1):
		t = i / 20
		x = cx - half * (1 - t)
		y = top + (bottom - top) * (0.45 + 0.55 * math.sin(t * math.pi / 2))
		pts.append((x, y if t < 1 else bottom))
	pts = [pts[0], pts[1]] + pts[2:]
	pen.poly(pts, fill=0, outline=255, width=6)
	return pts


def icon_shield(pen):
	shield(pen, 50, 12, 32, 90)
	shield(pen, 50, 24, 20, 74)
	pen.line([(50, 32), (50, 66)], 5)
	pen.line([(36, 44), (64, 44)], 5)


def icon_aqueduct(pen):
	pen.rect(8, 30, 92, 38)
	for x0 in (12, 40, 68):
		pen.rect(x0, 38, x0 + 6, 86)
	for x0 in (18, 46, 74):
		pen.arc(x0 + 4, 52, 12, 14, 180, 360, 6)
	pen.rect(86, 38, 92, 86)
	pen.rect(4, 86, 96, 92)
	# the tower over the middle pier
	pen.rect(38, 10, 62, 30)
	for x in (38, 46, 54):
		pen.rect(x, 4, x + 6, 12)
	pen.rect(47, 16, 53, 26, fill=0)


def coin_face(pen, cx, cy, r):
	pen.circle(cx, cy, r, fill=0, outline=255, width=6)
	pen.circle(cx, cy, r - 8, outline=255, width=2.5)


def icon_coin_eagle(pen):
	coin_face(pen, 50, 52, 40)
	# a two-headed eagle, much simplified
	pen.poly([(50, 40), (30, 30), (24, 44), (36, 52), (26, 58), (40, 62), (50, 74), (60, 62), (74, 58), (64, 52), (76, 44), (70, 30)])
	pen.circle(40, 34, 4, fill=255, outline=None)
	pen.circle(60, 34, 4, fill=255, outline=None)
	pen.poly([(44, 26), (50, 18), (56, 26)])


def icon_coin_gem(pen):
	coin_face(pen, 50, 52, 40)
	pen.poly([(36, 46), (42, 36), (58, 36), (64, 46), (50, 68)], fill=0, outline=255, width=5)
	pen.line([(36, 46), (64, 46)], 4)
	pen.line([(42, 36), (46, 46), (50, 68)], 3)
	pen.line([(58, 36), (54, 46), (50, 68)], 3)


def icon_seal_scroll(pen):
	pen.rect(14, 20, 74, 80, fill=0, outline=255, width=6, radius=4)
	pen.circle(14, 26, 8, fill=255, outline=None)
	pen.circle(14, 74, 8, fill=255, outline=None)
	for y in (34, 46, 58):
		pen.line([(26, y), (60, y)], 4)
	pen.circle(72, 70, 16, fill=255, outline=None)
	pen.circle(72, 70, 9, fill=0, outline=None)
	pen.poly([(66, 82), (62, 96), (70, 90), (74, 98), (76, 84)])


def icon_shield_coin(pen):
	shield(pen, 50, 12, 32, 90)
	pen.circle(50, 46, 14, outline=255, width=5)
	pen.line([(50, 38), (50, 54)], 4)


def icon_factory(pen):
	pen.poly([(10, 88), (10, 50), (30, 36), (30, 50), (50, 36), (50, 50), (70, 36), (70, 88)], fill=0, outline=255, width=6)
	pen.rect(74, 14, 88, 88)
	for x in (18, 38, 56):
		pen.rect(x, 62, x + 8, 74)
	pen.rect(4, 86, 96, 92)
	pen.circle(80, 8, 4, fill=255, outline=None)


def icon_ornament(pen):
	for a in range(4):
		ang = a * math.pi / 2
		cx = 50 + math.cos(ang) * 18
		cy = 50 + math.sin(ang) * 18
		pen.circle(cx, cy, 15, outline=255, width=5)
	pen.circle(50, 50, 7, fill=255, outline=None)


def icon_swords(pen):
	for side in (-1, 1):
		x0, x1 = 50 - side * 34, 50 + side * 30
		pen.line([(x0, 86), (x1, 18)], 7)
		pen.poly([(x1 - 4, 18), (x1 + side * 4, 8), (x1 + side * 6, 22)])
		gx, gy = 50 - side * 22, 64
		pen.line([(gx - 9, gy - 6), (gx + 9, gy + 6)], 6)
		pen.circle(x0, 88, 5, fill=255, outline=None)


def icon_people(pen):
	person(pen, 26, 58, 0.95)
	person(pen, 74, 58, 0.95)
	person(pen, 50, 52, 1.25)


def icon_coins_doc(pen):
	pen.poly([(18, 8), (54, 8), (68, 22), (68, 74), (18, 74)], fill=0, outline=255, width=6)
	for y in (32, 44, 56):
		pen.line([(28, y), (56, y)], 4)
	coin_side(pen, 66, 74, 22, 7, 9)
	coin_side(pen, 66, 62, 22, 7, 9)


def icon_percent(pen):
	pen.circle(30, 30, 13, outline=255, width=7)
	pen.circle(70, 70, 13, outline=255, width=7)
	pen.line([(76, 18), (24, 82)], 8)


def icon_calendar(pen):
	pen.rect(12, 18, 88, 88, fill=0, outline=255, width=6, radius=6)
	pen.rect(12, 18, 88, 34, radius=6)
	for x in (30, 70):
		pen.rect(x - 4, 8, x + 4, 26, radius=2)
	for row in range(3):
		for col in range(4):
			pen.rect(22 + col * 16, 44 + row * 14, 30 + col * 16, 51 + row * 14)


def icon_crest(pen):
	for side in (-1, 1):
		for i in range(6):
			t = i / 5
			r = 32
			x = 50 + side * (math.sin(math.radians(20 + 140 * t)) * r)
			y = 54 - math.cos(math.radians(20 + 140 * t)) * r
			pen.ellipse(x - 6, y - 10, x + 6, y + 10, fill=255, outline=None)
	pen.arc(50, 54, 32, 32, 30, 150, 4)
	star = []
	for i in range(10):
		ang = -math.pi / 2 + i * math.pi / 5
		r = 16 if i % 2 == 0 else 7
		star.append((50 + math.cos(ang) * r, 46 + math.sin(ang) * r))
	pen.poly(star)


def icon_gavel(pen):
	# a gavel crossing a pen: the act of law
	pen.line([(24, 84), (66, 30)], 7)
	pen.poly([(54, 14), (82, 40), (90, 32), (62, 6)])
	pen.poly([(48, 22), (74, 48), (68, 54), (42, 28)])
	pen.line([(78, 86), (34, 30)], 4)
	pen.poly([(30, 22), (40, 26), (36, 34)])
	pen.rect(12, 88, 46, 94, radius=2)


def icon_bars(pen):
	for i, h in enumerate((30, 52, 40, 64)):
		x = 18 + i * 18
		pen.rect(x, 84 - h, x + 11, 84)
	pen.line([(10, 88), (90, 88)], 4)


def icon_line_chart(pen):
	pen.line([(12, 74), (34, 50), (52, 60), (86, 24)], 6)
	for x, y in ((12, 74), (34, 50), (52, 60), (86, 24)):
		pen.circle(x, y, 6, fill=255, outline=None)
	pen.line([(10, 88), (90, 88)], 4)


def icon_chevron_down(pen):
	pen.line([(22, 36), (50, 64), (78, 36)], 9)


def icon_chevron_left(pen):
	pen.line([(58, 22), (32, 50), (58, 78)], 9)
	pen.line([(80, 34), (64, 50), (80, 66)], 6)


def icon_shield_ceiling(pen):
	shield(pen, 50, 22, 30, 92)
	pen.line([(14, 10), (86, 10)], 6)
	pen.line([(50, 36), (50, 70)], 6)
	pen.line([(38, 48), (50, 36), (62, 48)], 6)


def icon_close(pen):
	pen.line([(24, 24), (76, 76)], 8)
	pen.line([(76, 24), (24, 76)], 8)


def icon_plus(pen):
	pen.line([(50, 20), (50, 80)], 9)
	pen.line([(20, 50), (80, 50)], 9)


def icon_minus(pen):
	pen.line([(20, 50), (80, 50)], 9)


def icon_dot(pen):
	pen.circle(50, 50, 40, fill=255, outline=None)


def icon_ring(pen):
	pen.circle(50, 50, 38, outline=255, width=12)


def icon_debt_coin(pen):
	coin_face(pen, 50, 52, 40)
	pen.line([(34, 52), (66, 52)], 8)


# The order of the cells; the game's finance_icon enum follows it.
ICONS = [
	('coin_stack', icon_coin_stack), ('scales', icon_scales), ('coins', icon_coins), ('facade', icon_facade),
	('document', icon_document), ('bank_coin', icon_bank_coin), ('progressivity', icon_progressivity),
	('person_one', icon_person_one), ('person_two', icon_person_two), ('person_crown', icon_person_crown),
	('book', icon_book), ('shield', icon_shield), ('aqueduct', icon_aqueduct), ('coin_eagle', icon_coin_eagle),
	('coin_gem', icon_coin_gem), ('seal_scroll', icon_seal_scroll), ('shield_coin', icon_shield_coin),
	('factory', icon_factory), ('ornament', icon_ornament), ('swords', icon_swords), ('people', icon_people),
	('coins_doc', icon_coins_doc), ('percent', icon_percent), ('calendar', icon_calendar), ('crest', icon_crest),
	('gavel', icon_gavel), ('bars', icon_bars), ('line_chart', icon_line_chart), ('chevron_down', icon_chevron_down),
	('chevron_left', icon_chevron_left), ('shield_ceiling', icon_shield_ceiling), ('close', icon_close),
	('plus', icon_plus), ('minus', icon_minus), ('dot', icon_dot), ('ring', icon_ring), ('debt_coin', icon_debt_coin),
]
# cells drawn plain white, for the game to tint
WHITE = {'dot', 'ring', 'chevron_down', 'close', 'plus', 'minus'}


def icons():
	"""katechon_fin_icons.png: one row of 96 px cells; gilded except the plain marks."""
	size = CELL * SS
	strip = Image.new('RGBA', (size * len(ICONS), size), (0, 0, 0, 0))
	for i, (name, draw) in enumerate(ICONS):
		pen = Pen(int(size * 0.84))
		draw(pen)
		mask = Image.new('L', (size, size), 0)
		off = (size - pen.size) // 2
		mask.paste(pen.mask, (off, off))
		if name in WHITE:
			cell = Image.new('RGBA', (size, size), (255, 255, 255, 0))
			cell.putalpha(mask)
		else:
			cell = gilt(mask)
		strip.alpha_composite(cell, (i * size, 0))
	done(strip, 'katechon_fin_icons.png')
	with open(os.path.join(HERE, 'finance_icons.txt'), 'w') as f:
		f.write('\n'.join(name for name, _ in ICONS) + '\n')


# ---- ornament ----

def scroll_corner(d, x, y, s, width, fill, flip_x=False, flip_y=False):
	"""An engraved corner: two curling scrolls running out along the edges, a bud between them."""
	def P(px, py):
		px = -px if flip_x else px
		py = -py if flip_y else py
		return (x + px * s, y + py * s)

	def curve(points, w):
		d.line([P(*p) for p in points], fill=fill, width=w, joint='curve')

	def spiral(cx, cy, r0, turns, start, w, sense):
		pts = []
		for i in range(60):
			t = i / 59
			a = start + sense * t * turns * 2 * math.pi
			r = r0 * (1 - 0.75 * t)
			pts.append((cx + math.cos(a) * r, cy + math.sin(a) * r))
		curve(pts, w)
	# along the top edge
	curve([(0.0, 0.0), (0.35, 0.02), (0.62, 0.08), (0.80, 0.18)], width)
	spiral(0.80, 0.28, 0.10, 0.85, -math.pi / 2, width, 1)
	curve([(0.18, 0.04), (0.42, 0.14), (0.50, 0.26)], max(1, width - 1))
	# along the left edge
	curve([(0.0, 0.0), (0.02, 0.35), (0.08, 0.62), (0.18, 0.80)], width)
	spiral(0.28, 0.80, 0.10, 0.85, math.pi, width, -1)
	curve([(0.04, 0.18), (0.14, 0.42), (0.26, 0.50)], max(1, width - 1))
	# the bud on the diagonal
	cx, cy = P(0.20, 0.20)
	r = 0.055 * s
	d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=fill)
	curve([(0.06, 0.06), (0.16, 0.16)], width)


def corner_strip():
	"""katechon_fin_corner.png: the engraved corner in its four turns, 64 px cells (top left, top right, bottom left, bottom right)."""
	cell = 64 * SS
	strip = Image.new('RGBA', (cell * 4, cell), (0, 0, 0, 0))
	for i, (fx, fy) in enumerate(((False, False), (True, False), (False, True), (True, True))):
		mask = Image.new('L', (cell, cell), 0)
		d = ImageDraw.Draw(mask)
		x = cell - 2 * SS if fx else 2 * SS
		y = cell - 2 * SS if fy else 2 * SS
		scroll_corner(d, x, y, cell * 0.92, int(2.2 * SS), 255, fx, fy)
		strip.alpha_composite(gilt(mask, shade=False), (i * cell, 0))
	done(strip, 'katechon_fin_corner.png')


def panel(name, w, h, corner, fill_top, fill_bottom, line, flourish, seed, glow=None, radius=3, inner_line=True, edge_alpha=255):
	"""A plate: a dark edge, one thin gilt line, a faint inner line, a lacquered fill, scrollwork in the corners."""
	img = canvas(w, h)
	d = ImageDraw.Draw(img)
	s = SS
	r = radius * s
	d.rounded_rectangle([0, 0, w * s - 1, h * s - 1], radius=r + s, fill=EDGE + (edge_alpha,))
	body = gradient((w * s - 4 * s, h * s - 4 * s), fill_top, fill_bottom)
	m = Image.new('L', body.size, 0)
	ImageDraw.Draw(m).rounded_rectangle([0, 0, body.size[0] - 1, body.size[1] - 1], radius=r, fill=255)
	img.paste(body, (2 * s, 2 * s), m)
	grain(img, (2 * s, 2 * s, w * s - 2 * s, h * s - 2 * s), 5, seed)
	if glow is not None:
		gl = Image.new('RGBA', img.size, (0, 0, 0, 0))
		ImageDraw.Draw(gl).ellipse([-w * s * 0.2, h * s * 0.45, w * s * 1.2, h * s * 1.6], fill=glow)
		gl = gl.filter(ImageFilter.GaussianBlur(h * s * 0.22))
		inside = Image.new('L', img.size, 0)
		inside.paste(m, (2 * s, 2 * s))
		gl.putalpha(ImageChops.multiply(gl.split()[3], inside))
		img.alpha_composite(gl)
	# the gilt line, and a faint light inside it
	d.rounded_rectangle([1 * s, 1 * s, w * s - 1 - 1 * s, h * s - 1 - 1 * s], radius=r, outline=line + (255,), width=int(1.6 * s))
	if inner_line:
		d.rounded_rectangle([int(3.2 * s), int(3.2 * s), w * s - 1 - int(3.2 * s), h * s - 1 - int(3.2 * s)], radius=max(0, r - 2 * s), outline=(255, 236, 200, 20), width=s)
		d.line([(r + 3 * s, int(3.6 * s)), (w * s - r - 3 * s, int(3.6 * s))], fill=(255, 240, 210, 30), width=s)
	if flourish:
		fd = ImageDraw.Draw(img)
		size = flourish * s
		for fx, fy in ((False, False), (True, False), (False, True), (True, True)):
			mask = Image.new('L', img.size, 0)
			md = ImageDraw.Draw(mask)
			x = (w * s - 4.5 * s) if fx else 4.5 * s
			y = (h * s - 4.5 * s) if fy else 4.5 * s
			scroll_corner(md, x, y, size, int(1.3 * s), 255, fx, fy)
			img.alpha_composite(gilt(mask, GOLD_LIGHT, GOLD_DIM, shade=False))
	flat = Image.new('RGBA', img.size, PAGE + (255,))
	flat.alpha_composite(img)
	done(flat, name)


def plates():
	# the large panels of the sheet: 160 x 160, corners 24 px
	panel('katechon_fin_panel.png', 160, 160, 24, (17, 28, 42), (10, 18, 30), (204, 166, 104), 20, 1)
	# the five figures: lighter at the head
	panel('katechon_fin_card.png', 160, 160, 24, (22, 33, 49), (11, 19, 31), (204, 166, 104), 18, 2)
	# an inset box inside a panel: no scrollwork, a quieter line; corners 10 px
	panel('katechon_fin_inset.png', 64, 64, 10, (19, 30, 44), (15, 25, 38), (58, 70, 86), 0, 3, radius=3, inner_line=False)
	# a row of a table under the mouse or chosen; corners 8 px
	panel('katechon_fin_row.png', 64, 32, 8, (23, 34, 49), (18, 28, 41), (44, 56, 72), 0, 4, radius=2, inner_line=False)
	# the gilt button and its quiet sibling; corners 12 px
	panel('katechon_fin_button.png', 96, 48, 12, (34, 40, 52), (18, 23, 33), (214, 178, 114), 7, 5, radius=4)
	panel('katechon_fin_button_hover.png', 96, 48, 12, (52, 54, 60), (28, 30, 38), (236, 204, 140), 7, 6, radius=4)
	panel('katechon_fin_tab.png', 96, 48, 12, (24, 32, 46), (14, 21, 32), (64, 76, 94), 0, 7, radius=4, inner_line=False)
	# the bar of the sheet's sections, and its lit section, with the violet light under it
	panel('katechon_fin_nav.png', 128, 48, 12, (24, 30, 44), (14, 19, 30), (204, 166, 104), 0, 8, radius=3)
	panel('katechon_fin_nav_on.png', 96, 48, 12, (54, 40, 70), (34, 24, 48), (214, 178, 114), 0, 9, radius=3,
		glow=(196, 120, 210, 150))


def flourish_strip():
	"""katechon_fin_flourish.png: the scroll at the ends of the section bar, 96 x 48 cells (left end, right end)."""
	w, h = 96 * SS, 48 * SS
	strip = Image.new('RGBA', (w * 2, h), (0, 0, 0, 0))
	for i, flip in enumerate((False, True)):
		mask = Image.new('L', (w, h), 0)
		d = ImageDraw.Draw(mask)
		def P(x, y):
			return ((1 - x) * w if flip else x * w, y * h)
		def curve(points, width):
			d.line([P(*p) for p in points], fill=255, width=width, joint='curve')
		# a long stem curling into a volute toward the bar, two leaves off it
		curve([(0.98, 0.5), (0.7, 0.5), (0.45, 0.47), (0.25, 0.38)], int(2.0 * SS))
		pts = []
		for k in range(60):
			t = k / 59
			a = math.pi * 0.2 + t * 1.7 * 2 * math.pi
			r = 0.16 * (1 - 0.8 * t)
			pts.append((0.2 + math.cos(a) * r * 0.5, 0.5 + math.sin(a) * r))
		curve(pts, int(1.8 * SS))
		curve([(0.62, 0.5), (0.52, 0.3), (0.4, 0.22)], int(1.4 * SS))
		curve([(0.62, 0.5), (0.52, 0.7), (0.4, 0.78)], int(1.4 * SS))
		cx, cy = P(0.82, 0.5)
		r = 0.06 * h
		d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=255)
		strip.alpha_composite(gilt(mask, shade=False), (i * w, 0))
	done(strip, 'katechon_fin_flourish.png')


def medallion():
	"""katechon_fin_medallion.png, 128 x 128: the round plate the icon of a figure sits in."""
	s = SS
	n = 128
	img = canvas(n, n)
	d = ImageDraw.Draw(img)
	c = n * s / 2
	shadow = Image.new('RGBA', img.size, (0, 0, 0, 0))
	ImageDraw.Draw(shadow).ellipse([8 * s, 10 * s, (n - 8) * s, (n - 6) * s], fill=(0, 0, 0, 170))
	img.alpha_composite(shadow.filter(ImageFilter.GaussianBlur(5 * s)))
	r = (n / 2 - 9) * s
	body = gradient(img.size, (24, 30, 42), (8, 12, 20))
	m = Image.new('L', img.size, 0)
	ImageDraw.Draw(m).ellipse([c - r, c - r, c + r, c + r], fill=255)
	img.paste(body, (0, 0), m)
	d.ellipse([c - r, c - r, c + r, c + r], outline=(120, 92, 56, 255), width=int(1.6 * s))
	r2 = r - 5 * s
	d.ellipse([c - r2, c - r2, c + r2, c + r2], outline=(255, 230, 180, 26), width=s)
	done(img, 'katechon_fin_medallion.png')


def knob():
	"""katechon_fin_knob.png, 64 x 64: the metal head of a slider."""
	s = SS
	n = 64
	img = canvas(n, n)
	c = n * s / 2
	shadow = Image.new('RGBA', img.size, (0, 0, 0, 0))
	ImageDraw.Draw(shadow).ellipse([10 * s, 13 * s, 54 * s, 57 * s], fill=(0, 0, 0, 190))
	img.alpha_composite(shadow.filter(ImageFilter.GaussianBlur(3 * s)))
	r = 21 * s
	body = Image.new('RGBA', img.size)
	px = body.load()
	for y in range(img.size[1]):
		for x in range(img.size[0]):
			dx, dy = (x - c) / r, (y - c) / r
			t = max(0.0, min(1.0, 0.5 + 0.55 * (dy * 0.8 - dx * 0.25)))
			light = 1.0 - t
			hl = max(0.0, 1.0 - ((dx + 0.35) ** 2 + (dy + 0.45) ** 2) * 3.0)
			col = [GOLD_DEEP[i] + (GOLD_LIGHT[i] - GOLD_DEEP[i]) * light for i in range(3)]
			col = [min(255, v + hl * 70) for v in col]
			px[x, y] = (int(col[0]), int(col[1]), int(col[2]), 255)
	m = Image.new('L', img.size, 0)
	ImageDraw.Draw(m).ellipse([c - r, c - r, c + r, c + r], fill=255)
	img.paste(body, (0, 0), m)
	d = ImageDraw.Draw(img)
	d.ellipse([c - r, c - r, c + r, c + r], outline=(92, 66, 34, 255), width=int(1.4 * s))
	r2 = r - 6 * s
	d.ellipse([c - r2, c - r2, c + r2, c + r2], outline=(120, 88, 48, 150), width=s)
	done(img, 'katechon_fin_knob.png')


def soft_dot():
	"""katechon_fin_glow.png, 64 x 64: a soft white light, for the glow of what is lit."""
	n = 64 * SS
	img = Image.new('RGBA', (n, n), (255, 255, 255, 0))
	m = Image.new('L', (n, n), 0)
	ImageDraw.Draw(m).ellipse([n * 0.22, n * 0.22, n * 0.78, n * 0.78], fill=255)
	img.putalpha(m.filter(ImageFilter.GaussianBlur(n * 0.12)))
	done(img, 'katechon_fin_glow.png')


# ---- the backdrop ----

def pine(d, rnd, x, base, h, w, colour):
	"""A fir in silhouette: ragged tiers of branches round a thin trunk, never two alike."""
	tiers = rnd.randint(9, 14)
	d.line([(x, base + 4), (x, base - h)], fill=colour, width=max(2, int(w * 0.05)))
	for i in range(tiers):
		t = i / tiers
		y = base - h * (0.12 + 0.88 * t)
		half = w * 0.5 * (1 - t) ** 0.9 * rnd.uniform(0.7, 1.15)
		droop = h * rnd.uniform(0.05, 0.09)
		for side in (-1, 1):
			reach = half * rnd.uniform(0.75, 1.1)
			tip = (x + side * reach, y + droop)
			d.polygon([(x, y - droop * 0.6), tip, (x + side * reach * 0.5, y + droop * 0.35), (x, y + droop * 0.5)], fill=colour)
			for k in range(rnd.randint(1, 3)):
				u = rnd.uniform(0.3, 0.9)
				bx, by = x + side * reach * u, y + droop * u
				d.line([(bx, by), (bx + side * rnd.uniform(3, 9), by + rnd.uniform(2, 7))], fill=colour, width=2)
	d.polygon([(x - 2, base - h * 0.92), (x, base - h * 1.04), (x + 2, base - h * 0.92)], fill=colour)


def header(reference):
	"""katechon_fin_header.png, 3200 x 240: the header band; a dark forest, then the painted treasury."""
	W, H = 3200, 240
	img = gradient((W, H), (30, 27, 42), (16, 15, 24)).convert('RGBA')
	rnd = random.Random(11)
	# a haze of light behind the trees
	haze = Image.new('RGBA', (W, H), (0, 0, 0, 0))
	ImageDraw.Draw(haze).ellipse([W * 0.25, -H * 0.6, W * 0.7, H * 0.9], fill=(70, 58, 84, 90))
	img.alpha_composite(haze.filter(ImageFilter.GaussianBlur(120)))
	for colour, scale, blur, gap in (((33, 30, 45, 255), 0.7, 4.0, (26, 60)), ((26, 24, 37, 255), 0.95, 2.2, (40, 110)), ((19, 18, 28, 255), 1.2, 1.0, (70, 190))):
		trees = Image.new('RGBA', (W, H), (0, 0, 0, 0))
		td = ImageDraw.Draw(trees)
		x = rnd.uniform(-60, 20)
		while x < W * 0.76:
			h = rnd.uniform(120, 240) * scale
			pine(td, rnd, x, H + 10 - rnd.uniform(0, 30) * (1.3 - scale), h, h * rnd.uniform(0.30, 0.42), colour)
			x += rnd.uniform(*gap) * scale
		img.alpha_composite(trees.filter(ImageFilter.GaussianBlur(blur)))
		# mist between the rows of trees
		mist = gradient((W, H), (30, 27, 42), (36, 32, 48)).convert('RGBA')
		mist.putalpha(gradient((W, H), (0, 0, 0), (70, 70, 70)).convert('L'))
		img.alpha_composite(mist)
	# columns at the edge of the forest, where the hall begins
	cols = Image.new('RGBA', (W, H), (0, 0, 0, 0))
	cd = ImageDraw.Draw(cols)
	for i, x in enumerate(range(1900, 2300, 95)):
		cd.rectangle([x, 20, x + 34, H], fill=(28, 25, 34, 220))
		for k in range(5):
			cd.line([(x + 5 + k * 6, 26), (x + 5 + k * 6, H)], fill=(40, 36, 46, 200), width=2)
		cd.rectangle([x - 8, 10, x + 42, 24], fill=(32, 28, 38, 230))
	cols = cols.filter(ImageFilter.GaussianBlur(1.2))
	fade = Image.new('L', (W, H), 0)
	fd = ImageDraw.Draw(fade)
	for x in range(1800, 2400):
		fd.line([(x, 0), (x, H)], fill=int(255 * min(1, (x - 1800) / 300) * min(1, (2400 - x) / 200)))
	cols.putalpha(ImageChops.multiply(cols.split()[3], fade))
	img.alpha_composite(cols)
	hall = os.path.join(HERE, 'finance_hall.png')
	if reference and os.path.exists(reference):
		# the painted part of the reference design's band: the hall, the archive, the desk
		Image.open(reference).convert('RGB').crop((968, 62, 1503, 178)).save(hall, optimize=True)
	if os.path.exists(hall):
		art = Image.open(hall).convert('RGB')
		art = art.resize((int(art.size[0] * H / art.size[1]), H), Image.LANCZOS)
		art = art.filter(ImageFilter.UnsharpMask(radius=2, percent=60, threshold=2)).convert('RGBA')
		mask = Image.new('L', art.size, 255)
		md = ImageDraw.Draw(mask)
		ramp = int(art.size[0] * 0.28)
		for x in range(ramp):
			md.line([(x, 0), (x, art.size[1])], fill=int(255 * (x / ramp) ** 1.4))
		art.putalpha(mask)
		img.alpha_composite(art, (W - art.size[0], 0))
	grain(img, (0, 0, W, H), 6, 21, step=1)
	img = img.convert('RGB')
	img.save(os.path.join(ASSETS, 'katechon_fin_header.png'), optimize=True)
	print('katechon_fin_header.png', img.size)


def page_grain():
	"""katechon_fin_damask.png, 256 x 256, tiling: the faint pattern of the hall's wall behind the panels."""
	n = 256 * SS
	img = Image.new('RGBA', (n, n), (0, 0, 0, 0))
	m = Image.new('L', (n, n), 0)
	d = ImageDraw.Draw(m)
	for cx, cy in ((0, 0), (n, 0), (0, n), (n, n), (n / 2, n / 2)):
		for k in range(4):
			a = k * math.pi / 2
			x = cx + math.cos(a) * n * 0.16
			y = cy + math.sin(a) * n * 0.16
			d.ellipse([x - n * 0.11, y - n * 0.07, x + n * 0.11, y + n * 0.07] if k % 2 == 0 else [x - n * 0.07, y - n * 0.11, x + n * 0.07, y + n * 0.11], outline=255, width=3 * SS)
		d.ellipse([cx - n * 0.04, cy - n * 0.04, cx + n * 0.04, cy + n * 0.04], outline=255, width=3 * SS)
	for cx, cy in ((n / 2, 0), (0, n / 2), (n, n / 2), (n / 2, n)):
		d.line([(cx - n * 0.06, cy), (cx + n * 0.06, cy)], fill=255, width=2 * SS)
		d.line([(cx, cy - n * 0.06), (cx, cy + n * 0.06)], fill=255, width=2 * SS)
	img = Image.new('RGBA', (n, n), (255, 232, 200, 0))
	img.putalpha(m.filter(ImageFilter.GaussianBlur(2 * SS)).point(lambda v: int(v * 0.06)))
	done(img, 'katechon_fin_damask.png')


if __name__ == '__main__':
	reference = sys.argv[1] if len(sys.argv) > 1 else None
	icons()
	plates()
	corner_strip()
	flourish_strip()
	medallion()
	knob()
	soft_dot()
	page_grain()
	header(reference)
