#!/bin/sh
# Rebuilds the KATECHON 2010 scenario mod from public data.
# Works in build/katechon-data (downloads and intermediates) and writes
# <Victoria 2>/mod/katechon2010 plus katechon2010.mod. The Victoria 2 path is
# set at the top of the Python scripts.
set -e
here="$(cd "$(dirname "$0")" && pwd)"
work="$here/../../build/katechon-data"
mkdir -p "$work" && cd "$work"
cp "$here"/*.py .
base=https://raw.githubusercontent.com/nvkelso/natural-earth-vector/master/geojson
for f in ne_10m_admin_0_countries ne_10m_admin_1_states_provinces ne_110m_land ne_10m_populated_places_simple \
	ne_10m_bathymetry_K_200 ne_10m_bathymetry_J_1000 ne_10m_bathymetry_I_2000 ne_10m_bathymetry_H_3000 ne_10m_bathymetry_G_4000 ne_10m_bathymetry_F_5000; do
	[ -s "$f.geojson" ] || curl -sL -o "$f.geojson" "$base/$f.geojson"
done
for ind in SP.POP.TOTL NY.GDP.MKTP.CD SP.RUR.TOTL.ZS; do
	[ -s "wb_$ind.json" ] || curl -sL -o "wb_$ind.json" "https://api.worldbank.org/v2/country/all/indicator/$ind?date=2010&format=json&per_page=400"
done
python3 calib.py         # land mask of the base map, rough projection fit
python3 warp.py          # control-point warp from city names
python3 build_map.py     # 2010 first-level regions rasterised onto the map
python3 finalize_map.py  # province numbering, provinces.bmp, definition.csv
python3 build_mod.py     # countries, history, population, scenario tables
python3 build_sea.py     # the sea chart: map/terrain/colormap_water.dds
python3 build_flags.py   # the flags of 2010: gfx/flags
python3 build_relief.py  # the relief of the land: map/terrain/colormap.dds
