#!/usr/bin/env python3
"""Build assets/svg_katechon: the dark-and-gold skin for the templated windows.

The templated windows (settings, save, budget, ...) are drawn from the SVG
backgrounds and watercolour textures in assets/svg. This script derives the
KATECHON versions from them, so the two sets never drift apart by hand.
Run it from the repository root after changing anything in assets/svg.
"""

from pathlib import Path
import shutil

import numpy as np
from PIL import Image

SOURCE = Path("assets/svg")
TARGET = Path("assets/svg_katechon")

# Paper, ink and pencil of the old skin -> panel, gold and dim gold.
SVG_COLOURS = {
	"#fef7f0": "#0b0e15",
	"#32291f": "#b8944e",
	"#847d73": "#6f5a33",
	"#9b2226": "#8a2a2a",
}

# Each watercolour keeps its brushwork but takes a new base colour.
TEXTURES = {
	"lbwc.png": (30, 36, 50),
	"mbwc.png": (40, 50, 70),
	"dbwc.png": (16, 20, 30),
	"owc.png": (122, 96, 48),
	"mowc.png": (100, 76, 36),
	"dowc.png": (78, 56, 24),
	"rwc.png": (110, 30, 30),
	"drwc.png": (80, 22, 24),
	"mcwc.png": (22, 26, 36),
	"blue_watercolor.png": (14, 18, 27),
}

EMBLEM = """<svg xmlns="http://www.w3.org/2000/svg" width="512" height="512" viewBox="0 0 512 512">
<circle cx="256" cy="256" r="236" fill="#0b0e15" stroke="#b8944e" stroke-width="10"/>
<circle cx="256" cy="256" r="214" fill="none" stroke="#6f5a33" stroke-width="3"/>
<path fill="#b8944e" d="M244 96h24v72h70v24h-70v64h46v22h-46v138h-24V278h-46v-22h46v-64h-70v-24h70z"/>
<path fill="#b8944e" d="M256 78l12 14-12 14-12-14zM256 402l12 14-12 14-12-14zM150 180l14-12 14 12-14 12zM334 180l14-12 14 12-14 12z"/>
</svg>
"""


def retint(path: Path, base: tuple[int, int, int]) -> Image.Image:
	image = Image.open(path).convert("RGBA")
	pixels = np.asarray(image).astype(np.float32)
	luminance = pixels[..., :3].mean(axis=2)
	variation = (luminance - luminance.mean()) / 255.0
	out = np.empty_like(pixels)
	for channel in range(3):
		out[..., channel] = np.clip(base[channel] * (1.0 + 1.4 * variation), 0, 255)
	out[..., 3] = pixels[..., 3]
	return Image.fromarray(out.astype(np.uint8), "RGBA")


def main() -> None:
	if TARGET.exists():
		shutil.rmtree(TARGET)
	TARGET.mkdir(parents=True)
	for source in sorted(SOURCE.iterdir()):
		target = TARGET / source.name
		if source.name == "svglogo2.svg":
			target.write_text(EMBLEM)
		elif source.name in TEXTURES:
			retint(source, TEXTURES[source.name]).save(target)
		elif source.name == "printbrush.png":
			# The old skin roughened every line with this mask; the new one draws them clean.
			size = Image.open(source).size
			Image.new("RGBA", size, (255, 255, 255, 255)).save(target)
		elif source.suffix == ".asvg":
			text = source.read_text()
			for old, new in SVG_COLOURS.items():
				text = text.replace(old, new).replace(old.upper(), new)
			target.write_text(text)
		else:
			shutil.copyfile(source, target)


if __name__ == "__main__":
	main()
