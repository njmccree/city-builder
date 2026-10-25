# game/content

All game content is data (architecture §8). The base game is the `base` mod in `base/`; other mods live in `mods/`. See `base/README.md` and `mods/README.md`.

## Map packages

A map (content type `"map"`, `MapDef`) names a **map package**: generated data in `<mod folder>/maps/<package>/` (architecture §15.2). Packages are **not committed**. `.gitignore` ignores `game/content/**/maps/*/`, and each `maps/` folder keeps a `README.md`. The offline pipeline (`tools/terrain`) builds real packages on the PC; `citysim_mapgen --synthetic` builds a small synthetic one for tests and cloud sessions.

### The map definition

```json
{ "type": "map", "id": "base:map.ancient_near_east",
  "name_key": "map.ancient_near_east.name", "desc_key": "map.ancient_near_east.desc",
  "package": "ancient_near_east",
  "bounds": { "min_lon": 25.0, "min_lat": 20.0, "max_lon": 55.0, "max_lat": 40.0 },
  "region_cell_size_m": 1000,
  "map_seed": 1234567,
  "detail_regions": [
    [[29.5, 29.0], [32.5, 29.0], [32.5, 31.8], [29.5, 31.8], [29.5, 29.0]]
  ],
  "river_regimes": ["base:river.nile"] }
```

- `package` is the folder name under `maps/`: letters, digits, `_` and `-`.
- `region_cell_size_m` is optional (default 1000). `map_seed` is required and drives the terrain's micro-detail, so a map looks the same in every game; it is not the game seed.
- `detail_regions` are closed polygons in lon/lat (the last point repeats the first, at least three corners) inside the bounds. A city can only be founded where all of its tiles lie in a detail region.
- `river_regimes` are plain id strings until the river regime definition type exists (T1.3.2.1).
- `name_key` and `desc_key` need text in `en`, like any definition.

### The package folder

```
maps/ancient_near_east/
  package.json
  region/<layer>.csr        elevation, land_mask, fertility, vegetation, wetness, deposits
  detail/<tx>_<ty>.csr      65 x 65 samples, 32 m spacing, 2,048 m tiles
  rivers.json
  lakes.json
```

`package.json` (all fields required; unknown fields are ignored):

```json
{
  "format": 1,
  "map_id": "base:map.ancient_near_east",
  "pipeline_version": "1.0.0",
  "sources": [
    { "name": "Copernicus DEM GLO-30", "version": "2021_1", "license": "Copernicus DEM licence" }
  ],
  "projection": { "type": "laea", "lat_0": 30.0, "lon_0": 38.0 },
  "bounds_m": { "width": 3000000.0, "height": 2000000.0 },
  "region": {
    "cell_size_m": 1000.0, "width": 3000, "height": 2000,
    "layers": { "elevation": "region/elevation.csr", "land_mask": "region/land_mask.csr" }
  },
  "detail": { "tile_size_m": 2048.0, "sample_spacing_m": 32.0, "tiles": [[10, 20], [11, 20]] },
  "rivers": "rivers.json",
  "lakes": "lakes.json",
  "files": {
    "detail/10_20.csr": "0123456789abcdef",
    "region/elevation.csr": "89abcdef01234567"
  },
  "package_hash": "fedcba9876543210"
}
```

- Paths are relative to the package folder, use `/` and never contain `..`.
- Every checksum is the XXH3-64 of the file's bytes as 16 lower-case hex digits. `files` lists every file except `package.json`.
- `package_hash` is the XXH3-64 of the text made of one line `<path>=<checksum>\n` per file, in sorted path order. It identifies the data; saves store it.
- The `.csr` byte layout and the exact hash definition are in `docs/architecture.md` §15.2. The loader and verifier are `citysim::world::verify_package`.

### `rivers.json` and `lakes.json`

World-frame coordinates: meters from the south-west corner of the map, `x` east and `z` north. Written by the pipeline and by `citysim_mapgen --synthetic`; read by the water systems (T1.3.1.1).

```json
{
  "format": 1,
  "rivers": [
    {
      "id": "synthetic_nile", "name": "Synthetic Nile",
      "reaches": [
        { "id": "main", "name": "Nile", "order": 3, "discharge_m3s": 3000.0,
          "downstream": ["west", "center", "east"],
          "vertices": [
            { "x": 46145.6, "z": 0.0, "width_m": 200.0, "depth_m": 6.0, "water_level_m": 38.5 }
          ],
          "floodplain": [ [ [46000.0, 100.0], [47000.0, 100.0], [47000.0, 900.0], [46000.0, 100.0] ] ] }
      ]
    }
  ]
}
```

