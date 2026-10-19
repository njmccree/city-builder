# E1 — Terrain & World

**Goal:** a real-data **Ancient Near East** map running in the game. That means:
- continuous terrain from Egypt to Mesopotamia, built from real elevation, river and land-cover data, with light historical corrections
- a **region view** that blends from 3D terrain into a paper-map look
- founding an **8 × 8 km city** in the **Nile Delta** and entering a detailed 4 m terrain city view
- fixed rivers whose water rises and falls with the **annual Nile inundation**
- CS2-style resource and fertility overlays
- instant terraforming
- the walking-skeleton agents walking on the real terrain

**Milestone:** M1 (first Epic of "one playable city in one era: Ancient Nile Delta").

**Binding design:**
- GDD §7.5 (instant terraforming), §12.7 (resources), §13 (world and terrain decisions E1-a…f)
- `docs/architecture.md` §15 (World & Terrain), plus §4, §7.3, §8, §10, §11

**Out of scope:**
- roads, buildings, zoning (E2)
- real citizens (E3)
- borders, realms and labels for realms (E6)
- seamless zoom between views (later)
- water that flows like in CS (later)
- other maps (E12)

**Size:** 9 Features · 18 Stories · 32 Tasks. **Model budget:** Opus ×2 (★), Haiku ×2, Sonnet ×28. **Sessions:** 19 [Cloud], 13 [PC].

**Before starting:** S0.9 (Foundation hardening) is recommended first, especially the optimized "dev" preset (F-4) for the [PC] Tasks. It isn't required.

## Suggested execution order

```
F1.1  T1.1.1.1 → T1.1.1.2 → T1.1.2.1★ → T1.1.2.2 → T1.1.2.3
F1.2  T1.2.1.1 → T1.2.5.1 → T1.2.2.1(PC) → T1.2.3.1(PC) → T1.2.4.1 → T1.2.4.2 → T1.2.5.2 → T1.2.6.1(PC)
F1.5  T1.5.1.1                                    (after T1.1.2.1)
F1.3  T1.3.1.1 → T1.3.1.2 → T1.3.2.1                (after T1.1.2.2)
F1.4  T1.4.1.2 → T1.4.1.1                            (after T1.1.2.2)
F1.6  T1.6.1.1★(PC) → T1.6.1.2(PC) → T1.6.1.3(PC) → T1.6.1.4(PC) → T1.6.2.1(PC)
F1.7  T1.7.1.3 · T1.7.1.1(PC) → T1.7.1.2(PC) → T1.7.2.1(PC)
F1.8  T1.8.1.1 → T1.8.1.2(PC)
F1.9  T1.9.1.1 → T1.9.1.2(PC)
```
Cloud-only work (F1.1, F1.3–F1.5, the pipeline code) can be built and tested with the **synthetic map** before the real package exists. The real Ancient Near East package (T1.2.6.1) is needed only by the PC verification Tasks.

## Epic acceptance criteria
- [ ] `python -m terrain.build_map tools/terrain/maps/ancient_near_east/map.json` builds the package on the PC. Its size is recorded and its data licenses are documented in `docs/licenses.md`.
- [ ] New game → the region view shows Egypt → Mesopotamia. Zooming out blends it into a paper-map style with rivers, coasts and historical labels.
- [ ] Choosing a valid Nile Delta site founds a city. Overlapping, off-land and outside-detail-region sites are rejected with a reason.
- [ ] The city view shows 4 m terrain at ≥ 60 FPS on the GTX 1660 Ti, with water, vegetation, and fertility/stone/ore overlays.
- [ ] The Nile rises and falls over the calendar year. Floods change the visible water extent and renew fertility, and high or low years emit events.
- [ ] Terraforming is instant, stays within the city area, and survives save → load. The base terrain is not stored in saves.
- [ ] Debug agents walk on the real terrain, avoiding water and steep slopes.
- [ ] All determinism, save round-trip and CI checks pass. Terrain generated from the same package is identical across runs.

---

## F1.1 — World Data Model & Map Packages

### S1.1.1 — Map definitions and the package format
> *As a developer, maps are content definitions backed by generated packages in one documented raster format, so the simulation, the pipeline and tests all share one contract.*

**Acceptance criteria**
- `MapDef` is loaded and validated.
- `.csr` read/write round-trips.
- `package.json` is parsed and its checksums are verified.
- `citysim_mapgen --synthetic` produces a valid small package that tests and cloud sessions use.

#### [x] T1.1.1.1 — MapDef, `.csr` rasters, package manifest
- **Model:** Sonnet · **Session:** [Cloud] · **Depends on:** E0
```
Task T1.1.1.1 (Story S1.1.1). Read CLAUDE.md and docs/architecture.md §8, §15.1–§15.3 carefully.

Create a new module citysim::world (sim/include/citysim/world/, sim/src/world/, sim/tests/world/).

1. `.csr` raster I/O (architecture §15.2): struct RasterHeader {width, height, dtype, scale, offset, nodata};
   template Raster<T> (row-major, row 0 = south); write_csr(path, raster) / read_csr(path) with zstd payload
   and a trailing XXH3 checksum; errors throw world::MapFormatError with the path and reason. Supported
   dtypes u8, u16, i16, f32; decode applies scale/offset to give float values via a helper.
2. MapPackage manifest: parse package.json (schema in §15.2: map id, pipeline_version, sources[], projection
   {type:"laea", lat_0, lon_0}, bounds_m {width, height}, region {cell_size_m, width, height, layers{name:
   file}}, detail {tile_size_m: 2048, sample_spacing_m: 32, tiles: [[tx,ty],...]}, rivers file, lakes file,
   files{relative path: xxh3 hex}, package_hash). verify_package(dir) checks every listed file's checksum.
   Put a precise JSON example in a comment and in game/content/README.md (new "Map packages" section).
3. MapDef content type "map" (architecture §8): id, name_key, desc_key, package (folder name under the mod's
   maps/), lon/lat bounds, region_cell_size_m (default 1000), map_seed (u64), detail_regions (array of
   polygons in lon/lat), river_regimes (array of DefRef ids — leave as plain strings until T1.3.2.1 adds the
   def type; document that). Validation: bounds sane, polygons closed with ≥ 3 points, seed present.
4. .gitignore: ignore game/content/*/maps/*/ (generated packages) but keep a README.md in each maps/ folder.
5. Tests: raster round-trip for each dtype; checksum corruption detected; manifest parse errors with JSON
   pointers; MapDef validation errors.

Run all tests and the determinism lint. Follow the Task workflow in CLAUDE.md (branch story/S1.1.1-map-format).
```

#### [x] T1.1.1.2 — Synthetic map generator (`citysim_mapgen --synthetic`)
- **Model:** Sonnet · **Session:** [Cloud] · **Depends on:** T1.1.1.1
```
Task T1.1.1.2 (Story S1.1.1, last task). Read docs/architecture.md §15.1–§15.2 and §15.6.

Add sim/tools/mapgen/ → executable citysim_mapgen with `--synthetic --out DIR [--seed N] [--size-km N]`.
It writes a complete, valid map package (architecture §15.2) with no real data, for tests, CI and cloud sessions:
- A region of size-km × size-km (default 96 km), 1,000 m region cells; elevation: a valley running
  south→north that widens into a delta and a coast at the north edge (sea below 0 m), desert plateaus at
  ±60–120 m on both sides, procedural noise via citysim::rng only (deterministic for a seed).
- One river (a main channel that splits into 3 delta branches) in rivers.json with per-vertex width (200–600 m
  main, 80–200 m branches), depth and base water level; an empty lakes.json.
- Region layers: elevation (i16, 0.25 m units), land_mask (u8), fertility (u8; high on the valley floor and
  near channels), vegetation (u8), wetness (u8), deposits: stone_limestone and ore_copper (u8, a few blobs).
- Detail tiles (32 m spacing, 65×65, 2,048 m) covering the valley and delta (a detail region), consistent
  with the region elevation.
- package.json with all checksums and the package hash; a matching MapDef JSON written next to it (so a test
  content root can be assembled).
Also add a CTest fixture setup that generates the synthetic package into the build dir once
(FIXTURES_SETUP), and a helper in sim/tests/support/ returning its path, so later tests can use it.
Keep the synthetic MapDef in test content only (do not add it to game/content/base); the base game gets the
real map in T1.2.6.1. Document the tool in sim/tools/README.md.

Tests: the package verifies; the same seed gives byte-identical files; different seeds differ.
Run all tests. Follow the Task workflow in CLAUDE.md; last task of S1.1.1, so open the PR.
```

