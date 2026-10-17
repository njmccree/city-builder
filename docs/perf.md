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

Not measured yet. Added by T0.8.1.4, together with the rendering FPS with 10,000 visible agents.