- Reaches are numbered in file order, rivers first; that **reach index** names a reach in the game (`WaterState`, river-level commands). Ids are unique within a river, `downstream` names reaches of the same river.
- A **reach** is a polyline from upstream to downstream. `vertices` are in that order; `width_m` and `depth_m` are the channel at that vertex, and `water_level_m` is the base water surface above sea level (it does not rise going downstream).
- `order` is a stream order (a bigger number is a bigger river) and `discharge_m3s` the mean discharge.
- `downstream` lists the reaches that start where this one ends (their first vertex equals this reach's last vertex). A delta is one reach with several downstream reaches.
- `name` (optional, reaches) is the reach's display name; `floodplain` (optional) lists closed outer rings `[x, z]` of the land beside the channel that can flood (written by the pipeline). The game reads it for the flood extents (architecture §15.6): a ring needs at least 3 different points, and a repeated last point (as in GeoJSON) is dropped. A reach without a floodplain never floods beyond its channel.
- `lakes.json` is `{ "format": 1, "lakes": [ { "id": "lake_moeris", "name": "Lake Moeris", "water_level_m": -3.2, "area_km2": 214.5, "rings": [ [[x, z], ...], [[x, z], ...] ] } ] }`: `rings[0]` is the outer boundary and any others are islands (holes), each ring closed. The synthetic package has `"lakes": []`.

## Resource definitions

A resource (content type `"resource"`, `ResourceDef`) is something in the land that the player can look at and use up: soil, stone, ore, fuel, vegetation. Its amount at every 4 m sample of a city is a *resource field*, taken from the map package's region layer of the same name (architecture §15.8). In a defs file:

```json
{ "type": "resource", "id": "base:resource.ore_copper",
  "name_key": "resource.ore_copper.name", "desc_key": "resource.ore_copper.desc",
  "kind": "ore",
  "layer": "ore_copper",
  "ramp": ["#2e1f14", "#b87333", "#f2c58a"],
  "renewable": "none",
  "renewal_per_year": 0.0 }
```

- `kind`: `soil`, `stone`, `ore`, `fuel` or `vegetation`.
- `layer` (optional): the package region layer. Default: the id's name without a leading `resource.` (`ore_copper`). Two resources may name one layer and then share a field (the fertile-soil resource uses the `fertility` layer; the package has one `vegetation` layer). `fertility`, `vegetation` and `wetness` fields exist whatever the definitions say. The offline pipeline names a deposit's layer after its resource the same way (`tools/terrain`: `layer_name`).
- `ramp`: 2 to 8 colors `"#rrggbb"` from little to much, for the overlay.
- `renewable` (`none`, `silt` or `regrowth`, default `none`) and `renewal_per_year` (0 to 1, default 0): how the resource comes back; read by the systems that renew it (silt: the flood).
- Both keys (`name_key`, `desc_key`) need text in `en` like any definition.

## Defines

`defines/*.json` files hold numbers the simulation reads. Later mods override earlier ones field by field.
- `defines/time.json`: tick rate, speeds, day length (see `time::TimeDefines`).
- `defines/terrain.json`: the channel profile of the synthesized terrain, the flood fill's speed and the terraforming numbers: `channel_edge_depth_fraction` (0.3: the share of a river's depth left at the channel's edge, 0 to 1) and `bank_width_m` (24: how far beyond the edge the bank climbs back to the ground, 1 to 200). They shape the terrain, so a save made with other values warns that the terrain may differ. `flood_samples_per_tick` (250000, 1000 to 100000000) is how many 4 m samples the flood fill may look at per tick; it changes when a flood appears, never where it ends up. `terraform_step_m` (1.0, 0.01 to 50) is how far a full-strength Raise or Lower stroke moves the ground at the middle of the brush, and `terraform_cost_per_m3` (0.1, 0 to 1000000) what each cubic meter of earth moved costs; the cost is only reported until the treasury exists (E4). `walk_max_slope` (0.7, 0.05 to 5) is the steepest ground (rise over run, 0.7 is about 35 degrees) the debug agents walk on.
- `defines/world.json`: `max_player_cities` (default 10), `min_land_fraction` (0.5: how much of a city area must be
  land) and `active_ring_tiles` (1: tiles around the active city that are synthesized too).