### S1.1.2 — World terrain in the simulation
> *As a systems developer, the simulation holds the map, knows the city areas, and can synthesize and query 4 m terrain deterministically, so every later system (water, resources, building, pathing) builds on one terrain service.*

**Acceptance criteria**
- `WorldTerrain` loads a package, synthesizes city tiles deterministically (golden hash), and answers height, slope and normal queries.
- Terrain edits are sparse deltas that are hashed and saved. Saves never contain base heights.

#### [x] T1.1.2.1 ★ — WorldTerrain service, frames, tile cache
- **Model:** **Opus** · **Session:** [Cloud] · **Depends on:** T1.1.1.2
```
Task T1.1.2.1 (Story S1.1.2). Architecture-critical. Read docs/architecture.md §4, §5, §7.2, §7.3, §10 and
§15 fully, and sim/include/citysim/debug/walking_skeleton.hpp + runner/game_session.hpp for the module
pattern (install(), registration, publish hooks).

Design and implement the core of citysim::world:
1. Frames (architecture §15.3): WorldPos {double x, z}; LocalPos {float x, z}; TileCoord {int32 tx, ty};
   helpers world↔tile, and city-local↔world via CityArea::origin. Constants: k_tile_size_m = 2048,
   k_city_sample_spacing_m = 4, k_city_tile_samples = 513, k_city_area_tiles = 4.
2. LoadedMap: owns the parsed manifest and region rasters (decoded to float on load), lazily loads package
   detail tiles (32 m) on request. Immutable after load; shared via std::shared_ptr<const LoadedMap>.
3. CityTile: immutable synthesized base heights (513×513 float) + a version number; shared via
   std::shared_ptr<const CityTile>. The synthesis itself is T1.1.2.2 — here define the interface
   TerrainSynth::make_tile(const LoadedMap&, TileCoord, uint64 map_seed) and a trivial placeholder
   implementation (bilinear upsample only) so this task is testable.
4. WorldTerrain singleton (World ctx):
   - holds the map id, package hash, generator version, shared LoadedMap;
   - a tile cache keyed by TileCoord with "active" tiles (requested for simulation) — activate_tiles(span)
     synthesizes missing tiles in parallel via JobSystem (one chunk per tile, merged in TileCoord order);
     deactivate releases them;
   - height_at(WorldPos) and height_at_local(city, LocalPos) (bilinear on base + delta; outside active tiles
     fall back to the 32 m detail DEM, then the region raster), slope_at, normal_at;
   - publishes active tiles to snapshots: add a publish hook that exposes a "terrain.tiles" list (TileCoord +
     shared_ptr<const CityTile> + version) — extend RenderSnapshot with a small dedicated vector for this,
     documented like SnapshotGrid in architecture §7.3.
5. Hashing/saving: register WorldTerrain so the hash and save include ONLY map id, package hash, generator
   version and the set of active tiles (not heights). On load: re-open the package by map id via the
   ContentDatabase (MapDef → package path), compare package hash and generator version (mismatch → load
   warning), re-activate tiles.
6. Module install: world::install(Simulation&, WorldConfig{map_id}) wired into runner::GameSession
   (NewGameRequest gains map_id; empty = no world, as today). Keep the walking skeleton working unchanged.
7. Tests using the synthetic package fixture: frame conversions; height sampling continuity across tile
   edges (shared edge samples equal); parallel activation identical for 1 and 8 workers; save→load restores
   the same active tiles and the same state hash; package-hash mismatch produces a warning.

Update architecture §15 if any detail had to change (explain in the PR). Run all tests (+ linux-tsan).
Follow the Task workflow in CLAUDE.md (branch story/S1.1.2-world-terrain).
```

#### [x] T1.1.2.2 — Deterministic terrain synthesis (upsampling + micro-detail)
- **Model:** Sonnet · **Session:** [Cloud] · **Depends on:** T1.1.2.1
```
Task T1.1.2.2 (Story S1.1.2). Read docs/architecture.md §4 and §15.4, and the TerrainSynth interface from
T1.1.2.1.

Replace the placeholder TerrainSynth with the real generator (k_terrain_generator_version = 1):
1. Bicubic (Catmull-Rom) upsampling of the 32 m package detail DEM to 4 m, reading the neighbor tiles'
   samples at borders so shared edges match exactly.
2. Micro-detail: a sum of value/gradient-noise octaves implemented in sim (no libraries) seeded only by
   rng::hash64(map_seed, stream("terrain.detail"), tx, ty) and world sample coordinates, so adjacent tiles
   agree at the edges. Amplitude scales with slope and terrain class: ≤ 0.3 m on flat floodplain, up to 3 m
   on rough desert; zero on water cells.
3. Parallel-safe and allocation-light; generating one tile must take < 30 ms in Release on the cloud
   machine (measure in a benchmark test tagged [.bench]).
4. Golden test: the synthetic package with a fixed seed produces a fixed XXH3 of tile (3,5)'s heights
   (record it; changing it requires bumping k_terrain_generator_version and noting it in the PR).
5. Tests: edge continuity in heights and in first differences across tile borders; determinism across
   worker counts; version constant is stored in saves (from T1.1.2.1).

Run all tests. Follow the Task workflow in CLAUDE.md (same story branch).
```

#### [x] T1.1.2.3 — Terrain edit deltas
- **Model:** Sonnet · **Session:** [Cloud] · **Depends on:** T1.1.2.2
```
Task T1.1.2.3 (Story S1.1.2, last task). Read docs/architecture.md §4, §10 and §15.4.

1. TerrainDeltas singleton: sparse per-tile float offset layers (allocate a tile's 513×513 layer only when
   first edited; keep a dirty-rect per tile). Edits produce a NEW immutable CityTile version (copy-on-write
   of base+delta composite heights) published through the existing "terrain.tiles" snapshot data, so the
   renderer sees a new pointer only for edited tiles.
2. Shared edges: an edit touching a tile edge also updates the neighbor's shared samples so edges stay equal.
3. API used by later tasks: apply_delta(TileCoord, rect, fn(sample_x, sample_y, current_height) -> new_height)
   and convenience brushes are T1.8.1.1 — here only the storage, composition and publication.
4. Hash and save the deltas (only non-empty tiles, compressed with zstd inside the WRLD block), bump nothing
   else. Deltas for tiles that are not active are kept (and re-applied when the tile is activated again).
5. Tests: edit → height_at reflects it; edge consistency; save→load→same hash and same heights; deltas on an
   inactive tile survive deactivate/activate; memory: an unedited tile allocates no delta layer.

Run all tests. Follow the Task workflow in CLAUDE.md; last task of S1.1.2, so open the PR.
```

---

## F1.2 — Real-Data Terrain Pipeline (offline, Python)

### S1.2.1 — Pipeline skeleton and data licenses
> *As the developer, I have a tested Python pipeline with cached downloads and documented dataset licenses, so real maps can be built reproducibly and shipped legally.*

**Acceptance criteria**
- `tools/terrain` is a Python package with a CLI, pinned requirements, download caching with checksums, pytest tests in CI, and a `.csr` writer that matches the C++ reader.
- `docs/licenses.md` records each dataset's license and attribution, and states whether commercial use is allowed.

