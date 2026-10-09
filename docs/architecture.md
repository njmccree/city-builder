# Architecture

> The technical source of truth. Backlog Task prompts refer to sections of this file by number (e.g., "architecture §5.3").
> Change it only on purpose: if a Task needs to deviate, update this document in the same PR and explain why.

## 1. Overview

```
┌──────────────────────────────────────────────────────────────────┐
│ Godot 4 project  (game/)                                         │
│   GDScript: UI, menus, camera, input, scenes, audio              │
│        │  calls / signals                                        │
│        ▼                                                         │
│ GDExtension bridge  (gdext/, C++)                                │
│   CitySim node · snapshot readers · fast renderers (MultiMesh)   │
│        │  commands in ▼         ▲ snapshots out                  │
├────────┼────────────────────────┼────────────────────────────────┤
│ Simulation core  (sim/, C++20, NO Godot dependency)              │
│   SimulationRunner (own thread) → Simulation → World (EnTT ECS)  │
│   Scheduler · Systems · JobSystem · RNG · Clock/Calendar         │
│   Content registry & mods · Localization tables · Save/Load      │
└──────────────────────────────────────────────────────────────────┘
        ▲ loads
   game/content/   (JSON data: base game = the "base" mod; mods/ alongside)
```

**Principles**
1. **The simulation is the game.** `sim/` holds all game rules and state. Godot only draws state and collects input.
2. **`sim/` never includes Godot headers.** It builds and tests headless on Linux (cloud sessions, CI) and Windows.
3. **Deterministic within the same build and platform** (§4).
4. **Everything is data** (§8). The base game is loaded exactly like a mod.
5. **Micro is detailed, macro is abstracted.** Viewed cities simulate individual agents; other cities run as aggregates (§12).

## 2. Repository Layout

```
CMakeLists.txt            top-level; adds sim/, gdext/ (optional), tools
CMakePresets.json         linux-debug, linux-release, windows-debug, windows-release
cmake/                    Dependencies.cmake (pinned FetchContent), warnings, options
sim/
  include/citysim/        public headers, grouped by module (core/, time/, content/, save/, ...)
  src/                    implementation, same module folders
  tests/                  Catch2 tests, same module folders; tests/fixtures/ for data
  tools/headless/         citysim_headless CLI (run, hash, replay, bench)
  tools/validate/         citysim_validate CLI (content validation)
gdext/                    GDExtension library (C++), links citysim_core + godot-cpp
third_party/godot-cpp/    git submodule, pinned to match the Godot version
game/                     Godot project (project.godot at this level)
  bin/                    built GDExtension binaries (gitignored)
  content/base/           base-game data (JSON) — the "base" mod
  content/mods/           bundled/dev mods
  scenes/ scripts/ ui/ assets/ tests/
tools/                    Python/PowerShell dev tools (env checks, later: terrain pipeline)
docs/                     gdd.md, architecture.md, backlog/, versions.md, perf.md, spikes/
```

## 3. Build & Dependencies

- **CMake ≥ 3.25 + Ninja**, configured through `CMakePresets.json`. Compilers: GCC or Clang on Linux, MSVC (VS 2022 Build Tools) on Windows.
- **C++20.** Warnings at a high level; `-Werror` / `/WX` in CI. **Never use `-ffast-math`** or equivalents.
- Third-party libraries come through **FetchContent with pinned release tags** in `cmake/Dependencies.cmake`. Each pin is recorded in `docs/versions.md`.

| Library | Use |
|---|---|
| EnTT | ECS |
| Catch2 v3 | unit tests |
| nlohmann/json | content files |
| spdlog | logging |
| zstd | save compression |
| xxHash | state hashing, content hashing |
| godot-cpp | GDExtension bindings (git submodule, not FetchContent) |
| Tracy (optional, `CITYSIM_TRACY=ON`) | profiling |

- Targets:
  - `citysim_core` (static library)
  - `citysim_tests`
  - `citysim_headless`
  - `citysim_validate`
  - `citysim_gdext` (shared library, written to `game/bin/`)

## 4. Determinism Rules (mandatory in `sim/`)

1. **Randomness only through `citysim::rng`** (§5.4). No `rand()`, `std::random_device`, `<random>` engines, or `<random>` distributions (their output differs between standard libraries).
2. **No wall-clock time** in simulation logic. Simulation time comes only from the tick counter and the clock (§6). Wall-clock time is allowed only in `SimulationRunner` pacing and profiling.
3. **No iteration over unordered containers when the order affects state.** Use vectors, sorted containers, or EnTT views. EnTT storage order is deterministic given deterministic operations.
4. **No pointer-address-dependent behavior** (sorting by pointer, hashing pointers).
5. **Parallel work must be order-independent** (§7.2). Chunks are a fixed size that doesn't depend on thread count, and outputs are merged in chunk order.
6. **All external input enters as Commands** (§5.3), applied at tick boundaries and recorded in the input log.
7. Floats are allowed. The goal is reproducibility within one binary and platform, not across platforms.
8. **Every component that holds state is registered** (§5.1), so it is hashed and saved. Unregistered state is a bug.

