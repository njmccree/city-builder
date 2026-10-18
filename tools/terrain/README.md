# tools/terrain: the offline terrain pipeline

Builds **map packages** (docs/architecture.md §15.2) from open datasets. The game never runs this; it only reads
the package. Real packages are built on the PC; tests and cloud sessions use the synthetic package from
`citysim_mapgen --synthetic` (sim/tools/README.md).

Python 3.11 or newer. Dependencies are pinned in `requirements.txt` (numpy, rasterio, pyproj, shapely,
zstandard, xxhash, requests, pyshp, pytest).

## Setup

Linux / macOS:

```
python3 -m venv .venv
. .venv/bin/activate
pip install -r tools/terrain/requirements.txt
pip install -e tools/terrain --no-deps
```

Windows (PowerShell, from the repository root):

```
py -3.12 -m venv .venv
.venv\Scripts\Activate.ps1
pip install -r tools\terrain\requirements.txt
pip install -e tools\terrain --no-deps
```

`rasterio`, `pyproj` and `shapely` ship binary wheels for Windows and Linux, so no GDAL install is needed.

## Run

```
python -m terrain.build_map tools/terrain/maps/<map>/map.json [--out DIR] [--steps a,b,c] [--offline]
```

- `map.json` needs `map_id` and `package` (the folder under `maps/`); `mod` (default `base`) and `sources` are
  optional. A source is `{ "dataset", "name", "url", "size"?, "sha256"? }`.
- `--out` defaults to `game/content/<mod>/maps/<package>` (gitignored; architecture §15.2).
- `corrections` (optional, in `map.json`): GeoJSON overlay files, relative to `map.json`, applied in this order by the
  `corrections` step. See the docstring of `terrain/corrections.py` for the operations (`set_water`, `set_land`,
  `elevation_offset`, `flatten_to`, `remove_river`, `river_path`, `fill_reservoir`); every feature needs `source` and
  `confidence`. `bounds` (`min_lon`, `min_lat`, `max_lon`, `max_lat`) and optionally `projection` (`lat_0`, `lon_0`)
  give the projection.
- `layers`, `deposits`, `fertility_overrides` (optional, in `map.json`): parameter overrides for the layer rules, and
  GeoJSON files (relative to `map.json`) of authored deposits (`resource` + `richness`, or `geology`) and fertility
  overrides (`mode` set/add/mul + `value`, optional `feather_m`). Every feature needs `source` and `confidence`. The
  rules (ancient-correct: modern cropland does not count as fertile) are in the docstring of `terrain/layers.py`.
  The `layers` step writes u8 rasters (scale 1/255) `fertility`, `vegetation`, `wetness` and one per deposit resource
  to `<out>/region/`.
- `bounds` (`min_lon`, `min_lat`, `max_lon`, `max_lat`), `region_cell_size_m` (default 1000) and `detail_regions` (rings of
  `[lon, lat]`, closed; the same as in the `MapDef`) are what the elevation steps below need.
- `--steps` picks steps from `fetch, mosaic, reproject, corrections, region, detail, rivers, layers, package`; they
  always run in that order. `rivers` and `package` still print which Task fills them in (T1.2.3.1 rivers, T1.2.6.1
  the package file); the rest do real work.
- `--offline` never downloads and fails at once, before any work, if a needed file is not in the cache.
- Exit status: 0 success, 1 a step failed (network, checksum, ...), 2 a usage error.

## The elevation steps (T1.2.2.1)

Everything is in the map's **world frame** (`terrain/projection.py`: Lambert azimuthal equal-area, origin at the
south-west corner of the projected bounds) and every array is `[row, column]` with row 0 at the south.

| Step | What it does |
|---|---|
| `fetch` | downloads what the bounds need: the Copernicus DEM GLO-30 1x1 degree tiles (AWS bucket `copernicus-dem-30m`; URL pattern in `terrain/dem.py`; open-sea tiles do not exist and are skipped) and Natural Earth `ne_10m_land.zip`, plus anything listed in `sources` |
| `mosaic` | joins the tiles into a VRT (`<out>.work/dem.vrt`, beside the package, not in it) and loads the land polygons |
| `reproject` | reads the DEM onto the region grid with GDAL (mean, minimum and maximum elevation per cell) and rasterizes the land mask; sea is forced to 0 m or below, missing data reads as sea; this is the working state that `corrections` and `layers` use |
| `region` | writes `region/elevation.csr`, `elevation_min.csr`, `elevation_max.csr` (i16, 0.25 m units) and `land_mask.csr` (u8) |
| `detail` | for every 2,048 m tile that touches a `detail_regions` polygon: reads the DEM at 32 m with an 8-sample margin (bilinear), applies the same corrections, and writes `detail/<tx>_<ty>.csr` (65 x 65 i16, `height = raw * 0.1 + offset` with a whole-meter offset per tile). Shared edge samples are made identical first (the first tile in row-then-column order wins) |