#### [x] T1.2.1.1 — Pipeline skeleton, `.csr` writer, license review
- **Model:** Sonnet · **Session:** [Cloud] · **Depends on:** T1.1.1.1
```
Task T1.2.1.1 (Story S1.2.1). Read docs/architecture.md §15.2 and §15.5.

1. tools/terrain/ as a Python package `terrain` (Python ≥ 3.11): pyproject.toml or requirements.txt with
   pinned versions of numpy, rasterio, pyproj, shapely, zstandard, xxhash, requests, pytest. README with setup
   (venv) for Windows and Linux.
2. terrain/csr.py: write_csr / read_csr exactly matching the C++ format (T1.1.1.1). Add a C++ test that reads
   a small .csr written by the Python writer (commit that tiny fixture, < 4 KB, under sim/tests/fixtures/maps/)
   and a pytest that reads one written by citysim_mapgen (skip if the binary is not built).
3. terrain/cache.py: download manager → ~/.cache/citysim/terrain/<dataset>/..., resumable, verifies size and
   checksum when known, never re-downloads a valid file, clear errors for proxy/network failures.
4. terrain/build_map.py CLI: `python -m terrain.build_map <map.json> [--out DIR] [--steps a,b,c] [--offline]`
   with a step registry (fetch, mosaic, reproject, corrections, region, detail, rivers, layers, package) —
   steps are stubs that later tasks fill; `--offline` fails fast if a needed file is not cached.
5. terrain/package.py: write package.json + checksums + package hash per architecture §15.2.
6. CI: add a `terrain` job running pytest (no network; fixtures only).
7. License review: research the current licenses of Copernicus DEM GLO-30, HydroSHEDS (HydroRIVERS,
   HydroLAKES), Natural Earth and ESA WorldCover. Write docs/licenses.md with, per dataset: license name and
   link, commercial use allowed?, attribution text required, redistribution of derived data allowed?, and the
   date checked. If any is NOT suitable for a commercial game, say so clearly at the top and propose the
   fallback (Natural Earth or OpenStreetMap) — do not silently switch.

Run pytest and all C++ tests. Follow the Task workflow in CLAUDE.md (branch story/S1.2.1-pipeline); open the PR.
In your final message, summarize the license findings for the user.
```

### S1.2.2 — Elevation and land/sea
> *As the developer, the pipeline produces region and detail elevation rasters and a land mask for any map definition, so maps come from real terrain.*

**Acceptance criteria**
- For the map bounds: the GLO-30 tiles are fetched, mosaicked, reprojected to LAEA, and turned into region (1 km) and detail (32 m tiles, detail regions only) rasters plus a land mask.
- Outputs are visually checked with preview PNGs.

#### [x] T1.2.2.1 — Elevation, reprojection, region and detail rasters
- **Model:** Sonnet · **Session:** [PC] · **Depends on:** T1.2.1.1, T1.2.5.1
```
Task T1.2.2.1 (Story S1.2.2). Read docs/architecture.md §15.1–§15.3, §15.5, §15.7 and tools/terrain/README.md.
Run on the PC (large downloads; network access needed).

Implement the pipeline steps fetch (elevation), mosaic, reproject, corrections (call the engine from
T1.2.5.1 with the map's correction files — none may exist yet; that is fine), region, detail:
1. Fetch the Copernicus GLO-30 1°×1° tiles covering the map's lon/lat bounds (+ a small margin) from the
   official public distribution (document the URL pattern), via the cache manager. Missing ocean tiles are
   normal — treat as sea.
2. Mosaic into a VRT; reproject to LAEA centered on the bounds' center (architecture §15.3) with bilinear
   resampling; world frame origin at the projected SW corner.
3. Land mask from Natural Earth 10m land polygons (fetch via cache) rasterized at region and detail
   resolutions; sea elevation forced ≤ 0.
4. Region rasters at the map's region_cell_size_m: elevation (mean), elevation_min, elevation_max (i16,
   0.25 m units), land_mask (u8).
5. Detail tiles: 2,048 m tiles at 32 m spacing (65×65 incl. shared edges) for every tile intersecting a
   detail-region polygon; i16 at 0.1 m resolution with a per-tile offset in the header's offset field.
6. Preview PNGs (hillshade) of the region and one detail tile written to <out>/previews/ (not part of the
   package).
7. Add a test map definition tools/terrain/maps/test_nile_small/map.json covering a small box around the
   Nile Delta apex (e.g., 30.8–31.4 E, 29.8–30.3 N) with one detail region, and build it.

Report download sizes, run time, package size and attach the preview paths in your final message.
Follow the Task workflow in CLAUDE.md (branch story/S1.2.2-elevation); open the PR.
```

### S1.2.3 — Rivers and lakes
> *As the developer, the pipeline extracts real river courses with widths and water levels, so the simulation's fixed rivers follow real geography.*

**Acceptance criteria**
- `rivers.json` has simplified polylines with order, discharge, and per-vertex width, depth and base water level, consistent with the DEM.
- `lakes.json` and floodplain polygons are included.

#### [x] T1.2.3.1 — River and lake extraction
- **Model:** Sonnet · **Session:** [PC] · **Depends on:** T1.2.2.1
```
Task T1.2.3.1 (Story S1.2.3). Read docs/architecture.md §15.2, §15.5–§15.7 and docs/licenses.md (use the
river dataset the license review approved).

Implement the rivers step:
1. Fetch the approved river and lake datasets for the bounds via the cache.
2. Select rivers above a configurable order/discharge threshold (map.json: rivers.min_discharge_m3s),
   reproject, simplify (Douglas–Peucker, tolerance 30 m in detail regions, 300 m elsewhere), merge into
   continuous reaches split at confluences and bifurcations.
3. Per vertex: width from discharge via a documented hydraulic-geometry rule (width = a·Q^b, with a, b in
   map.json and per-river overrides), depth likewise, and base water level = DEM minimum along the channel
   cross-section + depth·0.8, made monotonically non-increasing downstream.
4. Floodplain polygon per reach: cells within N km of the channel whose elevation is < water level + H m
   (N, H in map.json) — this bounds flood fills in the simulation (§15.6).
5. Lakes: polygons with a water level; skip reservoirs listed in the corrections (T1.2.5.1 removes them).
6. Write rivers.json / lakes.json (schema documented in tools/terrain/README.md and game/content/README.md)
   and a preview PNG overlaying rivers on the hillshade.
7. Rebuild test_nile_small and check the Nile and its delta branches appear.

Follow the Task workflow in CLAUDE.md (branch story/S1.2.3-rivers); open the PR with the preview path.
```

### S1.2.4 — Fertility, vegetation and deposit layers
> *As the designer, every map has soil fertility, vegetation, wetness and mineral deposit layers grounded in geography and history, so resource overlays and farming have meaning.*

**Acceptance criteria**
- Rule-based fertility, vegetation and wetness layers are documented.
- Authored deposit and fertility override GeoJSON for the Ancient Near East, with sources, is rasterized into the package.

#### [x] T1.2.4.1 — Derived layers and override rasterization
- **Model:** Sonnet · **Session:** [Cloud] · **Depends on:** T1.2.1.1
```
Task T1.2.4.1 (Story S1.2.4). Read docs/architecture.md §15.5 and §15.8, and docs/gdd.md §12.7.

Implement the layers step (it must work offline on fixture rasters; real inputs come from earlier steps):
1. Fertility (0–255): a documented rule combining height above the nearest river water level, distance to
   channel, the floodplain polygons (high), wetness, and land cover (desert/bare low). Ancient-correct: do NOT
   use modern cropland as fertility (modern irrigation reaches far beyond ancient floodplains). Rules and
   weights are parameters in map.json with defaults in code.
2. Vegetation (0–255) and wetness (0–255) from land cover (the approved land-cover dataset when available,
   else a rule from fertility + water proximity), with the same "no modern irrigation" caveat; wetlands and
   marsh classes map to high wetness (the delta's northern marshes matter).
3. Deposits: rasterize authored GeoJSON (tools/terrain/maps/<map>/deposits.geojson) — features with
   properties {resource: "<ResourceDef id>", richness 0–1, source, confidence} — into one u8 region layer per
   resource id, with soft edges; plus procedural fill for generic stone (limestone/sandstone where the
   geology polygons in deposits.geojson say so).
4. Fertility overrides: fertility_overrides.geojson features {mode: set|add|mul, value, source}.
5. pytest coverage with fixture rasters.

Follow the Task workflow in CLAUDE.md (branch story/S1.2.4-layers).
```

