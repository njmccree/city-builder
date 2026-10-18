# sim/tools

Command-line tools built on the core: `headless/` (citysim_headless), `mapgen/` (citysim_mapgen) and `validate/` (citysim_validate).

## citysim_headless

Runs the simulation without a front end (currently the demo scenario in `headless/demo_scenario.*`, until the base game has real systems).

```
citysim_headless --seed 42 --ticks 500                  # prints "tick hash" every 100 ticks, then "final <tick> <hash>"
citysim_headless --seed 42 --ticks 500 --record-log run.txt
citysim_headless --replay run.txt                       # same final hash as the recording
```

The log is a text format (documented in `headless/input_log_text.hpp`); S0.5 replaces it with the binary container. `--hash-every 0` prints only the final hash.

## citysim_mapgen

Makes map packages (architecture §15.2). Today it only has the synthetic mode, which needs no real data, so tests, CI and cloud sessions can use the whole terrain stack. Real packages are built by the PC pipeline (`tools/terrain`).

```
citysim_mapgen --synthetic --out DIR [--seed N] [--size-km N]
```

- `--out DIR`: the folder to write the package into (created if needed).
- `--seed N`: the map seed (default 1). One seed always gives the same files, byte for byte; another seed gives a different map.
- `--size-km N`: the map is N x N km, a whole number from 16 to 1000 (default 96).

The map is a river valley running south to north, 3 km wide in the south and widening into a delta of about 29 km, with desert plateaus 60-120 m above the valley floor, a coast along the north edge, and one river whose main channel splits into three delta branches. It writes the region layers (`elevation`, `land_mask`, `fertility`, `vegetation`, `wetness`, `stone_limestone`, `ore_copper`), detail tiles over the valley and delta (the middle 30% of the width, from 40% to 90% of the way north), `rivers.json` and `lakes.json` (empty), and `package.json` with all checksums and the package hash. It also writes `map_def.json` into the same folder: a matching map definition (`test:map.synthetic`, package folder `synthetic`) that is not part of the package. A test content root is a mod folder with that file under `defs/` and the package copied to `maps/synthetic/`.

Raster types: region elevation is i16 in 0.25 m steps; detail elevation is i16 in 0.125 m steps; every other layer is u8 (the land mask is 0 or 1, the rest are 0-1 in steps of 1/255). Floating-point output uses only `+ - * /`, `sqrt` and `floor`/`round`, plus `citysim::rng` hashes, so it does not depend on a math library.

### In tests

`ctest` first runs `synthetic_map_is_generated`, which writes the default package (seed 1, 96 km, about 1.7 MB, 3-4 s in a debug build) to `<build>/test_data/synthetic_map`. Tests that need it carry the Catch2 tag `[synthetic_map]`, are registered as `synthetic_map: <name>` and require that fixture. They get its path from `citysim::test::synthetic_map_dir()` (`sim/tests/support/synthetic_map.hpp`). For a smaller map in a test, call `mapgen::write_synthetic_package` (`mapgen/synthetic_map.hpp`) with `size_km = 16` and a `TempDir`.
