# Performance

Budgets come from `docs/architecture.md` §14; measurements are recorded here. Cloud numbers are **indicative**
(a shared container, no GPU); the PC numbers on the reference machine are **authoritative** and are added by the
PC Tasks (T0.8.1.4).

## Budgets (reference machine: GTX 1660 Ti, 6 GB VRAM)

| What | Budget |
|---|---|
| Rendering | 60 FPS at 1080p with 10,000 visible agents |
| Simulation at 4x (80 tps) | <= 12.5 ms per tick on average, with 50,000 simulated agents |
| VRAM | <= 4.5 GB in normal play |

## How to measure

```
cmake --preset linux-release && cmake --build --preset linux-release --target citysim_headless
build/linux-release/sim/tools/headless/citysim_headless --bench "agents=50000,ticks=1000,threads=0,map=512"
```

`--bench` runs the walking skeleton (`citysim::debug`: grid A* pathfinding, wandering agents, `parallel_for`
movement) with no pacing and prints milliseconds per tick (average, median, 95th percentile, maximum), path
searches per tick and ticks per second. The "last half" row leaves out the start of the run, when every agent is
given its first path: with 200 searches allowed per tick, 50,000 agents need about 250 ticks to all get one, so
that part of the run is much slower than the steady state. Budgets are about the steady state.

Keys: `agents`, `ticks`, `threads` (0 = hardware default), `map` (cells per side), `searches` (path searches per
tick). The result (the final hash) is the same for every thread count; only the speed changes.

## Results

All runs: release build (GCC, `-O2`-style `CMAKE_BUILD_TYPE=Release`), map 512x512 cells, 200 path searches per
tick at most, seed 1, 1000 ticks. Each row is one run. `p95` and `max` include the start-up ramp in the "all ticks"
columns.

### 2026-10-17, commit `bd8962c`, cloud container (indicative)

Machine: Intel Xeon @ 2.10 GHz, 4 hardware threads, 15 GB RAM, Linux, shared container.

| Agents | Threads | ms/tick all ticks (avg / p50 / p95 / max) | ms/tick last half (avg / p50 / p95 / max) | Path searches / tick | Ticks / s |
|---:|---:|---|---|---:|---:|
| 10,000 | 1 | 3.48 / 0.79 / 5.33 / 83.0 | 0.89 / 0.79 / 1.54 / 4.07 | 10.3 | 288 |
| 10,000 | 0 (4) | 3.02 / 0.40 / 4.90 / 80.8 | 0.55 / 0.46 / 1.12 / 3.51 | 10.3 | 331 |
| 50,000 | 1 | 21.00 / 6.95 / 70.96 / 126.6 | 6.84 / 5.96 / 12.94 / 25.04 | 51.0 | 48 |
| 50,000 | 0 (4) | 17.75 / 3.67 / 67.99 / 126.9 | 3.81 / 3.57 / 5.57 / 13.69 | 51.0 | 56 |

Reading it:

- **Steady state with 50,000 agents is 3.8 ms per tick on average with 4 threads** (6.8 ms with one), against a
  budget of 12.5 ms. There is headroom for the real game's systems, on this machine and without a GPU in the way.
- **The start-up ramp is the expensive part**: the first few hundred ticks, while every agent is given a path, average
  about 18-21 ms per tick with 50,000 agents and peak near 127 ms. The cause is the cost of path searches on a
  512x512 map: each search is a few hundred microseconds to a few milliseconds, 200 of them are allowed per tick, and
  they run on one thread (they must be in a fixed order). A real game should spread path requests over time or
  search hierarchically; the walking skeleton only proves the architecture.
- Threads help the movement step (about 1.8x on 4 hardware threads for the last half) but not the planner.
- The final hash of each pair of runs with different thread counts is identical (`c854ee2a1160dd12` for 10,000
  agents, `a8096e84c7945c0f` for 50,000), as the determinism rules require.

### PC, reference machine (authoritative)

2026-10-17, commit `cff8f93` plus the `windows-release-gdext` preset. Machine: AMD Ryzen 5 2600 (6 cores, 12
threads), 64 GB RAM, NVIDIA GeForce GTX 1660 Ti (6 GB), Windows 10, MSVC 19.44 (Visual Studio 2022 17.14),
Godot 4.7.2. Release build (`windows-release-gdext`, `/O2`).

#### Headless benchmarks (`citysim_headless --bench`, map 512x512, 200 path searches per tick at most, seed 1, 1000 ticks)

| Agents | Threads | ms/tick all ticks (avg / p50 / p95 / max) | ms/tick last half (avg / p50 / p95 / max) | Path searches / tick | Ticks / s |
|---:|---:|---|---|---:|---:|
| 10,000 | 1 | 3.66 / 0.68 / 5.57 / 95.5 | 0.78 / 0.68 / 1.21 / 3.93 | 10.3 | 273 |
| 10,000 | 0 (12) | 3.32 / 0.29 / 5.10 / 92.8 | 0.38 / 0.29 / 0.80 / 3.53 | 10.3 | 302 |
| 50,000 | 1 | 21.59 / 7.58 / 72.60 / 120.7 | 7.65 / 7.38 / 9.48 / 17.7 | 51.0 | 46 |
| 50,000 | 0 (12) | 16.67 / 1.80 / 68.84 / 115.5 | 2.04 / 1.70 / 3.84 / 11.9 | 51.0 | 60 |