#### [ ] T1.2.4.2 — Author Ancient Near East deposits and fertility overrides
- **Model:** Sonnet · **Session:** [Cloud] · **Depends on:** T1.2.4.1, T1.4.1.2
```
Task T1.2.4.2 (Story S1.2.4, last task). Content authoring with sources — no engine code.

Create tools/terrain/maps/ancient_near_east/deposits.geojson and fertility_overrides.geojson (lon/lat,
EPSG:4326) for the Ancient Near East around 3000 BCE, using the ResourceDef ids from
game/content/base/defs/resources.json. Cover at least:
- Stone: Tura/Mokattam limestone, Aswan granite, Gebel el-Silsila sandstone, Hatnub travertine/"alabaster",
  Wadi Hammamat greywacke, Mesopotamian scarcity of stone (alluvium has almost none).
- Ores and metals: Sinai/Wadi Maghara and Timna copper, Eastern Desert and Nubian gold, Anatolian/Taurus
  silver and lead, Iranian plateau tin sources (mark low confidence), Cyprus copper if in bounds.
- Other: Mesopotamian bitumen seeps (Hit), Natron (Wadi el-Natrun), Faiyum, clay along all floodplains.
- Geology polygons for generic limestone vs. sandstone vs. alluvium.
- Fertility overrides: Faiyum depression, Mesopotamian alluvial plain irrigation-capable zones, salinized
  southern Mesopotamia (lower), oases (Kharga, Dakhla, Siwa).
Every feature needs properties source (a citable reference: book/article/site) and confidence
(high|medium|low). Write tools/terrain/maps/ancient_near_east/SOURCES.md listing each source once and noting
uncertainties for the designer. Validate with the pipeline's layers step on fixture inputs.

Follow the Task workflow in CLAUDE.md; last task of S1.2.4, so open the PR and ask the user to review
historical accuracy (GDD §11.6: the designer reviews all historical content).
```

### S1.2.5 — Historical corrections
> *As the designer, the starting map reflects the ancient landscape: no modern canals or reservoirs, plus approximate ancient river branches and coastlines. The corrections are authored, sourced and reviewable.*

**Acceptance criteria**
- The correction engine supports the architecture §15.7 operations.
- The Ancient Near East correction set is authored with sources and confidence, and the designer has reviewed it.

#### [x] T1.2.5.1 — Correction overlay engine
- **Model:** Sonnet · **Session:** [Cloud] · **Depends on:** T1.2.1.1
```
Task T1.2.5.1 (Story S1.2.5). Read docs/architecture.md §15.7.

Implement terrain/corrections.py: load ordered GeoJSON files listed in map.json (corrections: [...]), apply
operations to the reprojected DEM, land mask and river set, in file order then feature order:
- set_water {level_m} / set_land {min_elevation_m}: polygon fill;
- elevation_offset {meters} with optional feather_m; flatten_to {elevation_m, feather_m};
- remove_river {match: name|id}; river_path {name, width_m[], depth_m[]} adding or replacing a polyline;
- fill_reservoir {polygon, profile: "valley_interpolate"}: replaces a reservoir's flat water surface with an
  interpolated valley profile from the dam downstream elevation to the upstream inflow elevation, carving a
  channel — used for Lake Nasser and Euphrates/Tigris reservoirs.
Every feature must have source and confidence properties; missing → error. Write a correction report
(markdown) listing each applied feature and its effect (area, mean elevation change). pytest with synthetic
rasters for each operation.

Follow the Task workflow in CLAUDE.md (branch story/S1.2.5-corrections).
```

#### [ ] T1.2.5.2 — Author light corrections for the Ancient Near East (~3000 BCE)
- **Model:** Sonnet · **Session:** [Cloud] · **Depends on:** T1.2.5.1, T1.2.3.1
```
Task T1.2.5.2 (Story S1.2.5, last task). Content authoring with sources.

Create tools/terrain/maps/ancient_near_east/corrections/*.geojson ("light corrections", GDD §13), each feature
with source and confidence:
1. Remove modern works: Suez Canal (set_land along its course), Sweet Water/Ismailia canals, Lake Nasser and
   the Aswan dams (fill_reservoir), Toshka lakes, Euphrates reservoirs (Atatürk, Keban, Karakaya, Birecik,
   Tabqa/Lake Assad, Haditha/Qadisiyah), Tigris reservoirs (Mosul, Dukan, Darbandikhan, Hamrin), Lake Tharthar
   and Habbaniyah links if modern, Toshka spillway; major modern drainage of the Mesopotamian marshes
   (restore marsh wetness via set_water shallow or a wetness override).
2. Ancient Nile Delta: approximate Pelusiac, Tanitic/Mendesian, Sebennytic, Bolbitine and Canopic branches as
   river_path features (low/medium confidence), remove the modern Rosetta/Damietta dominance only where
   sources support it; Lake Moeris in the Faiyum at a higher level (medium confidence).
3. Coastlines: the head of the Persian Gulf further north-west than today (~3000 BCE; low–medium confidence —
   cite the literature and choose a conservative line), the Nile Delta coast slightly south of today's where
   sources support it.
4. Keep it light: no speculative invention; when uncertain, prefer leaving modern terrain.
Write tools/terrain/maps/ancient_near_east/CORRECTIONS.md: each correction, why, source, confidence, and
what the designer should check. Run the corrections step on fixture inputs to validate syntax.

Follow the Task workflow in CLAUDE.md; last task of S1.2.5, so open the PR and ask the user to review.
```

### S1.2.6 — Build the Ancient Near East map package
> *As the player, the game ships the Ancient Near East map (Egypt → Mesopotamia), with detail regions where ancient cities can be founded.*

**Acceptance criteria**
- `base:map.ancient_near_east` is defined.
- The full package builds on the PC, its size and time are recorded, it passes `citysim_validate` and `verify_package`, and its previews look right.

#### [ ] T1.2.6.1 — Ancient Near East map definition and full build
- **Model:** Sonnet · **Session:** [PC] · **Depends on:** T1.2.2.1, T1.2.3.1, T1.2.4.2, T1.2.5.2
```
Task T1.2.6.1 (Story S1.2.6). Read docs/architecture.md §15 and tools/terrain/README.md.

1. tools/terrain/maps/ancient_near_east/map.json: bounds about lon 24°E–50°E, lat 21°N–38.5°N (Egypt incl.
   the Nile to the Second Cataract, the Levant, southern Anatolia's edge, Mesopotamia to the Gulf, western
   Iran's foothills); region_cell_size_m 1000; map_seed; detail regions: (a) Nile Delta + valley to Aswan with
   a ~15 km margin, (b) Faiyum, (c) the Mesopotamian alluvium from Hit/Samarra to the Gulf, (d) the upper
   Euphrates/Habur bend, (e) the southern Levant coast; corrections, deposits, overrides as authored.
2. game/content/base/defs/maps.json: base:map.ancient_near_east (name/desc keys + loc strings in
   game/content/base/loc/en/maps.json), package "ancient_near_east", same bounds, seed and detail regions
   (generated from map.json by the pipeline's package step to avoid drift — implement that sync).
3. Build the full package; record download size, build time and package size per layer in
   tools/terrain/maps/ancient_near_east/BUILD.md; run verify_package and citysim_validate.
4. Check the previews (hillshade, rivers, fertility, deposits, corrections) and list anything that looks
   wrong for the user to judge.

Report results honestly in your final message, including any dataset that failed to download.
Follow the Task workflow in CLAUDE.md (branch story/S1.2.6-ane-map); open the PR.
```