Enforcement:
- Determinism tests (§13) run in CI.
- Code review checks these rules.
- A grep-based lint step rejects `rand(`, `std::random_device`, `std::chrono` and `<random>` inside `sim/src` (excluding `runner/` and `profiling/`).

## 5. Simulation Core

### 5.1 World & Components
- `citysim::World` wraps `entt::registry` and owns **singleton state** (clock, calendar, map, and so on) as context variables.
- **`ComponentRegistry`:** every stateful component type is registered with
  - a **stable string name** (e.g., `"core.Position"`),
  - a hash function,
  - a serialize function (§10).
- Registration order is irrelevant. Hashing and saving iterate components **sorted by stable name**.
- Components are plain structs (POD where possible). Logic lives in systems, not components.

### 5.2 Systems, Phases & Scheduler
- A system is a class implementing `ISystem { std::string_view name(); void update(TickContext&); }`.
- Phases run in this fixed order every tick:
  1. `ApplyCommands`
  2. `PreUpdate`
  3. `Update`
  4. `PostUpdate`
  5. `Calendar`
  6. `Publish`
- Inside a phase, systems run in **explicit registration order**. That order is part of the game's behavior, so it is never sorted or reordered implicitly.
- `TickContext` provides:
  - `World&`
  - `tick` (uint64)
  - `Clock`/`Calendar` read access
  - `JobSystem&`
  - `rng` factory
  - an event bus (events produced this tick are delivered in the next phase)
- `Simulation::step()` runs exactly one tick. The `Simulation` class has no knowledge of threads or real time.

### 5.3 Commands
- A command is a `std::variant` of plain structs (e.g., `SetSpeed`, `TogglePause`, `SpawnDebugAgents`, `SaveGame`).
- **Every external change to game state goes through a command:** UI, console, AI scripts on the main thread, and load and save requests.
- **`CommandQueue`** is a thread-safe MPSC queue. Each command gets a monotonically increasing sequence number when submitted. At the start of tick *N*, the runner drains the queue and the `ApplyCommands` phase applies the commands in sequence order.
- The **input log** records `(tick, sequence, command)` and is serializable. `citysim_headless --replay` re-runs a game from seed + input log.

### 5.4 Random Numbers
- **Counter-based (primary):** `rng::hash64(world_seed, stream_id, tick, key, n)`, a SplitMix64/xxHash-style mixer. Results depend only on their inputs, so they are safe inside `parallel_for` (use the entity id as `key`).
- **Sequential (secondary):** `rng::Pcg32`, for single-threaded code that needs a sequence. It is seeded from `hash64`.
- **Own distributions:**
  - `uniform_int(lo, hi)`
  - `uniform_float01()`
  - `chance(p)`
  - `weighted_index(span<weights>)`
  - `normal(mean, sd)` using Box–Muller written in-house
- **Stream ids** are `constexpr` hashes of a system name string (e.g., `stream("citizens.marriage")`).

### 5.5 Events
- Systems emit typed events (`std::variant`) into a per-tick buffer. Consumers read them in a later phase or on the next tick.
- Events never cross threads directly. Snapshots carry the events the UI needs (§7.3).

## 6. Time Model

All of these values are **tunable through content data** (`game/content/base/defines/time.json`). The numbers below are starting defaults to be tuned in spike S0.8.2.

| Concept | Definition | Default |
|---|---|---|
| **Tick** | one `Simulation::step()` | — |
| **Base tick rate** | ticks per real second at 1× | 20 |
| **Speeds** | pause, 1×, 2×, 4× (tick-rate multipliers) | 0 / 20 / 40 / 80 tps |
| **Movement time** | simulated seconds of agent movement per tick = `movement_scale / base_tick_rate` | movement_scale = 3 |
| **Daily cycle** | time of day that drives citizen routines and lighting; one day = `day_length_ticks` | 14,400 ticks (12 real min at 1×) |
| **Calendar** | date (year, month, day), advancing at the era cadence | see below |

- **Era cadence:** each `EraDef` has `real_seconds_per_year` (at 1×):
  - Ancient: 120
  - Colonial: 300
  - Modern: 720
  - Utopian: 900

  From that, calendar seconds per tick = `365 × 86400 / (real_seconds_per_year × base_tick_rate)`. This is computed with **integer arithmetic and a carried remainder**, so it never drifts.
