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

- **CMake ≥ 3.28 + Ninja**, configured through `CMakePresets.json`. Compilers: GCC or Clang on Linux, MSVC (VS 2022 Build Tools) on Windows.
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
| godot-cpp | GDExtension bindings (git submodule, not FetchContent). Built through its CMake support when `CITYSIM_BUILD_GDEXT=ON` (presets `linux-debug-gdext`, `windows-debug-gdext`), which is off in the other presets so the simulation builds and tests stay fast. Exceptions stay enabled in it because the core throws. |
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

All of these values are **tunable through content data** (`game/content/base/defines/time.json`). The movement and daily-cycle defaults below were chosen in spike S0.8.2 (`docs/spikes/time-scale.md`, approved 2026-10-18); the rest are starting values.

| Concept | Definition | Default |
|---|---|---|
| **Tick** | one `Simulation::step()` | — |
| **Base tick rate** | ticks per real second at 1× | 20 |
| **Speeds** | pause, 1×, 2×, 4× (tick-rate multipliers) | 0 / 20 / 40 / 80 tps |
| **Movement time** | simulated seconds of agent movement per tick = `movement_scale / base_tick_rate` | movement_scale = 6 |
| **Daily cycle** | time of day that drives citizen routines and lighting; one day = `day_length_ticks` | 28,800 ticks (24 real min at 1×; 6 at 4×) |
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
- A fake clock can be injected for tests. `IClock` has three operations: `now_seconds()`, `sleep_until(deadline)` and `interrupt()`. `interrupt()` makes the current or next sleep return early, once, so `stop()` and new commands never wait out a sleep. `SimulationRunner::wait_until_idle()` lets tests advance a `FakeClock` and then wait deterministically, with no real sleeping.
- **Save and load** are the commands `SaveGame{path}` and `LoadGame{path}`. The runner executes them itself, strictly between ticks and also while paused; they never reach the `Simulation` and are not in the input log. A save includes the commands still waiting. A load builds a new `Simulation` (the runner repeats the setup given to `configure()`), loads the file into it and replaces the old one only on success, so a bad file changes nothing. `RequestStateHash{}` is handled the same way (between ticks, not in the input log) and answers with a `StateHash` `UiEvent` carrying the hash and the tick. The result reaches the UI as a `SaveCompleted` / `LoadCompleted` `UiEvent` (with `ok`, a failure message, and for loads the warnings such as changed content) in a snapshot published immediately; these events stay in snapshots until the consumer has read them, then are handed over exactly once.
- **Autosave** runs on the runner's `IClock`, never on ticks: every `autosave_interval_real_seconds` (0 = off) it writes `autosave_1.csav` … `autosave_<autosave_slots>.csav` in rotation into the configured directory. It is skipped while paused and the interval restarts on resume and after a load. Both settings are in `defines/time.json`; they are not hashed or saved.
- Ticks are scheduled as `origin + (n + 1) × interval`. The origin resets on a speed change and after unpausing. The first tick is one interval after the origin.