---

## F1.3 — Rivers & Water in the Simulation

### S1.3.1 — River network and variable water levels
> *As a player, rivers flow through my terrain with real widths and depths, and their water level can rise and fall, flooding low land, so water is a living part of the map.*

**Acceptance criteria**
- Rivers are loaded and their channels carved into city tiles deterministically.
- `is_water` and `water_depth` queries work.
- Per-reach level offsets are hashed and saved.
- Flood extents are recomputed incrementally and emit `WaterChanged`.

#### [x] T1.3.1.1 — River network, channel carving, water queries
- **Model:** Sonnet · **Session:** [Cloud] · **Depends on:** T1.1.2.3
```
Task T1.3.1.1 (Story S1.3.1). Read docs/architecture.md §15.4 and §15.6.

1. RiverNetwork (immutable, part of LoadedMap): reaches with polylines in the world frame, per-vertex width,
   depth, base water level; a spatial index (uniform grid of reach segments) for "nearest channel" queries.
2. Channel carving in TerrainSynth (bump k_terrain_generator_version to 2 and update the golden hash, noting
   it in the PR): for samples inside a channel's width, lower the bed to base level − depth with a smooth
   bank profile (parameters in a new defines/terrain.json, loaded as TerrainDefines content); deterministic,
   edge-consistent.
3. WaterState singleton: per-reach level offset (float m, default 0); sea level 0. Queries:
   water_level_at(WorldPos) (channel level, or flood level where flooded, or sea), is_water(pos),
   water_depth(pos) = max(0, level − height).
4. Tests with the synthetic package: carved channel depth matches; banks are continuous across tiles; queries
   on/off channel; hash/save include level offsets only.

Run all tests. Follow the Task workflow in CLAUDE.md (branch story/S1.3.1-rivers).
```

#### [x] T1.3.1.2 — Flood extents (incremental, time-sliced)
- **Model:** Sonnet · **Session:** [Cloud] · **Depends on:** T1.3.1.1
```
Task T1.3.1.2 (Story S1.3.1, last task). Read docs/architecture.md §5.5, §7.2 and §15.6.

1. FloodMap per active city: a bitset over the 4 m samples marking flooded land (not channel). When a reach's
   level changes, recompute: flood fill outward from the channel over samples with height < level, limited to
   the reach's floodplain polygon (from rivers.json); process at most N samples per tick (defines; default
   250,000) so it spreads over ticks; finished → emit WaterChanged{city, reach, flooded_area_m2}.
2. Publish flooded extents for rendering as a SnapshotGrid per active tile ("water.flood.<tx>_<ty>",
   shared pointer, new pointer only on change).
3. Debug command SetRiverLevel{reach_index, offset_m} (+ command_marshal entry + console command
   river_level <reach> <m>) for testing.
4. FloodMap is derived state: NOT saved; recomputed after load from WaterState (document; the hash includes
   WaterState only). Test that a loaded game converges to the same FloodMap.
5. Tests: fill bounded by the floodplain; time slicing produces the same final result as an unbounded fill;
   determinism across worker counts.

Run all tests. Follow the Task workflow in CLAUDE.md; last task of S1.3.1, so open the PR.
```

### S1.3.2 — The Nile inundation (test case for the event mechanic)
> *As a player in Ancient Egypt, the Nile floods every year on a historical schedule, with good, low and high years. Floods renew fertility, and extreme years produce events. This is the test case for natural disaster events.*

**Acceptance criteria**
- `RiverRegimeDef` content defines an annual curve and year variance, run on calendar time.
- The Nile regime rises around July, peaks around September, and recedes by November.
- Year class is drawn deterministically.
- `FloodSeason` events (low / normal / high / extreme) are emitted.
- Receding water applies a fertility renewal delta.

#### [ ] T1.3.2.1 — RiverRegimeDef and the annual Nile flood
- **Model:** Sonnet · **Session:** [Cloud] · **Depends on:** T1.3.1.2, T1.4.1.1
```
Task T1.3.2.1 (Story S1.3.2). Read docs/architecture.md §5.5, §6 (calendar time), §15.6, §15.8 and
docs/gdd.md §12.7.

1. Content type "river_regime": id, name_key, reaches (match by river name in rivers.json), annual curve as
   (day_of_year, level_offset_m) control points (cubic interpolation, wraps at year end), year classes
   [{id, weight, multiplier, event_key}] e.g. low 0.2×0.6, normal 0.6×1.0, high 0.17×1.25, extreme 0.03×1.5.
2. RiverRegimeSystem (phase Calendar, runs on calendar time): on YearStarted draw the year class with
   rng.at(stream("water.regime"), regime_index, year); each day sets reach offsets = curve(day) × multiplier
   (only when it changes by > 1 cm, to limit flood recomputation). Emit FloodSeasonStarted{class} at the start
   of the rise and FloodSeasonEnded{class, peak_m} when it recedes.
3. Fertility renewal: on FloodSeasonEnded, for samples that were flooded this season, add a silt fertility
   delta (scaled by class; extreme years also emit a FloodDamage{area} event for later systems). Uses the
   ResourceField delta API from T1.4.1.1.
4. Author game/content/base/defs/river_regimes.json: base:regime.nile_inundation (approximate historical
   timing: rise from late June/July, peak around mid-September, receding through October–November; peak
   offset ~7 m at Aswan scaled to ~2–3 m in the Delta — use per-reach scale factors) with loc strings; add
   it to the ancient map's river_regimes.
5. Tests: deterministic year classes; curve interpolation; renewal applied only to flooded samples; events
   order; a 10-year headless run is identical twice.

Run all tests. Follow the Task workflow in CLAUDE.md (branch story/S1.3.2-inundation); open the PR.
```

---

## F1.4 — Resources & Fertility in the Simulation

### S1.4.1 — Resource fields with depletion
> *As a player, each city's land has fertility, vegetation and mineral deposits that I can inspect and that run out when used, so where I build matters.*

**Acceptance criteria**
- `ResourceDef` content exists.
- Per-city 4 m resource fields are sampled deterministically from the package.
- The extraction and renewal API works, and deltas are hashed and saved.

#### [ ] T1.4.1.1 — Resource fields, extraction and renewal
- **Model:** Sonnet · **Session:** [Cloud] · **Depends on:** T1.1.2.3, T1.4.1.2
```
Task T1.4.1.1 (Story S1.4.1). Read docs/architecture.md §15.8 and §15.4 (the same base+delta pattern).

1. ResourceDef content type "resource" (id, name_key, kind soil|stone|ore|fuel|vegetation, region layer name
   in the package, overlay color ramp [stops], renewable {none|silt|regrowth}, renewal params).
2. ResourceFields per active city tile: fertility, vegetation, wetness and one field per deposit
   ResourceDef, at 4 m (store u8 or u16, document), sampled from region and detail layers with seeded detail
   noise (stream "resource.detail"), immutable base + sparse deltas, like terrain.
3. API: sample(resource, city, LocalPos); extract(resource, city, area/brush, amount) → amount actually
   removed (deterministic order of samples); add_delta(...) for renewal and damage. Depletion persists.
4. Publish fields for overlays as SnapshotGrids ("resource.<id>.<tx>_<ty>", shared pointers).
5. Hash and save only deltas. Tests: determinism; extraction totals; renewal; save round-trip.

Run all tests. Follow the Task workflow in CLAUDE.md (branch story/S1.4.1-resources).
```