The final hash is identical for every thread count on this machine (`749e8cfc1ebb0243` for 10,000 agents,
`8abf5835701c808a` for 50,000). **It differs from the cloud run** (`c854ee2a1160dd12` and `a8096e84c7945c0f`, GCC on
Linux): the same seed gives a different game on a different compiler and platform. See the follow-ups in
`docs/backlog/E0-foundation.md`.

#### In the game (Release extension, 1920x1061 window, test map 512x512 cells = 2,048 m)

Measured by a temporary script that ran `main.tscn` windowed on the GPU, spawned the agents with the console
(`new_test_map 1 512`, `spawn_agents N`), let them settle for 15 s at 4x (the path-search ramp-up), then sampled
every frame for 4 s. "Far" is the camera zoomed out over the whole map (1,400 m), "near" is 60 m above the middle.
Frame times are in milliseconds; `p99` is the 99th percentile.

| Agents | Speed | View | FPS | Frame p99 / max (ms) | Sim ticks/s | Tick ms (avg, from the overlay) | VRAM (MB) |
|---:|---:|---|---:|---|---:|---:|---:|
| 10,000 | 4x | far | 505 | 2.5 / 2.7 | 79.9 | 0.60 | 155 |
| 10,000 | 4x | near | 431 | 2.5 / 2.9 | 80.1 | 0.65 | 155 |
| 10,000 | 1x | far | 506 | 2.2 / 2.8 | 20.0 | 0.86 | 155 |
| 10,000 | 1x | near | 430 | 2.5 / 3.5 | 20.0 | 0.97 | 155 |
| 50,000 | 4x | far | 184 | 6.7 / 7.3 | 80.1 | 2.31 | 158 |
| 50,000 | 4x | near | 184 | 6.5 / 7.5 | 80.1 | 2.68 | 158 |
| 50,000 | 1x | far | 189 | 6.6 / 7.4 | 20.0 | 3.01 | 158 |
| 50,000 | 1x | near | 189 | 6.8 / 7.8 | 20.0 | 2.94 | 158 |

#### Against the budgets

| Budget | Result | Met? |
|---|---|---|
| 60 FPS at 1080p with 10,000 visible agents | 430 to 506 FPS | yes |
| Simulation at 4x (80 tps): <= 12.5 ms per tick with 50,000 agents | 2.3 to 2.7 ms in the game; 2.0 ms headless (7.6 ms on one thread) | yes |
| VRAM <= 4.5 GB | 158 MB (this scene is small) | yes |

Caveats, so these numbers are not over-read:

- The window was run with the Godot **editor** binary and the editor flavor of the extension built in Release; an
  exported release template has less overhead, so real FPS should be the same or higher.
- Capsules are 8 sided and cast no shadows. With Godot's default capsule (about a thousand triangles) and shadows
  the same 10,000 agents ran at **26 FPS even with the simulation paused**: the GPU, not the simulation, was the
  limit, and cutting the triangle count and the shadow pass gave a 20x gain (26 to about 500 FPS). Keep this in mind
  when real agent meshes arrive.
- A **Debug** build of the simulation is far too slow for judging performance: 10,000 agents ticked at 9 per second
  (about 65 ms per tick) instead of the full 20, so the debug overlay showed a low "Sim ticks/s" and the motion
  stuttered. Measure with Release only.
- The shown ticks/s is smoothed and takes about 3 s to settle after a speed change.
- The start-up ramp (every agent receiving its first path) is still the expensive part, as in the cloud run: with
  50,000 agents the first ticks average several times the steady state (headless, all ticks 16.7 ms against 2.0 ms for
  the last half; peak 115 ms). In the game this was spent at 4x before sampling.
- Not measured: the exported game (no export templates are installed), CPU use of the main thread on its own, and
  frame pacing with vsync on.

#### Save and load in the middle of a walk

- In the game: `save`, `hash`, run on, `load`, `hash` with 10,000 agents gave identical hashes (tests/test_agents.gd,
  also run against the Release extension).
