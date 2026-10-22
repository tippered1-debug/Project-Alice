"""Draws the art of the KATECHON diplomacy screens into assets/.

Lacquered panels in a bronze frame, a parchment header, medallions, the gold
button, the plates of a deal's items and a set of line icons. Everything is
drawn four times larger and reduced, so that edges come out smooth. The plates
are cut by the game in nine pieces (corner sizes are noted below), so they
stretch without bending their corners.

    python3 tools/katechon_ui/build_diplomacy.py
"""
import math
import os
import random

from PIL import Image, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
ASSETS = os.path.normpath(os.path.join(HERE, '..', '..', 'assets'))
SS = 4  # supersampling

BRONZE = (185, 151, 90)
BRONZE_DIM = (110, 91, 58)
BRONZE_LIGHT = (232, 205, 140)
EDGE = (11, 15, 20)
LACQUER_TOP = (36, 47, 61)
LACQUER = (27, 36, 48)
LACQUER_DEEP = (17, 24, 33)


def canvas(w, h):
	return Image.new('RGBA', (w * SS, h * SS), (0, 0, 0, 0))


def done(img, name):
	w, h = img.size
	img = img.resize((w // SS, h // SS), Image.LANCZOS)
	img.save(os.path.join(ASSETS, name))
	print(name, img.size)


def vertical(img, box, top, bottom, radius=0):
	"""A vertical gradient inside a (rounded) box."""
	x0, y0, x1, y1 = box
	grad = Image.new('RGBA', (x1 - x0, y1 - y0))
	gd = ImageDraw.Draw(grad)
	for y in range(y1 - y0):
		t = y / max(1, y1 - y0 - 1)
		c = tuple(int(top[i] + (bottom[i] - top[i]) * t) for i in range(3)) + (255,)
		gd.line([(0, y), (x1 - x0, y)], fill=c)
	mask = Image.new('L', grad.size, 0)
	ImageDraw.Draw(mask).rounded_rectangle([0, 0, grad.size[0] - 1, grad.size[1] - 1], radius=radius, fill=255)
	img.paste(grad, (x0, y0), mask)


def noise(img, box, amount, seed, radius=0):
	"""Faint grain, so that a surface does not look printed."""
	rnd = random.Random(seed)
	x0, y0, x1, y1 = box
	layer = Image.new('RGBA', (x1 - x0, y1 - y0), (0, 0, 0, 0))
	px = layer.load()
	for y in range(0, y1 - y0, SS):
		for x in range(0, x1 - x0, SS):
			v = rnd.randint(-amount, amount)
			c = (255, 255, 255, v) if v > 0 else (0, 0, 0, -v)
			for dy in range(SS):
				for dx in range(SS):
					if x + dx < x1 - x0 and y + dy < y1 - y0:
						px[x + dx, y + dy] = c
	mask = Image.new('L', layer.size, 0)
	ImageDraw.Draw(mask).rounded_rectangle([0, 0, layer.size[0] - 1, layer.size[1] - 1], radius=radius, fill=255)
	img.alpha_composite(Image.composite(layer, Image.new('RGBA', layer.size, (0, 0, 0, 0)), mask), (x0, y0))


def panel():
	"""katechon_dip_panel.png, 128 x 128, corners 20 px: the lacquered body inside one bronze line."""
	img = canvas(128, 128)
	d = ImageDraw.Draw(img)
	s = SS
	d.rectangle([0, 0, 128 * s - 1, 128 * s - 1], fill=EDGE + (255,))
	vertical(img, (1 * s, 1 * s, 127 * s, 127 * s), (200, 166, 104), BRONZE_DIM)
	vertical(img, (3 * s, 3 * s, 125 * s, 125 * s), LACQUER_TOP, LACQUER_DEEP)
	noise(img, (3 * s, 3 * s, 125 * s, 125 * s), 6, 1)
	d.line([(3 * s, 3 * s), (125 * s, 3 * s)], fill=(255, 255, 255, 26), width=s)
	done(img, 'katechon_dip_panel.png')


def diamond():
	"""katechon_dip_diamond.png, 32 x 32: the lozenge that holds a frame's corner."""
	img = canvas(32, 32)
	d = ImageDraw.Draw(img)
	s = SS
	c = 16 * s
	d.polygon([(c, 1 * s), (31 * s, c), (c, 31 * s), (1 * s, c)], fill=EDGE + (255,))
	d.polygon([(c, 3 * s), (29 * s, c), (c, 29 * s), (3 * s, c)], fill=BRONZE + (255,))
	d.polygon([(c, 7 * s), (25 * s, c), (c, 25 * s), (7 * s, c)], fill=LACQUER_DEEP + (255,))
	d.polygon([(c, 11 * s), (21 * s, c), (c, 21 * s), (11 * s, c)], fill=BRONZE_LIGHT + (255,))
	done(img, 'katechon_dip_diamond.png')


def header():
	"""katechon_dip_header.png, 128 x 48, corners 12 px: a parchment strip with a bronze edge below."""
	img = canvas(128, 48)
	s = SS
	vertical(img, (0, 0, 128 * s, 44 * s), (241, 233, 212), (214, 199, 163))
	noise(img, (0, 0, 128 * s, 44 * s), 10, 2)
	d = ImageDraw.Draw(img)
	vertical(img, (0, 44 * s, 128 * s, 48 * s), BRONZE, BRONZE_DIM)
	done(img, 'katechon_dip_header.png')


def plate(name, fill_top, fill_bottom, border, border_width, glow=False):
	"""An item's plate, 128 x 64, corners 16 px."""
	img = canvas(128, 64)
	s = SS
	d = ImageDraw.Draw(img)
	r = 9 * s
	d.rounded_rectangle([0, 0, 128 * s - 1, 64 * s - 1], radius=r, fill=border + (255,))
	inner = border_width * s
	vertical(img, (inner, inner, 128 * s - inner, 64 * s - inner), fill_top, fill_bottom, radius=r - inner)
	noise(img, (inner, inner, 128 * s - inner, 64 * s - inner), 5, 3, radius=r - inner)
	if glow:
		d.rounded_rectangle([inner, inner, 128 * s - inner - 1, 64 * s - inner - 1], radius=r - inner, outline=(255, 230, 170, 40), width=s)
	done(img, name)


def button(name, top, mid, bottom, edge):
	"""The gold button, 128 x 64, corners 16 px."""
	img = canvas(128, 64)
	s = SS
	d = ImageDraw.Draw(img)
	r = 10 * s
	d.rounded_rectangle([0, 3 * s, 128 * s - 1, 64 * s - 1], radius=r, fill=edge + (255,))
	vertical(img, (0, 0, 128 * s, 30 * s), top, mid, radius=r)
	vertical(img, (0, 20 * s, 128 * s, 60 * s), mid, bottom, radius=r)
	d.rounded_rectangle([0, 0, 128 * s - 1, 60 * s - 1], radius=r, outline=(255, 246, 210, 120), width=s)
	d.line([(r, 2 * s), (128 * s - r, 2 * s)], fill=(255, 255, 255, 110), width=s)
	done(img, name)


def chip():
	"""katechon_dip_chip.png, 64 x 32, corners 16 px: a pill."""
	img = canvas(64, 32)
	s = SS
	d = ImageDraw.Draw(img)
	d.rounded_rectangle([0, 0, 64 * s - 1, 32 * s - 1], radius=16 * s, fill=(58, 70, 85, 255))
	d.rounded_rectangle([s * 2, s * 2, 62 * s - 1, 30 * s - 1], radius=14 * s, fill=(15, 21, 28, 255))
	done(img, 'katechon_dip_chip.png')


def medals():
	"""katechon_dip_medal.png, two cells of 96 x 96: a medallion, then the same lit."""
	img = canvas(192, 96)
	s = SS
	d = ImageDraw.Draw(img)
	for cell, ring in enumerate((BRONZE_DIM, BRONZE_LIGHT)):
		ox = cell * 96 * s
		c = 48 * s
		d.ellipse([ox + 2 * s, 2 * s, ox + 94 * s, 94 * s], fill=EDGE + (255,))
		d.ellipse([ox + 6 * s, 6 * s, ox + 90 * s, 90 * s], fill=ring + (255,))
		d.ellipse([ox + 11 * s, 11 * s, ox + 85 * s, 85 * s], fill=EDGE + (255,))
		# the face: lighter towards the top left
		face = Image.new('RGBA', (70 * s, 70 * s))
		fd = ImageDraw.Draw(face)
		top = (66, 80, 98) if cell == 0 else (92, 82, 58)
		bottom = (24, 32, 42) if cell == 0 else (40, 33, 20)
		for i in range(70 * s, 0, -s):
			t = 1 - i / (70 * s)
			col = tuple(int(top[k] + (bottom[k] - top[k]) * t) for k in range(3)) + (255,)
			fd.ellipse([35 * s - i // 2 - 6 * s * (1 - t), 35 * s - i // 2 - 6 * s * (1 - t), 35 * s + i // 2, 35 * s + i // 2], fill=col)
		mask = Image.new('L', face.size, 0)
		ImageDraw.Draw(mask).ellipse([0, 0, 70 * s - 1, 70 * s - 1], fill=255)
		img.paste(face, (ox + 13 * s, 13 * s), mask)
		d.arc([ox + 14 * s, 14 * s, ox + 82 * s, 82 * s], 200, 320, fill=(255, 255, 255, 60), width=2 * s)
	done(img, 'katechon_dip_medal.png')


# ---- icons: white lines on clear, tinted by the game ----
ICONS = ['shield', 'passage', 'scales', 'coin', 'heart', 'peace', 'swords', 'eye', 'scroll', 'quill', 'people', 'star', 'lock', 'envelope', 'check', 'cross']


def icons():
	"""katechon_dip_icons.png, cells of 64 x 64 in the order of ICONS."""
	n = len(ICONS)
	img = canvas(64 * n, 64)
	d = ImageDraw.Draw(img)
	s = SS
	W = (255, 255, 255, 255)
	lw = 4 * s

	def P(i, x, y):
		return (i * 64 * s + x * s, y * s)

	for i, name in enumerate(ICONS):
		if name == 'shield':
			pts = [P(i, 32, 8), P(i, 52, 15), P(i, 52, 30), P(i, 47, 44), P(i, 32, 56), P(i, 17, 44), P(i, 12, 30), P(i, 12, 15)]
			d.line(pts + [pts[0]], fill=W, width=lw, joint='curve')
			d.line([P(i, 32, 16), P(i, 32, 48)], fill=W, width=3 * s)
		elif name == 'passage':
			d.line([P(i, 10, 24), P(i, 50, 24)], fill=W, width=lw)
			d.line([P(i, 40, 14), P(i, 51, 24), P(i, 40, 34)], fill=W, width=lw, joint='curve')
			d.line([P(i, 54, 42), P(i, 14, 42)], fill=W, width=lw)
			d.line([P(i, 24, 32), P(i, 13, 42), P(i, 24, 52)], fill=W, width=lw, joint='curve')
		elif name == 'scales':
			d.line([P(i, 32, 10), P(i, 32, 52)], fill=W, width=lw)
			d.line([P(i, 14, 18), P(i, 50, 18)], fill=W, width=lw)
			d.line([P(i, 22, 54), P(i, 42, 54)], fill=W, width=lw)
			for cx in (14, 50):
				d.line([P(i, cx, 18), P(i, cx - 8, 36), P(i, cx + 8, 36), P(i, cx, 18)], fill=W, width=3 * s, joint='curve')
				d.arc([i * 64 * s + (cx - 9) * s, 30 * s, i * 64 * s + (cx + 9) * s, 44 * s], 0, 180, fill=W, width=3 * s)
		elif name == 'coin':
			d.ellipse([i * 64 * s + 10 * s, 10 * s, i * 64 * s + 54 * s, 54 * s], outline=W, width=lw)
			d.line([P(i, 32, 18), P(i, 32, 46)], fill=W, width=3 * s)
			d.arc([i * 64 * s + 22 * s, 20 * s, i * 64 * s + 42 * s, 34 * s], 90, 270, fill=W, width=3 * s)
			d.arc([i * 64 * s + 22 * s, 30 * s, i * 64 * s + 42 * s, 44 * s], 270, 90, fill=W, width=3 * s)
		elif name == 'heart':
			pts = []
			for k in range(0, 361, 6):
				t = math.radians(k)
				x = 16 * math.sin(t) ** 3
				y = -(13 * math.cos(t) - 5 * math.cos(2 * t) - 2 * math.cos(3 * t) - math.cos(4 * t))
				pts.append(P(i, 32 + x * 1.3, 32 + y * 1.3))
			d.line(pts, fill=W, width=lw, joint='curve')
		elif name == 'peace':
			d.line([P(i, 14, 52), P(i, 50, 14)], fill=W, width=lw)
			for t, side in ((0.3, 1), (0.45, -1), (0.6, 1), (0.75, -1)):
				bx = 14 + (50 - 14) * t
				by = 52 + (14 - 52) * t
				d.ellipse([i * 64 * s + (bx + side * 4 - 6) * s, (by - side * 4 - 3) * s, i * 64 * s + (bx + side * 4 + 6) * s, (by - side * 4 + 3) * s], outline=W, width=3 * s)
		elif name == 'swords':
			d.line([P(i, 12, 12), P(i, 50, 50)], fill=W, width=lw)
			d.line([P(i, 52, 12), P(i, 14, 50)], fill=W, width=lw)
			d.line([P(i, 38, 46), P(i, 46, 38)], fill=W, width=lw)
			d.line([P(i, 18, 38), P(i, 26, 46)], fill=W, width=lw)
		elif name == 'eye':
			d.arc([i * 64 * s + 8 * s, 16 * s, i * 64 * s + 56 * s, 60 * s], 200, 340, fill=W, width=lw)
			d.arc([i * 64 * s + 8 * s, 4 * s, i * 64 * s + 56 * s, 48 * s], 20, 160, fill=W, width=lw)
			d.ellipse([i * 64 * s + 25 * s, 25 * s, i * 64 * s + 39 * s, 39 * s], outline=W, width=lw)
		elif name == 'scroll':
			d.rectangle([i * 64 * s + 16 * s, 12 * s, i * 64 * s + 48 * s, 52 * s], outline=W, width=lw)
			for y in (22, 30, 38):
				d.line([P(i, 22, y), P(i, 42, y)], fill=W, width=3 * s)
		elif name == 'quill':
			d.line([P(i, 14, 52), P(i, 46, 14)], fill=W, width=3 * s)
			d.line([P(i, 46, 14), P(i, 52, 22), P(i, 30, 40), P(i, 22, 36), P(i, 46, 14)], fill=W, width=3 * s, joint='curve')
			d.line([P(i, 10, 56), P(i, 22, 56)], fill=W, width=3 * s)
		elif name == 'people':
			for cx in (22, 42):
				d.ellipse([i * 64 * s + (cx - 7) * s, 12 * s, i * 64 * s + (cx + 7) * s, 26 * s], outline=W, width=3 * s)
				d.arc([i * 64 * s + (cx - 13) * s, 30 * s, i * 64 * s + (cx + 13) * s, 62 * s], 180, 360, fill=W, width=3 * s)
		elif name == 'star':
			pts = []
			for k in range(11):
				r = 22 if k % 2 == 0 else 9
				a = math.radians(-90 + k * 36)
				pts.append(P(i, 32 + r * math.cos(a), 34 + r * math.sin(a)))
			d.line(pts, fill=W, width=3 * s, joint='curve')
		elif name == 'lock':
			d.rectangle([i * 64 * s + 16 * s, 28 * s, i * 64 * s + 48 * s, 54 * s], outline=W, width=lw)
			d.arc([i * 64 * s + 22 * s, 10 * s, i * 64 * s + 42 * s, 40 * s], 180, 360, fill=W, width=lw)
		elif name == 'check':
			d.line([P(i, 12, 34), P(i, 26, 48), P(i, 52, 18)], fill=W, width=6 * s, joint='curve')
		elif name == 'cross':
			d.line([P(i, 16, 16), P(i, 48, 48)], fill=W, width=6 * s)
			d.line([P(i, 48, 16), P(i, 16, 48)], fill=W, width=6 * s)
		elif name == 'envelope':
			d.rectangle([i * 64 * s + 10 * s, 16 * s, i * 64 * s + 54 * s, 48 * s], outline=W, width=lw)
			d.line([P(i, 10, 16), P(i, 32, 34), P(i, 54, 16)], fill=W, width=3 * s, joint='curve')
	done(img, 'katechon_dip_icons.png')


if __name__ == '__main__':
	panel()
	diamond()
	header()
	plate('katechon_dip_item.png', (20, 27, 36), (15, 21, 28), (44, 56, 70), 2)
	plate('katechon_dip_item_on.png', (46, 44, 34), (29, 28, 22), BRONZE, 3, glow=True)
	button('katechon_dip_button.png', (240, 214, 150), (199, 158, 86), (160, 118, 52), (94, 68, 32))
	button('katechon_dip_button_hover.png', (252, 230, 176), (220, 180, 104), (182, 138, 64), (110, 80, 36))
	chip()
	medals()
	icons()