- **The calendar and the daily cycle are independent.** The calendar runs faster than visible days in every era. Long-term processes (aging, births, harvests, construction progress, economy) run on **calendar time**. Moment-to-moment behavior (walking, working hours, lighting) runs on the **daily cycle and movement time**. Systems state in their header comment which clock they use.
- **Calendar representation:**
  - `int64` calendar seconds since an epoch.
  - **Astronomical year numbering**: year 0 = 1 BCE, −2999 = 3000 BCE.
  - **365-day years, no leap years** (simplification), with 12 fixed months for now.
  - Displayed as BCE/CE through localization keys.
- **Known design risk:** travel time vs. day length. Agents move visibly at plausible speeds, but long trips can't fit realistic schedules inside a compressed day. The initial mitigation:
  - Trips beyond a time budget are **fast-forwarded** when the agent is not being watched.
  - Schedules use coarse blocks (sleep / work / errands / leisure).
  - Spike S0.8.2 validates this.

## 7. Threading

### 7.1 SimulationRunner
- Owns the `Simulation` and runs it on a **dedicated simulation thread**.
- Paces ticks to the target tick rate for the current speed. When it falls behind, it runs up to `max_catchup_ticks` and then slows the game down rather than spiralling.
- Pause: the thread sleeps on a condition variable. Commands are still accepted and applied when the game unpauses, or immediately for "while paused" commands such as save and load.
- The main thread (Godot) talks to the runner **only** through `CommandQueue` (in) and `SnapshotBuffer` (out).
- A fake clock can be injected for tests.

### 7.2 JobSystem
- A fixed pool of worker threads (`hardware_concurrency − 2`, minimum 1). The simulation thread also helps with work.
- `parallel_for(count, chunk_size, fn(begin, end, chunk_index))`. **The chunk size is chosen by the caller and never depends on thread count.**
- **Determinism pattern:** each chunk writes to its own output buffer indexed by `chunk_index`. The caller then merges the buffers serially in chunk order. Results must be bit-identical with 1, 2 or N worker threads, and tests enforce this.
- No work stealing that changes results. Ordering is enforced at the merge, so the scheduling strategy is free.

### 7.3 Snapshots
- After each tick, the `Publish` phase writes a `RenderSnapshot` into a **triple buffer** without blocking the simulation thread.
- A `RenderSnapshot` holds:
  - the tick
  - clock and calendar info
  - stats (tick ms, entity count)
  - flat arrays for renderers (e.g., agent positions and headings for the viewed area)
  - a UI event list
- The main thread takes the newest snapshot every frame. It interpolates positions between the previous and current snapshot, using real time since that tick was published.
- Snapshots are **read-only copies**. Godot code never reads `World` directly.

## 8. Content & Mods

- **Format:** JSON (UTF-8). Each file holds an object `{ "defs": [ ... ] }`, where each definition has `"type"` and `"id"`.
- **IDs are namespaced:** `"<mod_id>:<name>"`, e.g. `"base:era.ancient"`. References between definitions use full ids.
- **Layout:** `game/content/<mod_root>/<mod_id>/` containing
  - `mod.json`: id, name, version, `depends_on`, `load_after`
  - `defs/**.json`
  - `loc/<lang>/*.json`
  - `defines/*.json` (tunable constants)
- **Load order:**
  - Mod discovery scans the configured roots.
  - Mods are topologically sorted by dependencies, with ties broken by mod id.
  - Files within a mod load in sorted path order.
- **Override semantics:**
  - Same id in a later mod → **replace**.
  - `"patch": true` → **JSON Merge Patch (RFC 7386)** on top of the earlier definition.
- **`DefRegistry<T>`:** after loading and resolving, each type's definitions are stored in a vector **sorted by id**. A `DefHandle<T>` is a 32-bit index into it. Handles are stable for a given set of mods, and saves store ids, not handles.
- **Validation:**
  - schema checks (required fields and types)
  - reference resolution
  - localization-key existence
  - errors report the file and JSON path

  The `citysim_validate` CLI runs this in CI.
- **Content hash:** xxHash over the ordered mod list plus file contents. Stored in saves to warn about mismatches.

## 9. Localization

- Every player-facing string is a key: definitions use `*_key` fields (e.g., `"name_key": "era.ancient.name"`).
- Tables are stored at `loc/<lang>/*.json` as flat `{ "key": "text" }`, with `{named}` placeholders.
- `Localizer` looks up the current language, falls back to `en`, then falls back to the key itself (visible in debug builds).
- The Godot UI uses `tr()`. At startup the bridge builds a Godot `Translation` from the loaded tables, so `.tscn` labels hold keys.
- English only at launch. The validator flags missing `en` keys as errors and missing keys in other languages as warnings.