- Natively (`game_session_tests`, "a game with agents ends in the same state with or without a save and load in the
  middle"): 2,000 agents walked 60 ticks, saved, walked 60 more (state A). The same game walked 120 ticks without a
  save has the same hash as A. A fresh game that loads the save has A's hash at tick 60 and again at tick 120.

## Terrain and water (E1), cloud numbers

Indicative only (a shared cloud machine, Release build, gcc); the PC run is authoritative. Run the hidden benchmarks
with `citysim_tests "[.bench]"`.

- Synthesizing one 4 m city tile (generator version 2, with channel carving): best 13.6 ms, mean 15.3 ms (limit
  30 ms).
- One tick of flood fill (`FloodMap::advance` with the default 250,000-sample budget) while a river 20 m above its base
  level fills the whole floodplain of 3 x 3 active tiles: 15 calls, mean 4.3 ms, worst 15.9 ms (limit 20 ms). The calls
  that spread the fill are the expensive ones (about 12-16 ms); scanning the channel and the final steps cost about 1 ms.
  At 4x speed (80 ticks per second) the architecture allows 12.5 ms per tick on average (§14), so a flood that is
  spreading uses most of that for the few seconds it takes; lower `flood_samples_per_tick` in `defines/terrain.json`
  if that matters.
- One Terraform stroke with the biggest brush (200 m radius, about 10,000 samples, the smoothing brush): best
  1.1 ms, mean 1.3 ms (limit 20 ms), including applying it to the tiles.

### Debug agents on the real terrain (`citysim_headless --bench "agents=10000,ticks=400,map=world"`)

Release, gcc, a 4-core cloud machine (indicative; the PC run, T1.9.1.2, is authoritative). The world is a generated
16 km synthetic map with one city: 2,048 x 2,048 cells of 4 m, a river through it.

- City activation tick (36 terrain tiles synthesized plus the walk grid built): 337 ms.
- 10,000 agents: the start-up ramp is the expensive part, as on the flat map. While the agents get their first
  path (200 searches per tick, trips across an 8 km area are up to ~2,000 cells long) a tick averages ~650 ms for the
  first ~50 ticks; over all 400 ticks the average is 91 ms, p95 663 ms, max 924 ms. Once every agent has a path the
  second half of the run averages 0.53 ms per tick (p95 0.83 ms, max 2.4 ms). If the ramp matters, lower
  `searches` (the bench option) or the trip length.
- The walk grid keeps up with floods and edits by recomputing 512 x 512-cell tiles; the regions of the grid
  (`update_regions`, a flood fill over 4.2 million cells) are made when the planner has agents to plan and the
  grid has changed.

## City-view terrain rendering (T1.6.1.1), PC reference machine (authoritative)

Budget (architecture §15.9 and the Task): active city + ring + horizon at 1080p, terrain at most 4 ms GPU, at least
60 FPS overall; terrain VRAM at most 1 GB.

How: `game/tests/terrain_bench.gd` (see `game/tests/README.md`), release build of the extension
(`windows-release-gdext`), developer map (`citysim_mapgen --synthetic --mod game/content_dev`, 96 km, seed 1, city
founded at the middle of the detail terrain), V-Sync off, the real main scene (sun with shadows, sky, HUD). The
camera looks at the city's center from the zoom distance given, 30 degrees of yaw, pitch from the camera's curve.
Each row averages 120 frames. "Terrain GPU" is the GPU time with the terrain shown minus with it hidden (shadow
pass included). Triangles: "terrain" counts the chosen chunks (skirts included) and the horizon; "frame" is
Godot's count of everything drawn, shadow passes included.

### 2026-10-18, branch `story/S1.6.1-terrain-render`, GTX 1660 Ti, Windows 10, window 1920 x 1061

| View | Zoom (m) | GPU all (ms) | GPU without terrain (ms) | Terrain GPU (ms) | FPS | Chunks drawn | Triangles terrain | Triangles frame | Draw calls | VRAM total (MB) | Chunk meshes | Chunk mesh memory (MB) |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| close | 120 | 2.26 | 0.23 | 2.03 | 350 | 174 | 1,914,840 | 2,568,400 | 264 | 206 | 244 | 33.0 |
| street | 400 | 2.43 | 0.23 | 2.20 | 330 | 162 | 1,810,392 | 2,507,474 | 257 | 208 | 252 | 34.1 |
| district | 1,500 | 2.39 | 0.23 | 2.16 | 335 | 120 | 1,444,824 | 1,471,698 | 138 | 208 | 252 | 34.1 |
| city | 4,500 | 2.11 | 0.23 | 1.87 | 373 | 36 | 713,688 | 662,228 | 45 | 208 | 252 | 34.1 |
| overview | 9,000 | 2.27 | 0.23 | 2.04 | 352 | 36 | 713,688 | 714,452 | 51 | 208 | 252 | 34.1 |

Reading it:

- **Terrain costs about 2 ms of GPU time in every view, half the 4 ms budget, and the game runs at 330-370 FPS.**
  The horizon (two meshes, about 64,500 triangles on the 16 km test map and more on this 96 km one) and the shadow
  pass are a fixed part of that; the chunk count follows the view (36 when far away, about 170 close up).
- **Memory is small:** 252 chunk meshes are about 34 MB (8,449 triangles and 4,485 vertices each); the whole
  game uses about 208 MB of video memory. The cache keeps at most 1,200 chunk meshes (about 160 MB).
- The window's client area was 1,061 pixels high (the title bar takes the rest of 1,080); a borderless 1080p window
  would be about 2% more pixels.
- The "frame" triangle count can be lower than the terrain's own count because Godot culls chunks outside the
  view; the terrain count is what the LOD selection chose.