#### [ ] T1.4.1.2 — Author base ResourceDefs (data entry)
- **Model:** **Haiku** · **Session:** [Cloud] · **Depends on:** T1.1.1.1
```
Task T1.4.1.2 (Story S1.4.1). Data entry only — do not change C++ code. If the "resource" def type does not
exist yet, create the JSON anyway and run the validator after T1.4.1.1 lands (note this in the PR).

Create game/content/base/defs/resources.json with type "resource" defs (ids base:resource.<name>):
fertile_soil (soil, renewable silt), clay (soil), vegetation_reeds (vegetation, regrowth),
vegetation_palms (vegetation, regrowth), timber (vegetation, regrowth), stone_limestone, stone_sandstone,
stone_granite, stone_travertine (all stone), ore_copper, ore_tin, ore_gold, ore_silver, ore_lead, ore_iron
(all ore), natron (stone), bitumen (fuel), oil (fuel). Region layer names = the part after "resource." .
Give each an overlay color ramp (3 stops, readable on terrain; distinct hues per kind) and name/desc keys.
Add game/content/base/loc/en/resources.json with plain, grounded English names and one-sentence
descriptions (e.g., natron: "A natural salt from desert lake beds, used in mummification and glassmaking.").
Follow the Task workflow in CLAUDE.md (branch story/S1.4.1-resources).
```

---

## F1.5 — City Areas on Continuous Terrain

### S1.5.1 — Founding cities on the shared terrain
> *As a player, I found a city by choosing an 8 × 8 km area on the map. Neighbouring cities share the same land, so cities can grow next to each other.*

**Acceptance criteria**
- `CityArea` registry with a `FoundCity` command and validation reasons: overlap, not land, outside a detail region, or out of bounds.
- An active-city selection that activates tiles (plus a ring).
- A neighbours query.
- Saved and hashed.

#### [x] T1.5.1.1 — CityArea registry, FoundCity, active city
- **Model:** Sonnet · **Session:** [Cloud] · **Depends on:** T1.1.2.1
```
Task T1.5.1.1 (Story S1.5.1). Read docs/architecture.md §12, §15.1, §15.3 and docs/gdd.md §7.7 and §13.

1. CityArea entity/component: stable city id (u32, assigned in founding order), name (string; player-facing
   names are data, a default from a culture name list comes later — for now "City N"), origin TileCoord,
   4×4 tiles, founded_on (calendar date), sim mode (Agent|Aggregate per architecture §12; only the active city
   is Agent for now).
2. FoundCity{origin_tx, origin_ty, name} command: validates (all 16 tiles inside a detail region and the map
   bounds; ≥ 50% of samples land in the region raster; no overlap with existing areas; touching allowed);
   on failure emit a UiEvent with a loc key reason (city.found.error.overlap / .not_land / .no_detail /
   .out_of_bounds) and change nothing. Limit: 10 player cities (GDD §7.7) as a define, error key
   city.found.error.limit.
3. ActiveCity singleton + SetActiveCity{city_id} command: activates the city's 16 tiles + a 1-tile ring via
   WorldTerrain, deactivates the previous city's tiles; publishes the active city (id, origin) in the snapshot.
4. neighbors(city_id) → cities whose areas touch or are within one tile.
5. CitySim command_marshal entries found_city / set_active_city; Sim autoload helpers.
6. Tests: validation cases with the synthetic map; touching cities allowed; activation swaps tiles;
   save/load; determinism.

Run all tests. Follow the Task workflow in CLAUDE.md (branch story/S1.5.1-city-areas); open the PR.
```

---

## F1.6 — City-View Terrain Rendering

### S1.6.1 — Terrain, water and vegetation in the city view
> *As a player, the city view shows detailed, good-looking terrain with rivers, floods and vegetation at ≥ 60 FPS on my PC, so the land itself feels real.*

**Acceptance criteria**
- A CDLOD terrain renderer for the active tiles plus ring and horizon, which rebuilds only changed tiles.
- A splat shader.
- River, sea and flood water.
- Instanced vegetation.
- Budgets from architecture §15.9 met and recorded in `docs/perf.md`.

#### [x] T1.6.1.1 ★ — TerrainRenderer (CDLOD chunks, ring, horizon)
- **Model:** **Opus** · **Session:** [PC] · **Depends on:** T1.5.1.1, T1.1.2.3
```
Task T1.6.1.1 (Story S1.6.1). Architecture-critical rendering. Read docs/architecture.md §7.3, §11, §14,
§15.1, §15.3, §15.4 and §15.9, and gdext/src/agent_renderer.* + test_map_view.* for existing patterns.

Implement gdext class TerrainRenderer : Node3D:
1. Reads the "terrain.tiles" snapshot data (TileCoord, shared_ptr<const CityTile>, version) for the active
   city each frame; per tile a quadtree of chunks (e.g., 64×64-quad chunks at LOD0 = 4 m, coarser LODs by
   2×), selected by camera distance with a screen-space error metric; skirts (or morphing) hide seams between
   LODs and tiles. Meshes are built from the shared heights on a worker thread (Godot WorkerThreadPool) and
   swapped in on the main thread; rebuild only chunks whose tile pointer changed (dirty rects if available).
2. Ring and horizon: the 1-tile ring at reduced LOD; beyond it, the package 32 m DEM and then the region
   raster out to ~40 km, as a few coarse meshes (expose a CitySim accessor for the LoadedMap rasters).
3. Positions in the active city's local frame (architecture §15.3); normals from heights; a placeholder
   material (the splat shader is T1.6.1.2) using vertex colors by height and slope.
4. Physics: a HeightMapShape3D (or equivalent) for the active tiles for mouse picking (ray → terrain point);
   expose pick_terrain(screen_pos) → LocalPos to GDScript.
5. Replace the test ground plane in main.tscn when a world is loaded; keep the TestMapView path for
   new_test_map.
6. Performance: on this PC, active city + ring + horizon at 1080p ≤ 4 ms GPU for terrain, ≥ 60 FPS overall;
   record in docs/perf.md with chunk counts, triangle counts and VRAM.

Use the synthetic package until the real one exists (start a game with map_id of the synthetic MapDef
assembled into a dev content root — document how). Report what you verified and what the user should look
at. Follow the Task workflow in CLAUDE.md (branch story/S1.6.1-terrain-render).
```

#### [ ] T1.6.1.2 — Terrain splat shader
- **Model:** Sonnet · **Session:** [PC] · **Depends on:** T1.6.1.1, T1.4.1.1
```
Task T1.6.1.2 (Story S1.6.1). Read docs/architecture.md §15.8–§15.9 and CLAUDE.md (no large binaries outside
LFS).

1. A terrain shader (game/shaders/terrain.gdshader) blending 5–6 materials (alluvial soil, dry sand/desert,
   rock, grass/scrub, mud/wet bank, cultivated-ready dark soil) by slope, height above water level,
   fertility, vegetation and wetness. Feed the resource fields as small per-tile textures created from the
   SnapshotGrids (R8 textures, updated on pointer change).
2. Triplanar mapping where slope > threshold; macro variation noise to hide tiling; distance-based detail
   fade.
3. Textures: CC0 sources only (ambientCG or Poly Haven), downscaled to 1K, stored via Git LFS; record each
   source and license in docs/licenses.md (assets section).
4. A "terrain look" Resource with all tunables so the designer can adjust without code.
Run on this PC; capture before/after screenshots into docs/screenshots/ (LFS) and describe what to evaluate.
Follow the Task workflow in CLAUDE.md (same story branch).
```

