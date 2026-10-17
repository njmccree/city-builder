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