The steps also write hillshade PNGs to `<out>/previews/` (`region.png`, `detail_<tx>_<ty>.png` for the tile nearest the
middle, `detail_all.png`); they are not part of the package and `list_package_files` skips them. North is up in the
images, sea is blue, and the light comes from the north-west.

**Know the data.** The Copernicus DEM is a surface model: buildings, roads on embankments, orchards and tree canopy
are in the heights, so towns and cultivated land look rough in the previews. That is real, not a bug; smoothing or
corrections for it are a later decision (`docs/licenses.md` also has the license facts and the unreleased-tile note).

`maps/test_nile_small/` is a small real map (the Nile Delta apex, Cairo and Giza, 30.8 to 31.4 E and 29.8 to 30.3 N)
for trying the pipeline: `python -m terrain.build_map tools/terrain/maps/test_nile_small/map.json`. Its `sources`
list pins the size and SHA-256 of every file the build downloads, so a changed file is caught. To pin a new map:
build it once, then copy the sizes and checksums of the cached files into `sources` (the `fetch` step shows where they are).

## The download cache

Downloads go to `~/.cache/citysim/terrain/<dataset>/<file>` (override with `--cache` or
`CITYSIM_TERRAIN_CACHE`). A file that is there and matches its size and SHA-256 (when `map.json` gives them) is
never downloaded again; a stopped download is kept as `<file>.part` and resumed with an HTTP range request. A source
marked `"optional": true` (a DEM tile that may be open sea) that the server does not have (HTTP 403 or 404) is
remembered as `<file>.missing`, so later runs, `--offline` included, do not ask again.
Behind a proxy, set `HTTPS_PROXY` / `HTTP_PROXY`; network errors say so.

## Modules

| Module | What |
|---|---|
| `terrain/csr.py` | `write_csr` / `read_csr` / `read_csr_header`: the `.csr` raster format, byte for byte as `sim/src/world/raster.cpp` |
| `terrain/package.py` | `write_package_json`, `verify_package`, file checksums and the package hash (XXH3-64, as `world::compute_package_hash`) |
| `terrain/cache.py` | the download cache |
| `terrain/corrections.py` | the historical correction overlays (GeoJSON, architecture §15.7): operations, the working data they change, the report |
| `terrain/projection.py` | the map's projection and world frame; every step that turns lon/lat into map positions uses `make_to_world` |
| `terrain/dem.py` | the GLO-30 tile names and URLs, the VRT mosaic, and `Reprojector`, which reads windows of the DEM in the world frame |
| `terrain/landmask.py` | the Natural Earth land polygons and the land mask raster |
| `terrain/detail.py` | which tiles a polygon needs, the tile window, edge reconciling and the i16 quantization |
| `terrain/previews.py` | the hillshade images and the PNG writer |
| `terrain/steps.py`, `terrain/build_map.py` | the step registry and the command line |

## Tests

```
python -m pytest tools/terrain
```

No network and no real datasets: a local HTTP server stands in for the download sites, and a small synthetic DEM tile
and land zip (`tests/fixtures.py`) stand in for the elevation data. The cross-language tests in
`tests/test_cross_language.py` read the package that `citysim_mapgen` writes and need that binary
(`build/<preset>/sim/tools/mapgen/citysim_mapgen`, found automatically, or set `CITYSIM_MAPGEN`); they are skipped
if it is not built. The other direction, C++ reading a `.csr` written here, is the C++ test "a .csr written by the
Python pipeline reads in C++" on `sim/tests/fixtures/maps/python_*.csr`; regenerate those with
`python tools/terrain/scripts/make_cpp_fixture.py` only together with a format version bump.

## Data licenses

See `docs/licenses.md` before adding or shipping any dataset.