#### [ ] T1.6.1.3 — Water rendering (rivers, sea, floods)
- **Model:** Sonnet · **Session:** [PC] · **Depends on:** T1.6.1.1, T1.3.1.2
```
Task T1.6.1.3 (Story S1.6.1). Read docs/architecture.md §15.6 and §15.9.

1. gdext WaterRenderer: river surface meshes built from rivers.json reaches in the active city + ring
   (triangle strips following the polyline with per-vertex width, at the current reach water level from the
   snapshot); a sea plane at sea level clipped to the map; flooded areas from the "water.flood.*" grids as
   meshes at the flood level (rebuild on pointer change, on a worker thread).
2. A water shader: depth-based color and transparency (using the depth texture), gentle normal-mapped waves,
   flow direction along the reach, shoreline foam/wet-sand blending. Nile silt color for rivers (brownish),
   bluer sea.
3. Debug: console river_level changes are visible within a second at 4×.
Run on this PC; screenshots to docs/screenshots/; perf numbers to docs/perf.md.
Follow the Task workflow in CLAUDE.md (same story branch).
```

#### [ ] T1.6.1.4 — Vegetation scatter
- **Model:** Sonnet · **Session:** [PC] · **Depends on:** T1.6.1.2
```
Task T1.6.1.4 (Story S1.6.1, last task). Read docs/architecture.md §15.8–§15.9.

1. gdext VegetationRenderer: deterministic scatter per tile cell from the vegetation and wetness fields
   (positions from rng::hash64 of tile + cell, no randomness at run time) of date palms, reeds/papyrus, acacia
   and scrub, with density and type rules in a data file (game/content/base/defines/vegetation.json).
2. MultiMesh per type per tile, distance culling and 2–3 LODs (the far LOD a billboard or a very low-poly
   mesh); remove instances where terrain edits or (later) buildings clear them — expose a clear mask hook.
3. Meshes: simple low-poly placeholders built in code or CC0 models (LFS, licenses recorded).
4. Budget: vegetation ≤ 1.5 ms GPU with the default density over the whole city at 1080p.
Run on this PC; screenshots, perf to docs/perf.md. Follow the Task workflow in CLAUDE.md; last task of
S1.6.1, so open the PR.
```

### S1.6.2 — Resource and terrain overlays
> *As a player, I can toggle CS2-style overlays for fertility, each deposit type, elevation and slope, each with a legend, so I can plan where to farm, quarry and build.*

**Acceptance criteria**
- An overlay toggle UI, one overlay at a time, with a legend.
- Overlays are driven by `ResourceDef` color ramps.
- Elevation contours and slope.
- All text uses loc keys.

#### [ ] T1.6.2.1 — Overlay framework and UI
- **Model:** Sonnet · **Session:** [PC] · **Depends on:** T1.6.1.4
```
Task T1.6.2.1 (Story S1.6.2). Read docs/architecture.md §9, §15.8–§15.9 and docs/gdd.md §12.7.

1. In the terrain shader: an overlay mode uniform + a per-tile data texture + a color ramp texture; modes:
   none, fertility, vegetation, wetness, any deposit ResourceDef, elevation (with contour lines every N m),
   slope (buildability bands), water depth.
2. An OverlayController (GDScript) listing modes from content (ResourceDefs) and a panel
   (game/ui/hud/overlay_panel.tscn) with icons/labels via loc keys, a legend with the ramp and value labels,
   hotkeys (O to cycle, Esc to clear).
3. Hovering the terrain with an overlay active shows a tooltip with the sampled value
   (pick_terrain + a CitySim sample query).
Run on this PC; screenshots of each overlay. Follow the Task workflow in CLAUDE.md (branch
story/S1.6.2-overlays); open the PR.
```

---

## F1.7 — Region Map View

### S1.7.1 — Region terrain with a paper-map style
> *As a player, the region view shows the whole map as 3D terrain that turns into a paper-map style as I zoom out, with rivers, coasts and historical place names.*

**Acceptance criteria**
- A region scene rendering the region rasters with chunked LOD.
- A map-style blend driven by camera height.
- River and coast lines.
- `LabelDef` content per era shown with collision-avoiding placement.

#### [ ] T1.7.1.1 — Region view scene
- **Model:** Sonnet · **Session:** [PC] · **Depends on:** T1.6.1.1, T1.2.6.1
```
Task T1.7.1.1 (Story S1.7.1). Read docs/architecture.md §15.3 and §15.9.

1. game/scenes/region/region_view.tscn: reuse TerrainRenderer in a "region source" mode (region rasters at
   1 km plus the 32 m detail DEM where available, chunked by distance), coordinates = world meters relative
   to the map center; a region camera (reuse CityCamera with a separate settings resource: zoom 5–2,500 km,
   pitch curve flatter at height).
2. River lines from rivers.json (width scaled with zoom) and coastlines from the land mask edge.
3. City area outlines (from the snapshot's city list) as projected rectangles.
4. Performance: ≥ 60 FPS over the whole map on this PC.
Run with the real ancient_near_east package; screenshots. Follow the Task workflow in CLAUDE.md (branch
story/S1.7.1-region-view).
```

#### [ ] T1.7.1.2 — Map-style blending and labels
- **Model:** Sonnet · **Session:** [PC] · **Depends on:** T1.7.1.1, T1.7.1.3
```
Task T1.7.1.2 (Story S1.7.1, last task). Read docs/architecture.md §9 and §15.9; GDD §13 (E1-c: both styles,
blending by zoom).

1. Region terrain shader: blend factor from camera height (tunable curve): near = shaded 3D terrain with
   satellite-like colors from land cover; far = "paper map": parchment base, hillshade in ink tones, coast
   ink lines and sea hatching, river lines in blue ink, desert stippling, a subtle paper texture (CC0, LFS).
2. Labels: content type "label" (id, name_key, kind: region|river|sea|settlement|mountain, era range,
   lon/lat point or polyline for curved river/sea names, min/max zoom, priority). Render with Label3D or a
   2D overlay layer; greedy collision avoidance by priority; curved text along rivers is optional (straight is
   fine for now).
3. Settlements show as small glyphs; labels are localized via tr().
Run on this PC; screenshots near/mid/far. Follow the Task workflow in CLAUDE.md; last task of S1.7.1, so open
the PR.
```

#### [ ] T1.7.1.3 — Author Ancient Near East labels (data entry with sources)
- **Model:** **Haiku** · **Session:** [Cloud] · **Depends on:** T1.1.1.1
```
Task T1.7.1.3 (Story S1.7.1). Data entry only. If the "label" def type does not exist yet, write the JSON
and validate after T1.7.1.2 lands (note this in the PR).

Create game/content/base/defs/labels_ancient_near_east.json (type "label", era range base:era.ancient only)
and game/content/base/loc/en/labels_ancient_near_east.json with historically attested names around
3000–2500 BCE (use common English scholarly forms; ancient/native forms can be added later):
- Regions: Upper Egypt, Lower Egypt, Nubia (Ta-Seti), Sinai, Canaan, Mesopotamia, Sumer, Akkad, Elam,
  Subartu, the Eastern Desert, the Western Desert.
- Rivers/waters: Nile, Euphrates, Tigris, Karun, the Great Green (Mediterranean), the Lower Sea (Persian
  Gulf), Lake Moeris (Faiyum).
- Settlements (approximate coordinates): Memphis (Inebu-hedj), Thinis, Abydos, Nekhen (Hierakonpolis),
  Nekheb, Thebes (Waset; minor in this period — low priority), Buto, Sais, Heliopolis (Iunu), Elephantine;
  Ur, Uruk, Eridu, Lagash, Girsu, Umma, Kish, Nippur, Shuruppak, Larsa, Susa, Mari, Ebla, Byblos, Jericho,
  Megiddo.
Each def: id base:label.<name>, kind, lon/lat, a "source" field naming where the coordinates come from, priority (capital-class 1, major 2, minor 3), min/max zoom bands. Keep a SOURCES note in the
PR description. Follow the Task workflow in CLAUDE.md (branch story/S1.7.1-region-view) and ask the user to
review names and dates.
```

### S1.7.2 — Moving between views and founding a city
> *As a player, I zoom out from my city into the region view, pick an 8 × 8 km site with live feedback, found a city, and zoom back into it.*