### 7.2 JobSystem
- A fixed pool of worker threads (`hardware_concurrency − 2`, minimum 1). The simulation thread also helps with work. The configured count includes the calling thread, so a count of 1 means no extra threads.
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
- **Interpolation.** The front end keeps the previous and current snapshot (`SnapshotView`) and each frame computes `alpha = clamp((now − current.published_at) / tick_interval, 0, 1)`, where `now` is the runner clock and the interval follows the current speed; `alpha` is 1 while paused. `CitySim` computes it once per frame (`get_interpolation_alpha()`), and C++ renderer nodes read it with the two snapshots through `CitySim::snapshot_view()`. A load resets the history, so nothing is blended across it.
- **Channels.** The flat arrays for renderers are `SnapshotChannel`s (`name`, `data`, `stride`) in `RenderSnapshot::channels`, added by publish hooks and cleared before every publish. A channel's layout is a contract between its hook and its renderer, documented with the hook. GDScript can read one as a `PackedFloat32Array` (`get_channel(name)`) for debugging only. The built-in `debug.clock` channel (tick, speed, paused) exists to test the path.
- **Grids.** Big data that rarely changes (the test map's walkable cells) is a `SnapshotGrid` in `RenderSnapshot::grids`: `width`, `height`, `cell_size` and a *shared* `cells` pointer. The publish hook (`debug::TestMapPublisher`) hands out the same pointer until the map changes, so a renderer compares pointers and rebuilds only on a new grid (`TestMapView` does). `AgentRenderer` and `TestMapView` are GDExtension nodes that read the previous and current snapshot from `CitySim` directly.
- **Terrain tiles.** The active 4 m city terrain tiles (§15.4) are `RenderSnapshot::terrain_tiles` ("terrain.tiles"), filled by the publish hook `world::publish_terrain_tiles`: one `SnapshotTerrainTile` per active tile, sorted by `TileCoord`, holding the coordinate, the tile's version and a `std::shared_ptr<const world::CityTile>` that *is* the simulation's own immutable tile (no heights are copied). As with grids, a tile's pointer changes only when the tile does (a new `CityTile` with a higher version), so the terrain renderer rebuilds a tile's mesh only when its pointer differs from the one it last saw; a tile missing from the list is no longer active. Cleared before every publish like channels and grids.

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
- Tables are stored at `loc/<lang>/*.json` as flat `{ "key": "text" }`, with `{named}` placeholders (`{{` and `}}` are literal braces; a placeholder with no argument stays visible). Mods load in order, so a later mod's text replaces an earlier one.
- `Localizer` looks up the current language, falls back to `en`, then falls back to the key itself (visible in debug builds).
- The Godot UI uses `tr()`. At startup the bridge builds a Godot `Translation` from the loaded tables, so `.tscn` labels hold keys.
- English only at launch. The validator flags missing `en` keys as errors and missing keys in other languages as warnings; `en` keys no definition refers to are reported only with `--verbose` (code looks some up directly, such as calendar names).

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
  - Body: **sections** (4-char tag, u16 section version, u32 compressed length, u32 raw length, then zstd bytes). Unknown sections are skipped with a warning. The header itself is uncompressed so a load menu can read it, and the file ends with an XXH3 checksum of everything before it.
  - Core sections:
    - `META` (world seed and tick; also in the header)
    - `WRLD`: the whole World. Entities, every registered component type and every singleton (the game clock and calendar among them), each as a block sorted by stable name (see `save_world`). One section rather than separate `CLCK`/`ENTS`/per-type sections, because the clock and calendar are registered singletons and per-type versions live inside the blocks.
    - `CMDQ` (pending commands)
    - `ILOG` (optional input log)
- **Entity ids** are remapped on load. Components that reference entities serialize them through the archive's remap table (`ar.entity`, or `io()` on an `entt::entity`). A save holds dense indices (0..n-1 in ascending entity order), so a load gives every entity a fresh, dense id. Consequences for simulation code:
  - Entity ids and the storage order of views are not stable across a save/load. State must not depend on them beyond what is saved. Sort by something stable (an id stored in a component) when order matters.
  - `World::state_hash()` hashes raw entity values, so a world that has destroyed and recycled entities hashes differently after a save and load, even though the data is the same. Round-trip and continuation tests therefore use worlds without recycled ids until the hash is made rank-based (open question for the owner).
- **Content references** (`DefHandle`) are saved as ids and need the `ContentDatabase` on the archive (`BinaryWriter(nullptr, &content)`); loading an id the content lacks is a `SaveFormatError`.
- **Per-type blocks:** `save_world` writes each component type and singleton as `name, version, byte length, payload`, so a type this build does not register is skipped and reported. A registered, non-empty type with no serialize hooks makes saving fail (§4 rule 8).
- **Required tests:**
  - save → load → `state_hash` is equal
  - (run N ticks) vs (run k ticks, save, load, run N−k ticks) gives equal hashes
- **Versioning:** each section, and each component or singleton type inside `WRLD`, has a version. `MigrationRegistry` holds upgrades one version at a time: per-type migrations (`add_type(stable_name, from_version, fn)`, which convert one instance from the layout of `from_version` to the next) and per-section migrations (`add_section(tag, from_version, fn)`). A load runs the chain up to the current version and then calls the current `serialize()`, so `serialize()` only ever describes the current layout. A missing step is an error naming the type and the versions.

### Saves: when to bump a version
- Bump a type's version (the `version` argument of `save::bind_serializer`) on **any** change to its serialized layout: adding, removing, reordering or retyping a field, or changing how a value is encoded. A change that leaves the bytes identical (renaming a C++ field) needs nothing.
- In the same change: register a migration from the previous version (copy the old fields, write the new layout, fill new fields with a sensible default) and add a **fixture save** written with the old layout to `sim/tests/fixtures/saves/`. The migration is covered by a test that loads the fixture.
- Fixtures are never regenerated with a newer layout: they are the proof that old saves still load. The generator that writes one is a hidden test (`citysim_tests "[generate]"`) holding a copy of the old struct. Fixtures are small binary files marked `binary` in `.gitattributes`, not Git LFS.
- Releasing a version without its migration breaks every save made with the previous one; there is no “just this once”. A migration may be dropped only when the owner decides to stop supporting saves from that version, and the loader's error message for it must then say so.
- `save_simulation` / `load_simulation` (`citysim/save/simulation_io.hpp`) write and read a whole `Simulation`: loading goes into a fresh `Simulation` with the same modules installed, and returns warnings (content hash, mod list or game version differ; unknown sections or types skipped) rather than refusing. The wall-clock save time is supplied by the caller, since simulation code never reads the clock.
- **Derived state after a load.** A module that rebuilds state from saved data instead of saving it (the synthesized terrain tiles, §15.4) adds an *after-load hook* at install time (`Simulation::add_after_load_hook`). `load_simulation` runs the hooks in the order added, after the World and the loop state are restored; a hook may add load warnings (the map package changed, say), and a hook that throws fails the load like a damaged file.
- Saving and loading are commands that the runner executes **between ticks**.

## 11. Godot Bridge & Rendering

- **`CitySim`** is a GDExtension class (Node). The game registers it as the autoload `Sim`. Its API:
  - lifecycle: `new_game(seed, content_roots, enabled_mods, era_id)` → `{ok, errors, warnings}`, `shutdown()`, `is_running()`. Loading and saving are commands (below).
  - `submit_command(Dictionary)`, which converts to typed commands (`gdext/src/command_marshal.cpp`, one table line per command) and **validates in C++**: unknown types, missing, extra or wrongly typed fields, out-of-range values and unknown content ids are rejected with `push_error`. Commands: `set_speed`, `toggle_pause`, `set_era`, `save_game`, `load_game`, `state_hash`, `spawn_agents`, `clear_agents`, `set_time_scale` (changes `movement_scale` and `day_length_ticks` in the hashed `TimeDefines`; a normal recorded command).
  - `get_stats()` → `{tick, tps, tick_ms_avg, tick_ms_last, entity_count, speed, paused, date_text, time_text}` from the newest snapshot
  - signals emitted from `_process` on the main thread when a snapshot changes: `tick_advanced(tick)`, `date_changed(text)`, `speed_changed(speed, paused)`, `save_completed(path, ok, msg)`, `load_completed(path, ok, msg)`, `state_hash_ready(tick, hash)` (the answer to `state_hash`: `World::state_hash()` taken between ticks, as 16 hex digits)
  - The loaded localization tables are given to Godot as `Translation`s, so `tr("key")` works.
  - The game never uses `CitySim` directly: the autoload `Sim` (`game/scripts/autoload/sim.gd`) owns it, supplies the default content and forwards the signals.
  - Content is read from real folders, so it works in the editor and in a build that ships `content/` next to the executable. Reading it from a packed `.pck` is not supported yet.
  - The glue that is not Godot-specific (loading content, creating the runner, installing the time module and calendar) is `runner::GameSession` in `sim/`, which has native tests.
- **Fast renderers** are C++ GDExtension nodes (e.g., `AgentRenderer`). They read snapshot arrays and write `MultiMesh` instance buffers directly (`set_buffer`), with no per-agent GDScript.
- **GDScript** handles UI, camera, input mapping, scene composition, and tool modes. It never runs per-entity loops.
- **Units and coordinates:**
  - 1 unit = 1 meter
  - Godot Y-up
  - the simulation uses `(x, z)` on the ground plane plus a height `y`
  - angles in radians
  - simulation positions inside a city use that city's **local frame**, with its origin at the south-west corner of the city area (§15.3); region-level positions use world coordinates (§15.3)

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

## 15. World & Terrain

The world is **one continuous terrain per map**: a continent-scale region built from real data. Cities are 8 × 8 km areas cut out of it, and **neighboring cities share the same terrain** (GDD §13). Terrain is detailed only where it is needed.

### 15.1 Two resolutions
| Layer | Resolution | Extent | Source | Lives in |
|---|---|---|---|---|
| **Region rasters** | 1,000 m cells (set per map) | the whole map (e.g., Egypt → Mesopotamia) | offline pipeline (§15.5) | map package, always loaded |
| **Package detail DEM** | 32 m samples, in 2,048 m tiles | only **detail regions** (polygons in the map definition: the Nile Delta and valley, the Mesopotamian alluvium, …) | offline pipeline | map package, loaded per tile on demand |
| **City terrain** | **4 m** samples, in 2,048 m tiles (513 × 513 samples; edge samples are shared with the neighbor) | the city areas being simulated in detail, plus a 1-tile ring | **synthesized at run time** from the package detail DEM (§15.4) | memory only; never saved |

- A **city area** is exactly **4 × 4 detail tiles = 8,192 m square**, aligned to the tile grid. City areas never overlap but may touch. A city can only be founded where every one of its tiles lies inside a detail region.
- Outside the active city's tiles and ring, the renderer uses region rasters and the 32 m DEM.

### 15.2 Map packages
- A **`MapDef`** (content type `"map"`) holds:
  - `id`, `name_key`
  - geographic bounds (lon/lat)
  - region cell size (default 1,000 m) and a `map_seed` (drives the terrain's micro-detail, §15.4)
  - detail-region polygons
  - the river regime ids it uses (§15.6)
  - the package folder name
  - The projection is not in the `MapDef`: it is Lambert azimuthal equal-area centered on the middle of the bounds (§15.3), and the pipeline writes its parameters into `package.json`.
- A **map package** is generated data in `game/content/<mod>/maps/<map>/`. **It is not committed** (it is gitignored and rebuilt by the pipeline). It contains:
  - `package.json`: the map id, pipeline version, source dataset versions, projection, raster sizes, the tile list, an XXH3 checksum per file, and the overall **package hash**
  - `region/<layer>.csr`: elevation, land mask, fertility, vegetation, wetness, one layer per resource deposit
  - `detail/<tx>_<ty>.csr`: 65 × 65 samples, 32 m spacing, 2,048 m tiles
  - `rivers.json`: polylines with per-vertex width, depth and base water level, plus order and discharge
  - `lakes.json`
- **`.csr` raster format** (one format for every raster; little-endian; the C++ reader/writer in `citysim::world`, the Python pipeline and the tests all follow this layout):

  | Offset | Size | Field |
  |---|---|---|
  | 0 | 4 | magic `"CSRA"` |
  | 4 | 2 | format version (u16, currently 1) |
  | 6 | 4 | width in cells (u32) |
  | 10 | 4 | height in cells (u32) |
  | 14 | 1 | element type: 1 = u8, 2 = u16, 3 = i16, 4 = f32 |
  | 15 | 1 | flags: bit 0 = the raster has a no-data value (other bits 0) |
  | 16 | 4 | scale (f32) |
  | 20 | 4 | offset (f32) |
  | 24 | 8 | no-data value (f64, compared with the raw stored value; 0 when the flag is clear) |
  | 32 | 8 | payload size before compression (u64) = width × height × element size |
  | 40 | 8 | compressed payload size *n* (u64) |
  | 48 | *n* | payload: one zstd frame holding the cells, row-major, **row 0 at the south**, in the element type |
  | 48 + *n* | 8 | XXH3-64 (u64) of every byte before it |

  - A stored value `raw` means `raw × scale + offset` (elevation: i16 with scale 0.25 → 0.25 m steps). A no-data cell decodes to NaN.
  - The same raster always gives the same bytes from the C++ writer (fixed zstd level). Compressed bytes may differ between zstd versions or other writers, so nothing may assume byte-identical files across tools: identity is the checksums in `package.json` and the package hash below.
- **`package.json`** lists every file of the package with its checksum:
  - A file's checksum is the **XXH3-64 of the whole file's bytes**, written as 16 lower-case hex digits.
  - The **package hash** is the XXH3-64 of one text: for each file in `files`, in **byte-wise sorted path order**, the line `<relative path>=<checksum hex>\n` (paths use `/`; there is no trailing separator beyond the final newline). It covers every file except `package.json` itself, and it identifies the data whatever the folder is called. Saves store it (§15.4).
  - Verification (`verify_package`) checks that every listed file exists and matches, that every layer, tile, rivers and lakes file is listed, that no detail tile is unlisted, and that the stored package hash matches the file list.
  - The full field list, with an example, is in `sim/include/citysim/world/map_package.hpp` and `game/content/README.md`.
- **Sources** that *are* committed live in `tools/terrain/maps/<map>/`: the map definition input, authored GeoJSON (historical corrections, deposits, fertility overrides) and their source notes.
- Tests and cloud sessions use a **synthetic package** made by `citysim_mapgen --synthetic` (a procedural valley, river, delta and coast). Real packages need the PC pipeline.

### 15.3 Coordinate frames
- **Map projection:** Lambert azimuthal equal-area centered on the map's center, in meters. Distortion is acceptable at continental scale.
- **World frame:** meters from the south-west corner of the projected map bounds. Stored as `double` (`WorldPos {double x, z}`). The frames, the tile constants (`k_tile_size_m`, `k_city_sample_spacing_m`, `k_city_tile_samples`, `k_city_area_tiles`) and the conversions are in `citysim/world/frames.hpp`; tile (tx, ty) covers `[tx·2048, (tx+1)·2048]` along x, and the same along z, sharing its edges with its neighbors. Used for region-level things: city area origins, rivers, trade routes, armies.
- **City-local frame:** meters from the south-west corner of a city area, stored as `float`. Everything in the city simulation (agents, buildings, roads) uses it, because an 8 km frame gives float positions millimeter precision. Stored as `LocalPos {float x, z}`. Conversions go through `CityArea::origin()` (double), with `to_world` and `to_local`. Neighboring cities appear in the active city's frame through that offset.
- **Godot:**
  - The **city view** renders in the active city's local frame.
  - The **region view** renders world-frame meters relative to the map center (float precision there is ~0.25 m, plenty at that zoom).
  - The standard (single-precision) Godot build is enough. No double-precision engine build is needed.

### 15.4 Run-time terrain synthesis and edits
- `TerrainSynth` turns package detail tiles into 4 m city tiles. **It is deterministic and versioned** (`k_terrain_generator_version`):
  1. Bicubic (Catmull-Rom) upsampling of the 32 m DEM, reading the neighbor tiles' nodes at the borders so shared edges match bit for bit.
  2. Seeded micro-detail noise: value noise at lattice spacings of 128, 64, 32, 16 and 8 m (all divide the 2,048 m tile, so lattices meet at tile borders). Each lattice point is hashed from `rng::hash64(map_seed, stream("terrain.detail"), tx, ty, n)`, where (tx, ty) is the tile that **owns** the point (a point on a border belongs to the east/north neighbor) and `n` encodes the octave and the point's index in that tile; `map_seed` comes from the MapDef, **not** from the game seed. The same map therefore gives the same terrain in every game, and neighboring tiles agree at their edges. The amplitude is 0.3 m on flat, fertile ground (the region `fertility` layer and the slope of the base surface) rising to 3 m on steep or barren ground, and fades to zero at sea level and below.
  3. River channels carved along `rivers.json` (§15.6).
  4. Historical corrections, which were already applied by the pipeline.
- **Terrain is a function of the package hash and the generator version** (and the MapDef's `map_seed`, which the content hash covers). Saves store the map id, package hash and generator version, never base heights. A mismatch on load is a warning: the terrain may differ.
- **`WorldTerrain`** (`citysim/world/world_terrain.hpp`) is the terrain service: a World singleton registered as `"world.Terrain"` by `world::install(simulation, content, WorldConfig{map_id})`. Its hashed and saved state is exactly `SavedTerrain`: map id, package hash, generator version and the sorted set of **active tiles**. It holds the map as a shared, immutable **`LoadedMap`** (verified manifest, region layers decoded to float, 32 m detail tiles loaded lazily and thread-safely on first use); a game session opens the map once and every `Simulation` it builds shares it. Active tiles are `std::shared_ptr<const CityTile>` (513 × 513 floats plus a version) in a cache keyed by `TileCoord`. `activate_tiles(jobs, tiles)` synthesizes the missing ones with one chunk per tile and adds them in `TileCoord` order (§7.2); `deactivate_tiles` drops them.
- **On load**, the after-load hook (§10) re-opens the package by map id through the `ContentDatabase` if the save names another map (a map the content lacks fails the load), warns on a different package hash or generator version, skips saved tiles that are no longer on the map with a warning, and activates the saved tiles again.
- **`TerrainSynth::make_tile(map, tile, map_seed)`** is the generator, `k_terrain_generator_version` its version. Version 0 (T1.1.2.1) was a placeholder (bilinear 32 m DEM only). **Version 1** (T1.1.2.2) is Catmull-Rom upsampling plus the micro-detail above; river channels (step 3) follow in T1.3.1.1 and will bump the version again. Because every sample is a function of its world position alone, shared tile edges are always bit-equal, whatever the worker count. The generator never fuses multiply-adds (`-ffp-contract=off` on its source file), and a golden hash of two synthetic-map tiles guards it; changing any height needs a version bump and a note in the PR. One tile takes about 13 ms in a Release build (`citysim_tests "[.bench]"`).
- **Edits** (terraforming, building pads) are **sparse per-tile delta layers** (`float` offsets) on top of the synthesized base, held by `TerrainDeltas` (`citysim/world/terrain_deltas.hpp`) inside `WorldTerrain`, so edits, tiles and the active set change and save together. A layer (513 × 513 offsets and a dirty rectangle) exists only for a tile that was edited and still has a non-zero offset. `WorldTerrain::apply_delta(tile, rect, fn)` calls `fn(sx, sy, current_height)` for each sample of the rectangle of an active tile, stores `new − base` as the offset, and replaces the tile by a new `CityTile` (version + 1). A sample on a tile edge or corner is shared, so its offset is also written to the neighbor tiles, which keeps shared edges bit-equal; a neighbor that is not active keeps it in its layer. Layers of tiles that are not active are kept and added again when the tile is activated. Deltas are hashed (tile, then every offset) and saved: only non-empty layers, each as its tile and one zstd frame of the offsets, in the `world.Terrain` (WRLD) block. A load restores the layers before it activates the saved tiles.
- A tile's data is **immutable**. An edit produces a new tile version (copy-on-write) and publishes a new shared pointer, the same pattern as `SnapshotGrid` (§7.3). The renderer rebuilds a chunk only when its pointer changes.
- Height queries: `WorldTerrain::height_at(WorldPos)` and `height_at_local(city, LocalPos)` sample bilinearly from base + delta in the active tile that covers the point. Outside the active tiles they fall back to the 32 m detail DEM, then the region elevation (bilinear between cell centers, clamped at the raster edge). On a tile edge the first present tile in the order (tx, ty), (tx − 1, ty), (tx, ty − 1), (tx − 1, ty − 1) of the point's tile answers; edges agree, so the choice never changes the height. `slope_at` (rise over run) and `normal_at` come from central differences of `height_at` one city sample (4 m) to each side; water depth will be derived the same way.
- Synthesizing a city's 16 + ring tiles runs on the `JobSystem` when a city becomes active (a new game, founding, a load, or switching cities), with one chunk per tile.

### 15.5 Offline pipeline (`tools/terrain/`, Python)
- `python -m terrain.build_map tools/terrain/maps/<map>/map.json` → map package. Downloads are cached in `~/.cache/citysim/terrain/` and checked against checksums.
- Datasets. Licenses are verified and recorded in `docs/licenses.md` before use; attribution goes in the credits.
  - **Copernicus DEM GLO-30** for elevation
  - **HydroRIVERS / HydroLAKES** (HydroSHEDS) for rivers and lakes
  - **Natural Earth** for coastlines and the land/sea mask
  - **ESA WorldCover** for vegetation and wetland
  - If a license is unsuitable, fall back to Natural Earth or OpenStreetMap, as the license review in T1.2.1.1 decides.
- Steps:
  1. fetch
  2. mosaic
  3. reproject (pyproj / rasterio)
  4. apply **historical corrections** (§15.7)
  5. build the region rasters
  6. build the detail tiles for the detail regions
  7. extract and simplify rivers and lakes
  8. derive fertility and vegetation, then rasterize the authored deposit and fertility overrides
  9. write the package with checksums
- It has its own tests (pytest, with tiny fixture rasters) that run in CI.

### 15.6 Water
- **Fixed rivers with variable water levels** (GDD §13): river courses never move during play.
- Each river **reach** has a water-level offset in meters. It is singleton state that is hashed and saved.
- **Sea level** is 0 m, with a per-map offset (reserved for later).
- **Flood extent:** cells whose terrain lies below the reach's current water level, flood-filled outward from the channel and limited to the reach's floodplain polygon (derived by the pipeline). It is recomputed incrementally and time-sliced when a level changes, and it emits `WaterChanged` events.
- **River regimes** (`RiverRegimeDef`) define an annual level curve over the calendar year plus yearly variance (low, normal and high years drawn with `citysim::rng`). They run on **calendar time** (§6). The **Nile inundation** is the first regime and the test case for the event mechanic: high floods emit damage events, low floods emit shortage events, and receding water renews fertility (§15.8).

### 15.7 Historical corrections ("light")
- Authored **GeoJSON overlays**, applied in order by the pipeline, with these operations:
  - `set_water`, `set_land`
  - `elevation_offset`, `flatten_to`
  - `remove_river`, `river_path`, which adds or reroutes a polyline using per-vertex width and depth
- Every feature carries `source` and `confidence` properties. The designer reviews every correction.
- Corrections are specific to a map and an era. A map package is built for one starting era. A full playthrough keeps the terrain of its starting era, and later-era change (canals, dams) happens through gameplay.

### 15.8 Resources & fertility
- Each city has **resource fields** at 4 m: fertility (0–1), vegetation density, wetness, and one deposit amount per `ResourceDef`. They are sampled from the region and detail layers plus seeded detail noise. Like terrain, the base is regenerable and only **deltas** (depletion, silt renewal, disaster damage) are hashed and saved.
- `ResourceDef` (content type `"resource"`): id, `name_key`, kind (soil / stone / ore / fuel / vegetation), overlay color ramp, `renewable` flag and renewal rule.
- The extraction API reduces a deposit, and depletion is permanent unless the resource is renewable. Trade agreements (E4) exist so a realm doesn't have to exhaust its own deposits.

### 15.9 Rendering
- **City terrain:** `TerrainRenderer`, a GDExtension node. It uses a CDLOD-style quadtree of chunks per tile, with skirts at LOD seams. It builds meshes from the shared immutable tile data and draws the ring and horizon from the 32 m and region data.
- **Shading:** splat blending by slope, height, fertility, vegetation and wetness. Triplanar mapping on steep slopes.
- **Water:** river surfaces are built from polylines and current levels. There is a sea plane, and flood extents are shown as water surfaces.
- **Vegetation:** instanced (MultiMesh) scatter, deterministic and driven by the vegetation layer. It is culled and LOD'd by distance.
- **Overlays:** a shared data-overlay system (CS2-style) for fertility, deposits, elevation contours, slope and water depth. Each overlay reads a resource field through a texture and shows a legend.
- **Region view:** the same chunked renderer fed with region rasters. **A map-style shader blends from shaded 3D terrain into a "paper map" look as the camera rises**, with hillshade, inked coasts and rivers, and labels from `LabelDef` content (historical names per era).
- City view ↔ region view: zooming out past a threshold switches to the region view, and clicking a city area returns to the city view. The switch is a short cross-fade. A seamless zoom is a later improvement.
- **Budgets (reference machine):** terrain + water + vegetation ≤ 6 ms GPU at 1080p in the city view. Terrain VRAM ≤ 1 GB.
