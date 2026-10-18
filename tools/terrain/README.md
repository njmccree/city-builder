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
  always run in that order. `package` still prints which Task fills it in (T1.2.6.1, the package file); the rest do real work.
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

## The rivers step (T1.2.3.1)

`rivers` reads two more Natural Earth files (public domain): `ne_10m_rivers_lake_centerlines_scale_rank.zip` and
`ne_10m_lakes.zip` (about 2.4 and 2.3 MB). It writes `<out>/rivers.json`, `<out>/lakes.json` (schemas in
`game/content/README.md`) and `<out>/previews/rivers.png`.

What it does, in order:
1. Keeps the river lines inside the bounds whose **estimated discharge** is at least `rivers.min_discharge_m3s`
   (default 100). Natural Earth gives a `scalerank` but no discharge, so the discharge comes from a rank table
   (`rivers.rank_discharge`, default rank 1 = 1000 m3/s ... rank 12 = 0.3 m3/s). A river you know better gets
   `rivers.overrides.<name> = {"discharge_m3s": Q, "source": "...", "confidence": "low|medium|high"}`; the source is required.
2. Applies the `remove_river` and `river_path` corrections (the `corrections` and `detail` steps leave those two to this one).
3. Cuts the lines into **reaches** where another line ends on them, joins ends within `rivers.junction_m` (600) into one
   junction, and links each reach to the ones it flows into. Discharge is the largest of the reach's own estimate and its
   inflow, shared equally at a bifurcation; an override is never raised. Strahler order is computed.
4. **Snaps** each line onto the channel the DEM shows (`rivers.snap`: `enabled`, `radius_m` 800, `step_m` 30,
   `half_width_m` 90, `penalty_m_per_km` 0.5, `smooth_vertices` 15). The 1:10m lines are hundreds of meters off the real
   channel, so without this the carved bed and the water would sit on dry land. The shift fades to zero at junctions.
5. Simplifies with Douglas-Peucker: `rivers.tolerance_detail_m` (30) inside detail regions, `tolerance_elsewhere_m` (300) elsewhere.
6. Gives each reach a width `a * Q^b` and depth `c * Q^d` (`rivers.width` {a 7.2, b 0.5}, `rivers.depth` {a 0.27, b 0.39};
   Andreadis et al. 2013), with floors `min_width_m` 10 and `min_depth_m` 1.
7. **Water level** per vertex = the lowest DEM ground across the channel + `level_depth_fraction` x depth, fitted so it never
   rises downstream, and never above the reach upstream of it. **The default fraction is 0, not the 0.8 in the task text:**
   the Copernicus DEM is a surface model that already shows the flat water surface, so adding 0.8 depth would float the water
   above its own banks. Use 0.8 with a DEM of the river bed.
8. **Floodplain** per reach (`rivers.floodplain`: `distance_m` 3000, `height_m` 5, `cell_m` 64, `simplify_m` 64,
   `min_area_km2` 0.5): the cells within `distance_m` of the channel that are below the nearest water level + `height_m` and
   connected to the channel, as outer rings.
9. **Lakes** (`rivers.lakes`: `min_area_km2` 1, `simplify_m` 100): Natural Earth polygons with the median DEM height inside as
   their level. A lake mostly covered by a `fill_reservoir` correction is skipped (and logged).

**Know the data.** Natural Earth's lines are 1:10m, so small channels are missing and delta branches follow the DEM less
closely than the main river (the log prints how far the snap moved the lines). A hand correction (`river_path`) is the fix.

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
| `terrain/rivermodel.py` | the river settings, Natural Earth lines, reaches, flow graph, discharge, width/depth and the rivers.json document |
| `terrain/riverdem.py` | the DEM-dependent river parts: snapping to the channel, water levels, floodplains |
| `terrain/lakes.py`, `terrain/riverpreview.py` | the lake polygons; the rivers-over-hillshade image |
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