**Acceptance criteria**
- Zooming out past a threshold cross-fades to the region view, and clicking a city area returns to it.
- A founding tool with a site preview and validity reasons.
- New game starts in the region view.

#### [ ] T1.7.2.1 — View switching and the founding flow
- **Model:** Sonnet · **Session:** [PC] · **Depends on:** T1.7.1.2, T1.5.1.1
```
Task T1.7.2.1 (Story S1.7.2). Read docs/architecture.md §11, §15.3, §15.9 and docs/gdd.md §7.7.

1. A ViewManager (autoload or main-scene script): city view ↔ region view; zooming the city camera beyond
   its max distance (or a hotkey, M) cross-fades to the region view centered on the city; clicking a city
   area (or double-click) cross-fades back and sends set_active_city. Keep both scenes loaded; pause
   rendering of the hidden one.
2. New game flow: Sim.new_game(map_id = base:map.ancient_near_east, era = ancient) starts in the region view
   with the camera over the Nile Delta (a "start_focus" lon/lat on the MapDef).
3. Founding tool (button + hotkey F in the region view): an 8×8 km square snapped to the tile grid follows
   the mouse; it is colored valid/invalid using a CitySim query validate_city_site(tx, ty) that mirrors the
   FoundCity rules (add that query in C++, sharing the validation code); a tooltip shows the reason key;
   click → found_city command → on success set it active and zoom into it.
4. Loc keys for every string; Esc cancels the tool.
Run on this PC with the real map: found a city in the Delta, try invalid sites (sea, outside detail
regions, overlapping), zoom out and back. Report what you verified. Follow the Task workflow in CLAUDE.md
(branch story/S1.7.2-view-switch); open the PR.
```

---

## F1.8 — Instant Terraforming

### S1.8.1 — Terraforming tools
> *As a player, I can raise, lower, flatten, smooth and level terrain inside my city instantly, paying for it upfront, so I can shape the land (GDD §7.5).*

**Acceptance criteria**
- A deterministic `Terraform` command with brush operations, restricted to the active city area.
- The cost is computed from volume moved and reported (charged once the treasury exists in E4).
- A tool UI with brush preview.
- Survives save → load.

#### [x] T1.8.1.1 — Terraform command and brushes (core)
- **Model:** Sonnet · **Session:** [Cloud] · **Depends on:** T1.1.2.3, T1.5.1.1
```
Task T1.8.1.1 (Story S1.8.1). Read docs/architecture.md §4, §5.3, §15.4 and docs/gdd.md §7.5, §9.2.

1. Command Terraform{city_id, op (raise|lower|flatten|smooth|level), center LocalPos, radius_m (2–200),
   strength (0–1), target_height_m (flatten/level)}: applies a smooth falloff brush through the delta API
   from T1.1.2.3; deterministic sample order; clamps the result to [-50 m, +200 m] relative to base and never
   below the river bed at channel samples (rivers stay fixed).
2. Jurisdiction: only samples inside the city's area are changed (GDD §9.2 — tools work only inside the
   player's jurisdiction); outside → no-op with a UiEvent reason.
3. Cost: volume moved (m³) × a cost per m³ from defines/terrain.json; emit TerraformApplied{volume, cost}
   (the treasury that charges it arrives in E4 — note this in code).
4. Water interaction: if edits touch a floodplain, mark the affected reach for flood recomputation.
5. command_marshal entry "terraform"; tests: each op; jurisdiction clamp; determinism; save/load; cost.

Run all tests. Follow the Task workflow in CLAUDE.md (branch story/S1.8.1-terraform).
```

#### [ ] T1.8.1.2 — Terraforming tool UI
- **Model:** Sonnet · **Session:** [PC] · **Depends on:** T1.8.1.1, T1.6.1.2
```
Task T1.8.1.2 (Story S1.8.1, last task). Read CLAUDE.md (GDScript style) and docs/architecture.md §9, §11.

1. game/ui/tools/terraform_tool.tscn (+ .gd): tool panel with op buttons, radius and strength sliders,
   target height (picked with Ctrl+click for flatten/level), hotkeys; a brush preview (a projected decal or
   shader ring on the terrain) following pick_terrain.
2. While the mouse is held, submit Terraform commands at a fixed rate (e.g., 15 per second at 1×; scale
   strength by real time so the feel does not depend on frame rate); show the running cost from
   TerraformApplied events.
3. Disable outside the active city (cursor shows why).
Run on this PC; verify edits look right, chunks rebuild without hitches, save → load keeps edits.
Follow the Task workflow in CLAUDE.md; last task of S1.8.1, so open the PR.
```

---

## F1.9 — Integration: Agents on Real Terrain

### S1.9.1 — Debug agents walk on the Nile Delta
> *As a developer, the walking-skeleton agents walk on the real city terrain, avoiding water and steep slopes, with floods and edits changing where they can go. This proves terrain, water, simulation and rendering work together.*

**Acceptance criteria**
- A walkability grid is derived from terrain and water at 4 m and updated on edits and floods.
- The walking skeleton uses it in a world game.
- Agents are placed on the terrain height.
- End-to-end PC verification and performance are recorded.

#### [x] T1.9.1.1 — Terrain walkability for the walking skeleton
- **Model:** Sonnet · **Session:** [Cloud] · **Depends on:** T1.3.1.2, T1.8.1.1
```
Task T1.9.1.1 (Story S1.9.1). Read docs/architecture.md §4, §15 and sim/include/citysim/debug/*.

1. A WalkGrid interface in citysim::debug (width, height, cell_size, walkable(index), cell_at, center) that
   TestMap implements and a new TerrainWalkGrid implements for the active city (2048×2048 cells at 4 m;
   walkable = not water (including floods) and slope < a define, e.g. 35°).
2. TerrainWalkGrid updates incrementally on WaterChanged and terrain edits (dirty rects), with a version
   number the planner checks to re-path agents whose path crosses newly blocked cells.
3. The walking skeleton picks the grid source: TestMap when test_map_size > 0, the TerrainWalkGrid when a
   world is loaded. Agent positions get y from height_at_local; the "agents.xform" channel carries real y.
4. Tests on the synthetic map: agents never stand on water; after raising the river level, agents in newly
   flooded cells move out or re-path; determinism and save/load continuation still pass; headless benchmark
   "agents=10000,map=world" added to citysim_headless --bench.

Run all tests. Follow the Task workflow in CLAUDE.md (branch story/S1.9.1-agents-on-terrain).
```

#### [ ] T1.9.1.2 — End-to-end verification on the PC
- **Model:** Sonnet · **Session:** [PC] · **Depends on:** T1.9.1.1, T1.7.2.1, T1.8.1.2, T1.3.2.1, T1.6.2.1
```
Task T1.9.1.2 (Story S1.9.1, last task; closes E1). Read docs/backlog/E1-terrain.md (Epic acceptance criteria)
and docs/perf.md.

On this PC with a Release build and the real ancient_near_east package:
1. New game → region view → found a city at the Delta apex near Memphis → city view.
2. spawn_agents 10000; watch them walk on the terrain; measure FPS (far/near), sim tick ms at 1× and 4×, GPU
   frame time, VRAM; add rows to docs/perf.md and compare with architecture §14 and §15.9 budgets.
3. Run one calendar year at 4×: confirm the inundation rises and recedes on schedule, the flood extent and
   agents' walkable area change, and fertility increases where it flooded (fertility overlay before/after
   screenshots).
4. Terraform a hill and a pit; save; load; confirm terrain, water level, year class and agents match
   (state_hash before/after).
5. Check every Epic acceptance criterion and tick the ones verified; list anything not met as Follow-ups
   at the end of this file with evidence.
Report verified results honestly. Follow the Task workflow in CLAUDE.md; last task of S1.9.1, so open the PR.
```

---

## Follow-ups
_Filled in by T1.9.1.2 and reviews._
