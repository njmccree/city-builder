# tools/terrain: the offline terrain pipeline

Builds **map packages** (docs/architecture.md §15.2) from open datasets. The game never runs this; it only reads
the package. Real packages are built on the PC; tests and cloud sessions use the synthetic package from
`citysim_mapgen --synthetic` (sim/tools/README.md).

Python 3.11 or newer. Dependencies are pinned in `requirements.txt` (numpy, rasterio, pyproj, shapely,
zstandard, xxhash, requests, pytest).

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
- `--steps` picks steps from `fetch, mosaic, reproject, corrections, region, detail, rivers, layers, package`; they
  always run in that order. Only `fetch` does real work so far. The others print which Task fills them in
  (T1.2.2.1 rasters, T1.2.3.1 rivers, T1.2.4.1 layers, T1.2.5.1 corrections, T1.2.6.1 the full build).
- `--offline` never downloads and fails at once, before any work, if a needed file is not in the cache.
- Exit status: 0 success, 1 a step failed (network, checksum, ...), 2 a usage error.

## The download cache

Downloads go to `~/.cache/citysim/terrain/<dataset>/<file>` (override with `--cache` or
`CITYSIM_TERRAIN_CACHE`). A file that is there and matches its size and SHA-256 (when `map.json` gives them) is
never downloaded again; a stopped download is kept as `<file>.part` and resumed with an HTTP range request.
Behind a proxy, set `HTTPS_PROXY` / `HTTP_PROXY`; network errors say so.

## Modules

| Module | What |
|---|---|
| `terrain/csr.py` | `write_csr` / `read_csr` / `read_csr_header`: the `.csr` raster format, byte for byte as `sim/src/world/raster.cpp` |
| `terrain/package.py` | `write_package_json`, `verify_package`, file checksums and the package hash (XXH3-64, as `world::compute_package_hash`) |
| `terrain/cache.py` | the download cache |
| `terrain/steps.py`, `terrain/build_map.py` | the step registry and the command line |

## Tests

```
python -m pytest tools/terrain
```

No network and no real datasets: a local HTTP server stands in for the download sites. The cross-language tests in
`tests/test_cross_language.py` read the package that `citysim_mapgen` writes and need that binary
(`build/<preset>/sim/tools/mapgen/citysim_mapgen`, found automatically, or set `CITYSIM_MAPGEN`); they are skipped
if it is not built. The other direction, C++ reading a `.csr` written here, is the C++ test "a .csr written by the
Python pipeline reads in C++" on `sim/tests/fixtures/maps/python_*.csr`; regenerate those with
`python tools/terrain/scripts/make_cpp_fixture.py` only together with a format version bump.

## Data licenses

See `docs/licenses.md` before adding or shipping any dataset.