## 10. Save / Load

- **Archive:** `BinaryWriter` / `BinaryReader`, little-endian, with varints, length-prefixed UTF-8 strings, and spans. Types implement `void serialize(Archive& ar, T& value, uint32_t version)`, using one function for both reading and writing.
- **File container (`.csav`):**
  - Header:
    - magic `CSAV`
    - container version
    - game version string
    - world seed
    - tick
    - content hash
    - mod list (id + version)
    - real-world save timestamp (metadata only)
  - Body: **sections** (4-char tag + section version + length), zstd-compressed. Unknown sections are skipped with a warning.
  - Core sections:
    - `META`
    - `CLCK` (clock and calendar)
    - `ENTS` (entity list)
    - one section per registered component type (sorted by stable name)
    - `CMDQ` (pending commands)
    - `ILOG` (optional input log)
- **Entity ids** are remapped on load. Components that reference entities serialize them through the archive's remap table.
- **Required tests:**
  - save → load → `state_hash` is equal
  - (run N ticks) vs (run k ticks, save, load, run N−k ticks) gives equal hashes
- **Versioning:** each section has a version. `MigrationRegistry` holds `(section, from_version) → fn` upgrades, and fixture saves for old versions live in `sim/tests/fixtures/saves/`.
- Saving and loading are commands that the runner executes **between ticks**.

## 11. Godot Bridge & Rendering

- **`CitySim`** is a GDExtension class (Node). The game registers it as the autoload `Sim`. Its API:
  - lifecycle: `new_game`, `load_game`, `save_game`, `shutdown`
  - `submit_command(Dictionary)`, which converts to typed commands and **validates in C++**
  - `get_snapshot_info()` for UI data
  - stats
  - signals emitted on the main thread from snapshot events
- **Fast renderers** are C++ GDExtension nodes (e.g., `AgentRenderer`). They read snapshot arrays and write `MultiMesh` instance buffers directly (`set_buffer`), with no per-agent GDScript.
- **GDScript** handles UI, camera, input mapping, scene composition, and tool modes. It never runs per-entity loops.
- **Units and coordinates:**
  - 1 unit = 1 meter
  - Godot Y-up
  - the simulation uses `(x, z)` on the ground plane plus a height `y`
  - angles in radians
  - world origin at the south-west corner of the city map

## 12. Simulation Level of Detail (forward-looking)

- Each city has a **sim mode**:
  - `Agent`: the viewed city plus a small number of recently viewed cities.
  - `Aggregate`: every other city.
- `Agent → Aggregate` collapses individuals into **cohorts**: households summarized by district, age, class, culture, religion and job sector. **Notable people stay individual in every mode.**
- `Aggregate → Agent` re-creates individuals from cohorts using deterministic RNG streams seeded by city and tick.
- The aggregate model runs on calendar time at a coarse cadence (e.g., monthly).
- Implemented in the Citizens epic. The Foundation epic only guarantees that systems can be filtered by city sim mode.

## 13. Testing Strategy

| Layer | Tool | Where |
|---|---|---|
| Unit tests | Catch2, `sim/tests/**` | Cloud, CI (Linux + Windows) |
| Determinism tests | same seed + command script → identical hash every tick; 1 vs N threads; save/load round-trip | Cloud, CI |
| Golden hashes | fixed scenarios with expected final hashes in `sim/tests/fixtures/golden/`; updated deliberately with a note in the PR | Cloud, CI |
| Content validation | `citysim_validate game/content` | Cloud, CI |
| Benchmarks | `citysim_headless --bench ...`, results in `docs/perf.md` | Cloud (indicative), PC (authoritative) |
| Godot smoke test | `godot --headless --path game -s res://tests/smoke.gd` loads the extension, steps the simulation, exits 0 | Cloud, CI (Linux) |
| Visual / manual | run the game, check behavior | PC |

## 14. Performance Budgets (reference machine: GTX 1660 Ti, 6 GB VRAM)

- **Rendering:** 60 FPS at 1080p in the walking skeleton with 10,000 visible agents.
- **Simulation at 4× (80 tps):** ≤ 12.5 ms per tick average on the simulation thread plus workers, including 50,000 simulated agents in the walking skeleton.
- **VRAM:** ≤ 4.5 GB in normal play, leaving headroom.
- These budgets are reviewed in `docs/perf.md` at the end of each milestone.
