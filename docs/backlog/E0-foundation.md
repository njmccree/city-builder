# E0 — Foundation & Walking Skeleton

**Goal:** a Godot 4 game that runs the deterministic C++20 simulation core through GDExtension. It includes:
- pause and speed controls
- an era-paced calendar
- data-driven content with mod layering
- localization
- save and load
- a city-builder camera
- debug tools
- **10,000 debug agents walking on a test map at 60 FPS**, proving the architecture end to end

**Milestone:** M0. **Out of scope:** real terrain, roads, buildings, real citizens, any gameplay.

**Size:** 8 Features · 21 Stories · 40 Tasks. **Model budget:** Opus ×4 (marked ★), Haiku ×1, Sonnet ×35.

## Suggested execution order

```
F0.1  T0.1.1.1 → T0.1.1.2 → T0.1.2.1 → T0.1.4.1        (T0.1.3.1 any time on PC)
F0.2  T0.2.1.1 → T0.2.2.1 → T0.2.2.2 → T0.2.3.1★ → T0.2.3.2 → T0.2.3.3
      → T0.2.4.1★ → T0.2.4.2★ → T0.2.5.1
F0.3  T0.3.1.1 → T0.3.2.1 → T0.3.2.2 → T0.3.2.3          (needs F0.4 T0.4.1.1 for EraDef; see deps)
F0.4  T0.4.1.1 → T0.4.1.2 → T0.4.1.3 → T0.4.2.1 → T0.4.3.1
F0.5  T0.5.1.1★ → T0.5.1.2 → T0.5.1.3 → T0.5.1.4
F0.6  T0.6.1.1 → T0.6.1.2 → T0.6.1.3(PC) → T0.6.2.1 → T0.6.2.2
F0.7  T0.7.1.1(PC) · T0.7.2.1(PC) → T0.7.2.2(PC)
F0.8  T0.8.1.1 → T0.8.1.2 → T0.8.1.3(PC) → T0.8.1.4(PC) → T0.8.2.1(PC)
```
Recommended interleaving: F0.1 → F0.2 → F0.4 (T0.4.1.1) → F0.3 → rest of F0.4 → F0.5 → F0.6 → F0.7 → F0.8.

## Epic acceptance criteria
- [ ] `cmake --preset linux-debug && cmake --build --preset linux-debug && ctest --preset linux-debug` passes in a fresh cloud session. The same works with the `windows-debug` preset on the PC.
- [ ] CI runs on every PR: Linux and Windows builds and tests, content validation, Godot headless smoke test.
- [ ] Opening `game/` in Godot and pressing Play shows a flat test map, a working camera, speed controls, the date in the Ancient calendar, and a debug overlay.
- [ ] Console `spawn_agents 10000` → agents wander at ≥ 60 FPS on the GTX 1660 Ti at 1×.
- [ ] Save mid-game → load → the simulation continues with identical state hashes compared with an uninterrupted run.
- [ ] A test mod can override an era definition and a localization string without code changes.

---

## F0.1 — Repository, Tooling & Workflow

### S0.1.1 — Buildable repository skeleton
> *As a developer, I can clone the repo and build an empty core library with passing tests using one preset command on Linux or Windows, so every later Task starts from a working build.*

**Acceptance criteria**
- The directory layout matches architecture §2.
- The `linux-debug` preset configures, builds and runs ≥ 1 Catch2 test.
- Dependencies are pinned in `cmake/Dependencies.cmake` and recorded in `docs/versions.md`.

#### [x] T0.1.1.1 — Repo skeleton, ignore rules, formatting config
- **Model:** Sonnet · **Session:** [Cloud] · **Depends on:** —
```
Task T0.1.1.1 (Story S0.1.1). Read CLAUDE.md and docs/architecture.md §2–§3.

Create the repository skeleton. Do not add any C++ code or CMake yet (that is T0.1.1.2).

1. Create these directories, each with a short README.md stating its purpose (one or two lines):
   sim/include/citysim, sim/src, sim/tests, sim/tools, gdext, game, game/content/base,
   game/content/mods, tools, docs/spikes.
2. .gitignore covering:
   - build/, out/, CMake user presets (CMakeUserPresets.json)
   - game/.godot/, game/bin/, *.import caches Godot regenerates
   - IDE folders (.vs/, .vscode/ except .vscode/extensions.json, .idea/)
   - OS junk
   - *.csav test outputs outside sim/tests/fixtures/
3. .gitattributes:
   - text=auto with LF for source and text files
   - Git LFS for binary asset types: png, jpg, jpeg, tga, exr, hdr, psd, blend, fbx, glb, gltf binaries (.bin),
     wav, ogg, mp3, flac, ttf, otf, ktx2, dds
   Do not run `git lfs install` in the commit.
4. .clang-format: based on LLVM, IndentWidth 4, ColumnLimit 110, PointerAlignment Left,
   AllowShortFunctionsOnASingleLine: Inline, SortIncludes: CaseSensitive, IncludeBlocks: Regroup.
5. .editorconfig: utf-8, LF, 4 spaces for C++/CMake/GDScript (tabs for .gd files per Godot style guide
   — use `indent_style = tab` for *.gd), 2 spaces for JSON/YAML/Markdown, trim trailing whitespace except *.md.
6. Root README.md: one-paragraph project description, links to docs/gdd.md, docs/architecture.md,
   docs/backlog/README.md, CLAUDE.md, and a "Building" section that points to CLAUDE.md commands.
7. docs/versions.md: a table with columns Component | Version/Tag | Why pinned | Updated.
   Add rows for Godot (TBD), godot-cpp (TBD), CMake min (3.25), C++ standard (C++20).

Acceptance: the files exist; `git status` is clean after commit; no build outputs are committed.
Follow the Task workflow in CLAUDE.md (branch story/S0.1.1-skeleton, commit "T0.1.1.1: ...").
```

#### [x] T0.1.1.2 — CMake project, presets, pinned dependencies, first test
- **Model:** Sonnet · **Session:** [Cloud] · **Depends on:** T0.1.1.1
```
Task T0.1.1.2 (Story S0.1.1, last task of the story). Read CLAUDE.md and docs/architecture.md §2–§3.

Set up the CMake build.

1. Top-level CMakeLists.txt (cmake_minimum_required 3.25, project citysim, C++20, no compiler extensions).
   Options:
   - CITYSIM_BUILD_TESTS (ON)
   - CITYSIM_BUILD_TOOLS (ON)
   - CITYSIM_BUILD_GDEXT (OFF for now; gdext/ comes later)
   - CITYSIM_WARNINGS_AS_ERRORS (OFF; CI turns it ON)
   - CITYSIM_TRACY (OFF)
2. cmake/Warnings.cmake: a function citysim_set_warnings(target).
   - GCC/Clang: -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Wnon-virtual-dtor
     -Wold-style-cast -Woverloaded-virtual
   - MSVC: /W4 /permissive-
   - Add -Werror or /WX when CITYSIM_WARNINGS_AS_ERRORS is ON.
   - Never add fast-math flags.
3. cmake/Dependencies.cmake using FetchContent, pinned to the LATEST STABLE RELEASE TAG of each
   (look up the current tag; do not use branches):
   EnTT, Catch2 (v3), nlohmann_json, spdlog, zstd (build only the static lib, no programs/tests), xxHash.
   - Use FIND_PACKAGE_ARGS / OVERRIDE_FIND_PACKAGE only if simple.
   - Use GIT_SHALLOW TRUE.
   - Third-party targets must not get our warning flags.
   - Record every tag in docs/versions.md.
4. sim/CMakeLists.txt:
   - static library target citysim_core with sim/src/core/version.cpp and
     sim/include/citysim/core/version.hpp exposing `std::string_view citysim::core::version()`
     returning "0.0.1".
   - Link EnTT, nlohmann_json, spdlog, zstd, xxhash as PUBLIC or PRIVATE as appropriate.
5. sim/tests/CMakeLists.txt: citysim_tests executable (Catch2WithMain) with
   sim/tests/core/version_tests.cpp checking version() is non-empty; register with catch_discover_tests.
6. CMakePresets.json:
   - configure presets linux-debug, linux-release (Ninja, binaryDir build/${presetName})
   - windows-debug, windows-release (Ninja, cl.exe, same binaryDir pattern)
   - a hidden base preset for shared cache vars
   - matching build and test presets (test presets: output on failure)
   - a ci-linux preset that enables CITYSIM_WARNINGS_AS_ERRORS
7. Update the README.md files in sim/ if needed.

Verify in this session:
  cmake --preset linux-debug && cmake --build --preset linux-debug && ctest --preset linux-debug
must pass with zero warnings from our own code.
Follow the Task workflow in CLAUDE.md; this is the last task of S0.1.1, so open the PR.
```

### S0.1.2 — Cloud sessions build and test automatically
> *As the developer, any new Claude cloud session can build and test the project without manual setup, so `[Cloud]` Tasks can verify their own work.*

**Acceptance criteria**
- A SessionStart hook installs any missing tools and pre-builds the `linux-debug` preset.
- The hook is idempotent and finishes quickly on a warm container.

#### [x] T0.1.2.1 — SessionStart hook for cloud sessions
- **Model:** Sonnet · **Session:** [Cloud] · **Depends on:** T0.1.1.2
```
Task T0.1.2.1 (Story S0.1.2). Use the session-start-hook skill.

Create a SessionStart hook for Claude Code cloud sessions in this repo that:
1. Checks for cmake (>= 3.25), ninja, a C++20 compiler (g++ >= 12 or clang++ >= 15), git, python3.
   Install missing ones with the system package manager when possible. Print clear messages.
2. Runs `git submodule update --init --recursive` (harmless if there are none yet).
3. Configures and builds the linux-debug preset (do not run tests in the hook; keep it fast).
4. Downloads the Godot editor Linux x86_64 binary into a cache dir (~/.cache/citysim/godot/<version>/)
   ONLY if docs/versions.md pins a Godot version (it says TBD right now: skip with a message in that
   case). Expose its path via a `tools/godot.sh` wrapper script that finds the cached binary.
5. Is idempotent and safe to re-run; it fails soft (prints warnings) on network errors instead of
   blocking the session.

Document the hook in CLAUDE.md under "Build & test" (one short paragraph).
Verify by running the hook script manually in this session and showing it succeeds twice in a row.
Follow the Task workflow in CLAUDE.md (branch story/S0.1.2-session-hook); last task of the story, so open the PR.
```

### S0.1.3 — Windows development environment
> *As the developer on Windows, I have a checklist and a script that verify my toolchain, so `[PC]` Tasks work the first time.*

**Acceptance criteria**
- `docs/dev-setup-windows.md` exists.
- `tools/check-env.ps1` reports pass or fail for every required tool.
- The `windows-debug` preset builds and tests pass on the PC.

#### [ ] T0.1.3.1 — Windows setup guide and environment check
- **Model:** Sonnet · **Session:** [PC] · **Depends on:** T0.1.1.2
```
Task T0.1.3.1 (Story S0.1.3). Read CLAUDE.md and docs/versions.md.

1. Write docs/dev-setup-windows.md: step-by-step install of
   - Git + Git LFS (and `git lfs install`)
   - Visual Studio 2022 Build Tools (Desktop C++ workload: MSVC, Windows SDK)
   - CMake >= 3.25 and Ninja (or the ones bundled with VS)
   - Python 3
   - the pinned Godot 4 editor (standard build, not .NET), saved to a known folder
   - optional: VS Code with clangd or the C/C++ extension, and the Godot Tools extension
   Include how to open "Developer PowerShell for VS 2022" and the exact build/test commands from CLAUDE.md.
2. Write tools/check-env.ps1: checks each tool's presence and minimum version and prints a PASS/FAIL
   table. Also checks that cl.exe is on PATH (i.e., running in a developer shell) and gives a hint if not.
   Exit code 1 if any required check fails.
3. Run the script and the windows-debug preset build and tests on this PC. Fix any preset issues found
   (for example, the MSVC runtime library setting or Ninja generator problems) in CMakePresets.json.

Report the check-env output in your final message.
Follow the Task workflow in CLAUDE.md (branch story/S0.1.3-windows-env); open the PR.
```

### S0.1.4 — Continuous integration
> *As the developer, every PR is built and tested automatically on Linux and Windows, so regressions are caught before merge.*

**Acceptance criteria**
- A GitHub Actions workflow runs on PRs and on pushes to `main`.
- Jobs: Linux (GCC and Clang) build and test, Windows (MSVC) build and test, all with warnings as errors.
- FetchContent downloads are cached.

#### [x] T0.1.4.1 — GitHub Actions CI
- **Model:** Sonnet · **Session:** [Cloud] · **Depends on:** T0.1.1.2
```
Task T0.1.4.1 (Story S0.1.4). Read CLAUDE.md and docs/architecture.md §13.

Create .github/workflows/ci.yml:
- Triggers: pull_request (any branch), push to main, workflow_dispatch.
- concurrency: cancel in-progress runs for the same ref.
- Job `linux` (ubuntu-latest), matrix compiler [gcc, clang]:
  checkout with submodules, install ninja, configure with the ci-linux preset (add ci-linux-clang if
  needed), build, ctest. Cache the FetchContent download dir (set FETCHCONTENT_BASE_DIR to a cacheable
  path) keyed on a hash of cmake/Dependencies.cmake.
- Job `windows` (windows-latest): set up an MSVC dev environment (ilammy/msvc-dev-cmd or equivalent),
  configure a windows-debug-style preset with warnings as errors (add a ci-windows preset), build, ctest.
- Job `lint`: a grep-based determinism lint per docs/architecture.md §4 that fails if sim/src (excluding
  sim/src/runner and sim/src/profiling) contains `rand(`, `std::random_device`, `#include <random>`, or
  `std::chrono`. Put the lint in tools/lint_determinism.sh so it can be run locally too.
- Placeholders as commented-out steps (not jobs) for: content validation and the Godot smoke test (later tasks enable them).

Make sure the workflow YAML is valid (use a YAML linter or python yaml.safe_load).
Run tools/lint_determinism.sh locally. Follow the Task workflow in CLAUDE.md
(branch story/S0.1.4-ci); open the PR and confirm CI is green on it. If CI fails, fix and push until green.
```

---

## F0.2 — Simulation Core Kernel

### S0.2.1 — World and component registry
> *As a systems developer, I have a `World` wrapping EnTT and a registry of stateful component types, so every later system shares one data model that can be hashed and saved.*

**Acceptance criteria**
- `World` exposes entity create and destroy, component access, and singleton context state.
- `ComponentRegistry` stores a stable name and hash/serialize hooks for each type. Unit tests cover it.

#### [x] T0.2.1.1 — World and ComponentRegistry
- **Model:** Sonnet · **Session:** [Cloud] · **Depends on:** T0.1.1.2
```
Task T0.2.1.1 (Story S0.2.1). Read CLAUDE.md and docs/architecture.md §4, §5.1, §10.

Implement in namespace citysim::core:
1. sim/include/citysim/core/world.hpp + src: class World
   - owns an entt::registry
   - create(), destroy(e), valid(e)
   - template get<T>/try_get<T>/emplace<T>/remove<T>
   - view<Ts...>()
   - ctx() access for singleton state (wrap registry.ctx())
   - entity_count()
   - non-copyable, movable
2. sim/include/citysim/core/component_registry.hpp: class ComponentRegistry
   - template<class T> void register_type(std::string_view stable_name, Hooks<T>)
   - Hooks hold: a `void (*hash)(StateHasher&, const T&)` placeholder (forward-declare StateHasher;
     a later task implements it) and a placeholder `serialize` slot (std::function or fn ptr, may be
     null for now)
   - type-erased storage so the registry can iterate all registered types sorted by stable_name
     (iteration order must be by name, never by registration order)
   - duplicate names or duplicate types → throw std::logic_error
   - lookup by name and by type
   World owns a ComponentRegistry (accessor registry()).
3. Tests (sim/tests/core/world_tests.cpp, component_registry_tests.cpp): create/destroy, component
   add/get/remove, ctx singletons, registry ordering by name regardless of registration order,
   duplicate detection.

Keep headers lean; put non-template code in .cpp. Build and run all tests (linux-debug).
Follow the Task workflow in CLAUDE.md (branch story/S0.2.1-world); last task of the story, so open the PR.
```

### S0.2.2 — Deterministic randomness and state hashing
> *As a developer, randomness is reproducible and I can fingerprint the whole simulation state, so determinism can be tested automatically.*

**Acceptance criteria**
- The RNG API matches architecture §5.4, with golden-value tests.
- `World::state_hash()` covers every registered component in a deterministic order.

#### [ ] T0.2.2.1 — Deterministic RNG
- **Model:** Sonnet · **Session:** [Cloud] · **Depends on:** T0.2.1.1
```
Task T0.2.2.1 (Story S0.2.2). Read CLAUDE.md and docs/architecture.md §4 and §5.4.

Implement namespace citysim::rng in sim/include/citysim/core/rng.hpp (+ .cpp where useful):
1. constexpr uint64_t mix64(uint64_t) — the SplitMix64 finalizer.
2. constexpr uint64_t hash64(uint64_t seed, uint64_t stream, uint64_t tick, uint64_t key, uint64_t n)
   — combine inputs by chained mixing (document the exact formula in a comment; it is part of
   save compatibility and must never change silently).
3. constexpr uint64_t stream(std::string_view name) — FNV-1a 64 of the name (compile-time usable).
4. class Pcg32 (PCG-XSH-RR 64/32): seed(state, inc), next_u32(); construct from hash64 output.
5. Distribution functions written by us (never <random>), as free functions taking a uint64 or a Pcg32&:
   uniform_int(lo, hi) inclusive with unbiased rejection (Lemire's method), uniform_float01() using the
   top 24 bits (float) / 53 bits (double), chance(p), weighted_index(std::span<const float>),
   normal(mean, sd) via Box–Muller using std::log/std::sqrt/std::cos.
6. A convenience struct RngContext { uint64_t world_seed; uint64_t tick; } with
   uint64_t at(uint64_t stream, uint64_t key, uint64_t n = 0) const.

Tests (sim/tests/core/rng_tests.cpp):
- golden values: hard-code the first 5 outputs for fixed inputs (compute once, paste them in, and comment
  that changing them breaks saves)
- uniform_int stays in range and is roughly uniform over 1e5 samples (loose chi-square bound)
- weighted_index respects zero weights
- stream("a") != stream("b")
- compile-time stream() works in a static_assert

Run all tests. Follow the Task workflow in CLAUDE.md (branch story/S0.2.2-rng-hash).
```

#### [ ] T0.2.2.2 — State hashing
- **Model:** Sonnet · **Session:** [Cloud] · **Depends on:** T0.2.2.1
```
Task T0.2.2.2 (Story S0.2.2, last task). Read docs/architecture.md §4, §5.1, §13.

1. Implement citysim::core::StateHasher (sim/include/citysim/core/state_hasher.hpp) on top of xxHash
   XXH3_64bits streaming: add(bytes), template add_pod<T>(const T&) restricted to trivially copyable
   types, add(std::string_view), add_float(float/double) (hash the bit pattern; normalize -0.0 to 0.0
   and all NaNs to one canonical NaN), digest().
2. Complete the hash hook in ComponentRegistry. Add World::state_hash() that:
   - hashes the entity list in ascending entity id order (entity value, version)
   - for each registered component type, sorted by stable name: hashes the name, then every
     (entity, component) pair in ascending entity order via that type's hash hook
   - hashes registered ctx singletons the same way (add register_singleton<T>(name, hooks) to the
     registry, also iterated by name)
3. Tests: the same operations in two worlds give equal hashes; changing one field changes the hash;
   registration order does not matter; creation order differences that lead to the same final state
   with the same entity ids give equal hashes.

Run all tests. Follow the Task workflow in CLAUDE.md; last task of S0.2.2, so open the PR.
```

### S0.2.3 — Tick loop, scheduler and commands
> *As a systems developer, I can register systems into ordered phases and submit commands that apply deterministically at tick boundaries, so all gameplay builds on one predictable loop.*

**Acceptance criteria**
- `Simulation::step()` runs the phases from architecture §5.2.
- Commands carry sequence numbers and are recorded in a replayable input log.
- `citysim_headless` runs N ticks and prints hashes, and can replay an input log with an identical result.

#### [ ] T0.2.3.1 ★ — Scheduler, phases, TickContext, Simulation
- **Model:** **Opus** · **Session:** [Cloud] · **Depends on:** T0.2.2.2
```
Task T0.2.3.1 (Story S0.2.3). This is architecture-critical. Read docs/architecture.md §4–§7 fully.

Design and implement the simulation kernel in namespace citysim::core:
1. enum class Phase { ApplyCommands, PreUpdate, Update, PostUpdate, Calendar, Publish }.
2. class ISystem { virtual std::string_view name() const = 0; virtual void update(TickContext&) = 0;
   virtual ~ISystem(); } plus optional on_register(World&).
3. struct TickContext: World&, uint64 tick, RngContext rng, a JobSystem* (forward-declared; may be
   nullptr until S0.2.4 — systems must handle null by running serially through a helper), an EventBus&.
4. class EventBus: typed events via a std::variant<...> declared in a central events.hpp (start with a
   DebugEvent{std::string}). Events emitted during phase P become readable from phase P+1 onward, and
   are cleared after Publish. Deterministic order: emission order.
5. class Scheduler: add(Phase, std::unique_ptr<ISystem>), run_phase(Phase, TickContext&). Order within
   a phase = registration order; duplicate system names → throw.
6. class Simulation: constructed with a world seed; owns World, Scheduler, EventBus; uint64 tick();
   void step() runs all phases in order and increments tick at the end; exposes world(), scheduler().
   Simulation knows nothing about threads or real time.
7. Per-system timing hook: an optional callback interface Simulation calls around each system
   (used later for profiling); no wall-clock calls inside sim/src/core itself.

Write a short design note at the top of simulation.hpp explaining the tick contract (what a system may
and may not do in each phase).
Tests: phase ordering, registration order within phases, event visibility rules, tick increment,
two simulations with the same seed and the same systems give the same state_hash after 100 steps (use a
small test system that mutates components with rng).
Run all tests and the determinism lint. Follow the Task workflow in CLAUDE.md (branch story/S0.2.3-kernel).
```

#### [ ] T0.2.3.2 — Commands, CommandQueue, input log
- **Model:** Sonnet · **Session:** [Cloud] · **Depends on:** T0.2.3.1
```
Task T0.2.3.2 (Story S0.2.3). Read docs/architecture.md §5.3 and the kernel in sim/include/citysim/core/.

1. sim/include/citysim/core/commands.hpp: plain structs NoOp{}, DebugSetComponentValue{uint32 entity;
   int32 value} (test-only), and using Command = std::variant<...>. Leave a clear comment on how to add
   new commands (game commands are added by later tasks in their own modules and listed here).
2. struct SequencedCommand { uint64 sequence; uint64 issue_tick; Command cmd; }.
3. class CommandQueue: thread-safe MPSC (a mutex is fine); submit(Command) assigns increasing sequence
   numbers; drain(std::vector<SequencedCommand>&) takes everything submitted so far.
4. In Simulation: a pending-commands buffer; Simulation::enqueue(std::vector<SequencedCommand>) for the
   runner; the ApplyCommands phase runs a built-in CommandDispatcher that applies the commands in sequence
   order via handlers registered per command type (register_handler<T>(fn)). Unhandled command types →
   log a warning, do not crash.
5. InputLog: records (tick, sequence, command) for each applied command; serialize later (S0.5) — for now
   provide an in-memory API and a simple text dump for debugging.

Tests: ordering by sequence, multi-threaded submit (4 threads × 1000 commands, all applied exactly once,
in sequence order), unhandled commands, input log contents.
Run all tests. Follow the Task workflow in CLAUDE.md (same story branch).
```

#### [ ] T0.2.3.3 — Determinism harness and headless runner CLI
- **Model:** Sonnet · **Session:** [Cloud] · **Depends on:** T0.2.3.2
```
Task T0.2.3.3 (Story S0.2.3, last task). Read docs/architecture.md §5.3 and §13.

1. sim/tests/support/determinism.hpp: helper run_scenario(seed, ticks, setup_fn, script) where script maps
   tick → commands; returns the vector of per-tick state hashes. Helper expect_same_hashes(a, b) that
   reports the first diverging tick.
2. A test that runs a scenario twice and compares hashes every tick; and a negative test proving a
   deliberately different command at tick 50 makes hashes diverge from tick 50 on (tick 51 after apply).
3. sim/tools/headless/: executable citysim_headless with options (use a tiny hand-written arg parser, no
   new dependency): --seed N, --ticks N, --hash-every N (print "tick hash" lines), --record-log FILE,
   --replay FILE. Use a text format for the log file for now (one command per line, document it); S0.5
   will switch it to binary. Replay must reproduce the identical final hash; print it.
4. Add a CTest test that runs citysim_headless --seed 42 --ticks 500 twice and compares the output.

Run all tests. Follow the Task workflow in CLAUDE.md; last task of S0.2.3, so open the PR.
```

### S0.2.4 — Multithreading
> *As a player, the simulation uses all my CPU cores and runs on its own thread without breaking determinism, so large cities stay smooth.*

**Acceptance criteria**
- `JobSystem::parallel_for` gives identical results with 1, 2 and N workers.
- `SimulationRunner` paces ticks for pause/1×/2×/4× and publishes triple-buffered snapshots.
- Tests use a fake clock.

#### [ ] T0.2.4.1 ★ — JobSystem with deterministic parallel_for
- **Model:** **Opus** · **Session:** [Cloud] · **Depends on:** T0.2.3.3
```
Task T0.2.4.1 (Story S0.2.4). Architecture-critical. Read docs/architecture.md §4 and §7.2.

Implement citysim::core::JobSystem:
- constructor(worker_count) — 0 means max(1, hardware_concurrency - 2); worker_count 1 means only the
  calling thread runs work (no worker threads at all).
- parallel_for(size_t count, size_t chunk_size, Fn fn) where fn(size_t begin, size_t end, size_t chunk_index).
  Chunks are [i*chunk_size, min(count,(i+1)*chunk_size)); the calling thread participates; blocks until
  every chunk is done; exceptions in chunks are captured and rethrown on the calling thread (first by
  chunk index).
- template helper parallel_map_reduce(count, chunk_size, map_fn, init, reduce_fn) where the per-chunk
  results are reduced IN CHUNK ORDER on the calling thread (this is the determinism pattern).
- template helper ChunkedOutput<T>: per-chunk std::vector<T> buffers + merge_into(std::vector<T>&) in
  chunk order.
- No allocation per chunk beyond what callers request; reuse internal structures across calls.
- Clean shutdown in the destructor.
Wire it in: Simulation owns a JobSystem (worker count from constructor arg, default 0) and passes it in
TickContext; replace the null-JobSystem fallback from T0.2.3.1 with this real system.

Tests: results (including float sums via parallel_map_reduce) are bit-identical for worker counts 1, 2,
8; exception propagation; 10,000 tiny parallel_for calls without deadlock; a simulation test using
parallel_for in a system produces identical state hashes with 1 and 8 workers.
Also run the tests under ThreadSanitizer: add a linux-tsan preset (clang, -fsanitize=thread) and report results.
Follow the Task workflow in CLAUDE.md (branch story/S0.2.4-threading).
```

#### [ ] T0.2.4.2 ★ — SimulationRunner and snapshot triple buffer
- **Model:** **Opus** · **Session:** [Cloud] · **Depends on:** T0.2.4.1
```
Task T0.2.4.2 (Story S0.2.4, last task). Architecture-critical. Read docs/architecture.md §6, §7.1, §7.3.

Implement in sim/src/runner/ (wall-clock time is allowed only here):
1. interface IClock { virtual double now_seconds() = 0; virtual void sleep_until(double) = 0; } with
   SteadyClock (std::chrono::steady_clock) and FakeClock (manually advanced, for tests).
2. struct RenderSnapshot { uint64 tick; uint8 speed; bool paused; double published_at; SimStats stats
   (last tick ms, avg tick ms over 60 ticks, entity count, ticks per second measured); std::vector<UiEvent>
   ui_events; } — keep it extensible; renderers will add arrays later.
3. template TripleBuffer<T>: single producer, single consumer, lock-free (atomic index swap), consumer always
   gets the newest complete value, producer never blocks.
4. class SimulationRunner: owns Simulation, CommandQueue, TripleBuffer<RenderSnapshot>, a std::thread.
   - start(), stop() (joins), submit(Command), latest_snapshot(), set_publish_hook(fn(Simulation&,
     RenderSnapshot&)) so other modules can add data to snapshots.
   - Speed table from docs/architecture.md §6 (0/20/40/80 tps), read from a struct so content can
     override later.
   - Loop: compute the next tick deadline from the speed; drain commands into the simulation; step; publish;
     sleep until the deadline. If behind, run up to max_catchup_ticks (default 5) back to back, then reset the
     deadline (game slows down instead of spiraling).
   - Paused: block on a condition variable; wake on submit or stop. Commands submitted while paused are
     applied without advancing the tick (apply-only path) so the UI can see the effects.
   - SetSpeed/TogglePause handling stays in the core clock (T0.3.1.1); for now add a minimal
     runner-level speed setter used by tests and note it will route through commands.
5. Tests with FakeClock: at 1× after 1.0 fake seconds → 20 ticks; at 4× → 80; paused → 0 and still
   applies commands; catch-up limit respected; latest_snapshot returns the newest tick; stop() is clean.
   Run under the linux-tsan preset too.

Follow the Task workflow in CLAUDE.md; last task of S0.2.4, so open the PR.
```

### S0.2.5 — Logging and profiling
> *As a developer, I get categorized logs and optional profiler zones, so I can diagnose problems without affecting the simulation.*

**Acceptance criteria**
- `CS_LOG_*` macros with categories.
- Tracy zones compile to nothing unless `CITYSIM_TRACY=ON`.
- Logging never changes simulation state.

#### [ ] T0.2.5.1 — Logging and optional Tracy
- **Model:** Sonnet · **Session:** [Cloud] · **Depends on:** T0.2.4.2
```
Task T0.2.5.1 (Story S0.2.5). Read CLAUDE.md and docs/architecture.md §3–§4.

1. sim/include/citysim/core/log.hpp: wrapper over spdlog with categories (core, runner, content, save,
   sim, bridge) as named loggers. Macros CS_LOG_TRACE/DEBUG/INFO/WARN/ERROR(category, fmt, ...).
   log::init(LogConfig{level per category, file path optional, console on/off}). Messages must not
   affect state (no side effects in arguments — document it).
2. Replace any std::cout/cerr diagnostics in sim/src with the macros (CLI tool output to stdout stays).
3. sim/include/citysim/profiling/profile.hpp: CS_PROFILE_ZONE(name), CS_PROFILE_FRAME_MARK. With
   CITYSIM_TRACY=ON, fetch Tracy (pinned latest release tag, recorded in docs/versions.md) and map the
   macros to Tracy; otherwise they compile to nothing. Add zones around Simulation::step and each system
   call (via the per-system timing hook from T0.2.3.1).
4. Make sure the linux-debug build is unaffected, and that a build with -DCITYSIM_TRACY=ON compiles.

Run all tests. Follow the Task workflow in CLAUDE.md (branch story/S0.2.5-logging); open the PR.
```

---

## F0.3 — Time & Calendar

### S0.3.1 — Game speeds and the daily clock
> *As a player, I can pause and choose 1×, 2× or 4× speed, and the time of day advances, so I control the pace of the game.*

**Acceptance criteria**
- `SetSpeed` and `TogglePause` commands work through the runner.
- Time of day is derived from ticks and `day_length_ticks`.
- Values come from `defines/time.json`, with built-in defaults as a fallback until content loading exists.

#### [ ] T0.3.1.1 — GameClock: speeds, pause, daily cycle
- **Model:** Sonnet · **Session:** [Cloud] · **Depends on:** T0.2.4.2
```
Task T0.3.1.1 (Story S0.3.1). Read docs/architecture.md §5.3, §6, §7.1.

Implement namespace citysim::time:
1. struct TimeDefines { uint32 base_tick_rate = 20; std::array<uint32,4> speed_multipliers{0,1,2,4};
   float movement_scale = 3.0f; uint32 day_length_ticks = 14400; } (content loading will fill it later;
   keep defaults here).
2. Singleton GameClock (registered with the ComponentRegistry for hashing; serialize hook comes in S0.5):
   uint8 speed (1..3 index into multipliers), bool paused, uint64 day_tick (ticks since day start),
   uint64 day_index. Functions: time_of_day_fraction(), hour(), minute(), movement_seconds_per_tick().
3. A ClockSystem in phase PreUpdate that advances the daily cycle each tick.
4. Commands SetSpeed{uint8} and TogglePause{} handled in ApplyCommands, updating GameClock. Replace the
   runner's temporary speed setter (T0.2.4.2): the runner reads speed/paused from the GameClock in the
   snapshot it publishes and paces accordingly.
5. Tests: the day wraps at day_length_ticks; hour/minute math; commands change speed and pause; with
   FakeClock, the runner at speed index 3 produces 80 ticks per fake second after a SetSpeed command.

Run all tests. Follow the Task workflow in CLAUDE.md (branch story/S0.3.1-clock); open the PR.
```

### S0.3.2 — Calendar with era-specific cadence
> *As a player, the calendar advances at a pace that fits the current era and can show BCE dates, so millennia of ancient history and decades of modern history both play well.*

**Acceptance criteria**
- Exact integer calendar advance with no drift: in tests, a year takes exactly `real_seconds_per_year × base_tick_rate` ticks at 1×, across eras.
- BCE/CE formatting uses localization keys.
- Calendar boundary events fire in a deterministic order.

#### [ ] T0.3.2.1 — Calendar date math and formatting
- **Model:** Sonnet · **Session:** [Cloud] · **Depends on:** T0.3.1.1
```
Task T0.3.2.1 (Story S0.3.2). Read docs/architecture.md §6 and §9.

Implement citysim::time::Calendar types:
1. CalendarTime = int64 seconds since epoch (epoch = 1 Jan of astronomical year 0).
2. struct Date { int32 year; uint8 month (1–12); uint8 day (1–31); } with to_calendar_time(Date) and
   to_date(CalendarTime) for 365-day years with fixed month lengths
   (31,28,31,30,31,30,31,31,30,31,30,31), correct for negative years (floor division!).
3. day_of_year, month boundaries, add_days, comparison operators.
4. format_date(Date, const Localizer*) → for now return a structured result {key, args} such as
   {"calendar.date.bce", {day, month_key, year_abs}} or ".ce"; year 0 shows as 1 BCE, year -2999 as 3000 BCE.
   (The Localizer arrives in S0.4.3; keep this as data, not a string.)
5. Tests: round-trips over many random dates including negative years (seeded via citysim::rng),
   boundary cases (year -1 → 0 → 1, Dec 31 → Jan 1), and BCE numbering.

Run all tests. Follow the Task workflow in CLAUDE.md (branch story/S0.3.2-calendar).
```

#### [ ] T0.3.2.2 — Era cadence: exact calendar advance per tick
- **Model:** Sonnet · **Session:** [Cloud] · **Depends on:** T0.3.2.1, T0.4.1.1
```
Task T0.3.2.2 (Story S0.3.2). Read docs/architecture.md §6 and §8.

1. Singleton CalendarState: CalendarTime now; uint64 remainder; current era (DefHandle<EraDef>, from
   T0.4.1.1). Register it for hashing.
2. CalendarSystem (phase Calendar): each tick, advance by
   seconds_per_tick = (365*86400) / (real_seconds_per_year * base_tick_rate)
   using integer division and carrying the remainder exactly (numerator 365*86400*1 accumulates in
   `remainder`; never use floating point here).
3. Command SetEra{era_id string} → resolves through the content registry, switches cadence from the
   next tick, keeps the remainder carry consistent (document the choice).
4. Start date: new_game takes a start Date (default from the era def's `default_start_year`; add that
   field to EraDef if it is missing).
5. Tests: at the Ancient cadence (120 s/yr), exactly 2400 ticks advance exactly one year; at Modern
   (720 s/yr) exactly 14,400 ticks advance one year; switching eras mid-year keeps total time exact; a
   10-million-tick run shows no drift against closed-form math.

Run all tests. Follow the Task workflow in CLAUDE.md (same story branch).
```

#### [ ] T0.3.2.3 — Calendar scheduler (day/month/year boundaries, dated triggers)
- **Model:** Sonnet · **Session:** [Cloud] · **Depends on:** T0.3.2.2
```
Task T0.3.2.3 (Story S0.3.2, last task). Read docs/architecture.md §5.2, §5.5, §6.

1. When the calendar crosses day, month or year boundaries during a tick (possibly several days per tick
   in fast eras), the CalendarSystem emits events DayStarted{date}, MonthStarted{date},
   YearStarted{year} — one per boundary crossed, in chronological order.
2. CalendarScheduler singleton: schedule(CalendarTime when, ScheduledAction action) where
   ScheduledAction is a std::variant of plain structs (start with DebugAction{std::string tag}) — no
   std::function (it must be savable). Due actions fire in (time, insertion sequence) order as
   ScheduledActionDue events. Register for hashing.
3. Tests: multiple day boundaries within one tick in the Ancient cadence produce the right count and
   order; scheduled actions fire exactly once, in order, including ones scheduled for the same time;
   determinism across two runs.

Run all tests. Follow the Task workflow in CLAUDE.md; last task of S0.3.2, so open the PR.
```

---

## F0.4 — Data-Driven Content & Mods

### S0.4.1 — Content definitions and registry
> *As a designer or modder, I define game content in JSON files and the game loads, cross-references and validates them, so content never needs code changes.*

**Acceptance criteria**
- `DefRegistry<T>` with handles, sorted by id.
- Reference resolution with file and JSON-path errors.
- The `citysim_validate` CLI runs in CI.
- Base era definitions exist.

#### [ ] T0.4.1.1 — Content loader and DefRegistry
- **Model:** Sonnet · **Session:** [Cloud] · **Depends on:** T0.2.1.1
```
Task T0.4.1.1 (Story S0.4.1). Read docs/architecture.md §8 carefully.

Implement namespace citysim::content (single mod root for now; mod layering is T0.4.2.1):
1. ContentId: string "<mod>:<name>" with validation (lowercase a–z, 0–9, '_', '.', one ':').
2. template DefHandle<T> (uint32 index, invalid sentinel), DefRegistry<T>: after finalize(), defs are
   sorted by id; get(handle), find(id) → optional handle, all() span. Duplicate ids → error.
3. A type registry for def kinds: register_def_type<T>("era", parse_fn) where parse_fn(const json&,
   ParseContext&) → T. ParseContext collects errors with file path + JSON pointer.
4. Loader: load_directory(path_to_mod_root) reads defs/**/*.json in sorted path order; each file is
   {"defs":[{"type":"era","id":"base:era.ancient",...}]}; dispatches by "type"; unknown types → error.
5. First def types:
   - EraDef { ContentId id; std::string name_key; std::string desc_key; uint32 real_seconds_per_year;
     int32 default_start_year; int32 order; }
   - TimeDefines (from defines/time.json, a single object rather than defs; fall back to the struct
     defaults from T0.3.1.1 if the file is missing)
6. ContentDatabase: owns all registries; load(paths) → LoadResult {errors, warnings}.
7. Tests with fixture content in sim/tests/fixtures/content/: happy path, duplicate id, unknown type,
   missing required field (with JSON pointer in the message), sorted order regardless of file order.

Run all tests. Follow the Task workflow in CLAUDE.md (branch story/S0.4.1-content).
```

#### [ ] T0.4.1.2 — References, validation, citysim_validate CLI
- **Model:** Sonnet · **Session:** [Cloud] · **Depends on:** T0.4.1.1
```
Task T0.4.1.2 (Story S0.4.1). Read docs/architecture.md §8 and §13.

1. template DefRef<T>: parsed as an id string, resolved to a DefHandle<T> in a resolve pass after all
   files load; unresolved → error with file + JSON pointer + the missing id. Add a `next_era` optional
   DefRef<EraDef> to EraDef to exercise it.
2. A validation pass per def type (validate_fn registered alongside parse_fn): e.g., EraDef
   real_seconds_per_year > 0, unique `order` values.
3. sim/tools/validate/: citysim_validate <content_root> [--strict] prints errors and warnings in the
   form "path:json_pointer: message" and exits non-zero on errors (or on warnings with --strict).
4. Enable the content-validation step in .github/workflows/ci.yml (it runs against game/content).
5. Tests for unresolved references, validation failures, and CLI exit codes (via CTest).

Run all tests. Follow the Task workflow in CLAUDE.md (same story branch).
```

#### [ ] T0.4.1.3 — Author base era definitions and time defines (data entry)
- **Model:** **Haiku** · **Session:** [Cloud] · **Depends on:** T0.4.1.2
```
Task T0.4.1.3 (Story S0.4.1, last task). Data entry only — do not change C++ code.

Create game/content/base/mod.json: {"id":"base","name_key":"mod.base.name","version":"0.0.1",
"depends_on":[]}.

Create game/content/base/defs/eras.json with four era defs (type "era"):
- base:era.ancient  — real_seconds_per_year 120, default_start_year -3000, order 1, next_era base:era.colonial
- base:era.colonial — 300, 1600, order 2, next_era base:era.modern
- base:era.modern   — 720, 1900, order 3, next_era base:era.utopian
- base:era.utopian  — 900, 2025, order 4, no next_era
Each has name_key "era.<name>.name" and desc_key "era.<name>.desc".

Create game/content/base/defines/time.json with base_tick_rate 20, speed_multipliers [0,1,2,4],
movement_scale 3.0, day_length_ticks 14400.

Create game/content/base/loc/en/eras.json with the name and one-sentence description strings for all
four eras (serious, grounded tone; Utopian: "From the present day to 2100 and beyond: humanity's future,
utopian or otherwise."), plus "mod.base.name": "Base Game".

Run citysim_validate game/content (build it with the linux-debug preset first) and fix data errors until
it passes. Follow the Task workflow in CLAUDE.md; last task of S0.4.1, so open the PR.
```

### S0.4.2 — Mod layering
> *As a modder, I can ship a mod folder that adds, replaces or patches base content and declares dependencies, so mods work without touching the base game.*

**Acceptance criteria**
- Topological load order, with ties broken by id.
- Replace and patch (RFC 7386) semantics.
- A content hash covers the mod list and files.
- The base game loads as mod `base`.

#### [ ] T0.4.2.1 — Mod manifests, load order, override and patch, content hash
- **Model:** Sonnet · **Session:** [Cloud] · **Depends on:** T0.4.1.3
```
Task T0.4.2.1 (Story S0.4.2). Read docs/architecture.md §8.

1. ModManifest from mod.json: id, name_key, version (semver string), depends_on [ids],
   load_after [ids, optional soft ordering]. Discover mods in a list of roots (each subfolder with
   mod.json). Enabled-mod list is an input (default: all discovered).
2. Load order: topological sort by depends_on + load_after, ties broken by mod id; missing hard
   dependency → error; cycles → error naming the cycle. `base` must load first.
3. Overrides: a later mod defining an existing id replaces it. A def with "patch": true is applied as
   JSON Merge Patch (RFC 7386 — implement it, it is short) onto the earlier def's JSON *before* parsing.
   Patch of a missing id → error. Track which mod last touched each def (for error messages and the
   encyclopedia later).
4. Content hash: XXH3 over (ordered mod ids + versions + every loaded file's relative path and bytes).
5. Add a fixture mod in sim/tests/fixtures/content/ that patches base:era.ancient real_seconds_per_year
   and adds a new era; tests for order, replace, patch, cycles, missing dependency, and that the hash
   changes when a file changes.
6. Add game/content/mods/example_mod/ (patch the Ancient era's desc_key to a new key + loc string),
   disabled by default, and document mod structure briefly in game/content/mods/README.md.

Run all tests and citysim_validate. Follow the Task workflow in CLAUDE.md (branch story/S0.4.2-mods); open the PR.
```

### S0.4.3 — Localization
> *As a player, all game text comes from translatable tables, so the game can be localized later without code changes.*

**Acceptance criteria**
- `Localizer` with fallback and `{named}` placeholders.
- The validator checks that every `*_key` exists in `en`.
- Calendar formatting renders through the Localizer.

#### [ ] T0.4.3.1 — Localizer and key validation
- **Model:** Sonnet · **Session:** [Cloud] · **Depends on:** T0.4.2.1, T0.3.2.1
```
Task T0.4.3.1 (Story S0.4.3). Read docs/architecture.md §9.

1. citysim::content::Localizer: loads loc/<lang>/*.json from every enabled mod in load order (later mods
   override keys); set_language(lang); get(key) → current → "en" → the key itself (and a missing-key
   counter); format(key, args) with {name} placeholders (args as a small vector of name/value pairs).
2. Validator: every string field whose name ends in "_key" in any def must exist in en (error); keys
   missing in other loaded languages → warning. Unused keys → info (not shown unless --verbose).
3. Add calendar loc keys to game/content/base/loc/en/calendar.json: month names (calendar.month.1–12),
   calendar.date.bce "{day} {month} {year} BCE", calendar.date.ce "{day} {month} {year} CE".
   Wire format_date from T0.3.2.1 into a helper that renders via the Localizer.
4. Tests: fallback order, mod override, placeholder substitution, missing-key behaviour, validator errors.

Run all tests and citysim_validate. Follow the Task workflow in CLAUDE.md (branch story/S0.4.3-loc); open the PR.
```

---

## F0.5 — Save & Load

### S0.5.1 — Save and load the exact game state
> *As a player, I can save and later load my game and it continues exactly as before, so long campaigns are safe.*

**Acceptance criteria**
- `.csav` container per architecture §10, zstd-compressed.
- Round-trip hash equality, plus continuation equality (run N ticks vs. save at k, load, run N−k).
- Section versioning with migrations and a fixture save.
- Save and load are commands executed between ticks, with an autosave interval.

#### [ ] T0.5.1.1 ★ — Serialization framework (archives and component hooks)
- **Model:** **Opus** · **Session:** [Cloud] · **Depends on:** T0.3.2.3, T0.4.2.1
```
Task T0.5.1.1 (Story S0.5.1). Architecture-critical. Read docs/architecture.md §5.1 and §10 fully.

Design and implement namespace citysim::save:
1. BinaryWriter / BinaryReader over byte buffers: little-endian fixed ints, LEB128 varints (signed via
   zigzag), floats as bit patterns, length-prefixed UTF-8 strings, spans of PODs, bounds-checked reads
   that throw SaveFormatError with offset info.
2. A single-function serialization pattern: template<class Ar> void serialize(Ar&, T&, uint32 version)
   where Ar is Writer or Reader (Ar::is_loading). Provide helpers for std::vector, std::string,
   std::optional, std::array, std::variant (index + value), enums, and entity references.
3. EntityRemap: on save, entities are written as dense indices (0..n-1 in ascending entity order); on
   load, new entities are created and an index→entity table resolves references. Entity-typed fields
   must go through ar.entity(e).
4. Complete the ComponentRegistry serialize hook: each registered component/singleton type provides
   serialize + a current version number. Register serialization for every existing stateful type
   (GameClock, CalendarState, CalendarScheduler, the test components, etc.).
5. World-level functions save_world(World&, Writer&) / load_world(World&, Reader&) that write, per type
   sorted by stable name: name, version, count, then (entity index, data) pairs.
6. Tests: primitive round-trips, variant/optional/vector round-trips, entity references survive remap,
   truncated buffer → SaveFormatError, world round-trip gives an equal state_hash.

Document the pattern with a worked example at the top of archive.hpp (later Sonnet tasks will copy it).
Follow the Task workflow in CLAUDE.md (branch story/S0.5.1-save).
```

#### [ ] T0.5.1.2 — `.csav` container, compression, continuation tests
- **Model:** Sonnet · **Session:** [Cloud] · **Depends on:** T0.5.1.1
```
Task T0.5.1.2 (Story S0.5.1). Read docs/architecture.md §10 and the archive.hpp example.

1. SaveFile writer/reader: header (magic "CSAV", container version u16, game version string, world
   seed, tick, content hash, mod list [id, version], real-world timestamp as metadata only), then
   sections [4-char tag, u16 section version, u32 compressed length, u32 raw length, zstd bytes].
   Sections: META, CLCK (clock + calendar), ENTS, one per component type (WORLD data from
   save_world may be a single WRLD section if simpler — document the choice), CMDQ (pending commands),
   ILOG (input log, optional; switch the headless runner's text log to this binary form).
2. Unknown section tags are skipped with a warning; a content-hash mismatch is a warning returned to the
   caller (the UI will ask the user), not an error.
3. Simulation::save(path) / Simulation::load(path) (path-based for now; the runner wraps them in T0.5.1.4).
4. Tests: header round-trip; corrupted magic → error; save → load → equal state_hash; continuation test:
   run A = 1000 ticks with a scripted command set; run B = 400 ticks, save, load into a fresh
   Simulation, run 600 → identical per-tick hashes for ticks 401–1000.
5. citysim_headless: --save-at TICK FILE and --load FILE options.

Run all tests. Follow the Task workflow in CLAUDE.md (same story branch).
```

#### [ ] T0.5.1.3 — Save versioning and migrations
- **Model:** Sonnet · **Session:** [Cloud] · **Depends on:** T0.5.1.2
```
Task T0.5.1.3 (Story S0.5.1). Read docs/architecture.md §10.

1. MigrationRegistry: register(section_tag or component stable name, from_version, fn) where fn upgrades
   raw bytes (or a Reader → Writer pass) to from_version + 1. On load, chain migrations up to the
   current version; missing migration → clear error naming the type and versions.
2. Produce a fixture: bump a test component to version 2 (add a field with a default), write a
   migration 1→2, and commit a v1 fixture save generated before the bump into
   sim/tests/fixtures/saves/ (generate it with a small one-off test or tool; keep the generator in the
   repo, disabled by default).
3. Tests: the v1 fixture loads and has the default field value; a missing migration fails clearly.
4. Add a "Saves" section to docs/architecture.md §10 explaining when to bump versions (any change to a
   serialized layout) and the fixture policy.

Run all tests. Follow the Task workflow in CLAUDE.md (same story branch).
```

#### [ ] T0.5.1.4 — Save and load commands in the runner, autosave
- **Model:** Sonnet · **Session:** [Cloud] · **Depends on:** T0.5.1.3
```
Task T0.5.1.4 (Story S0.5.1, last task). Read docs/architecture.md §7.1 and §10.

1. Commands SaveGame{path}, LoadGame{path}, executed by SimulationRunner strictly between ticks (also
   while paused). Results are reported via UiEvent in the next snapshot (SaveCompleted{path, ok, msg},
   LoadCompleted{...}, including content-hash-mismatch warnings).
2. Load replaces the Simulation's state (or the Simulation object) safely; the snapshot buffer must
   publish a fresh snapshot after load.
3. Autosave: TimeDefines gains autosave_interval_real_seconds (default 600, 0 = off) and autosave_slots
   (default 3, rotating files autosave_1..3.csav in a configured directory). Autosave timing uses the
   runner's IClock (wall time), never the simulation.
4. Tests with FakeClock: save/load via commands round-trips; autosave triggers at the interval and rotates
   slots; a load while paused publishes a snapshot.

Run all tests (including the tsan preset). Follow the Task workflow in CLAUDE.md; last task of S0.5.1, so open the PR.
```

---

## F0.6 — Godot Integration

### S0.6.1 — Godot project running the C++ simulation
> *As a developer, I open the Godot project and the C++ simulation runs inside it on Linux and Windows, so presentation work can begin.*

**Acceptance criteria**
- godot-cpp submodule pinned to match the Godot version.
- `citysim_gdext` builds for Linux and Windows into `game/bin/`.
- The Godot headless smoke test passes in CI.
- The editor runs the main scene on the PC.

#### [ ] T0.6.1.1 — godot-cpp submodule and GDExtension library
- **Model:** Sonnet · **Session:** [Cloud] · **Depends on:** T0.5.1.4
```
Task T0.6.1.1 (Story S0.6.1). Read CLAUDE.md and docs/architecture.md §2, §3, §11.

1. Choose the latest stable Godot 4.x release; record it in docs/versions.md. Add godot-cpp as a git
   submodule at third_party/godot-cpp pinned to the tag/branch the godot-cpp README specifies for that
   Godot version; record it in docs/versions.md.
2. gdext/CMakeLists.txt (enabled by CITYSIM_BUILD_GDEXT): build godot-cpp via its CMake support
   (target godot-cpp), then a SHARED library citysim_gdext linking citysim_core + godot-cpp. Output to
   game/bin/ with platform-specific names (libcitysim_gdext.linux.<debug|release>.x86_64.so,
   citysim_gdext.windows.<debug|release>.x86_64.dll). Use our warning flags only on our sources.
3. gdext/src/register_types.cpp/.hpp with the GDExtension entry point; a class CitySim : public Node with
   a bound method get_core_version() returning citysim::core::version().
4. game/bin/citysim.gdextension listing entry_symbol, compatibility_minimum = the pinned Godot minor,
   and the Linux/Windows debug/release library paths.
5. Add presets linux-debug-gdext / windows-debug-gdext (or turn CITYSIM_BUILD_GDEXT ON in the existing
   presets if build time is acceptable — document the choice).
6. Update the SessionStart hook / tools/godot.sh now that a Godot version is pinned (download the
   Linux editor binary, which supports --headless).

Verify the Linux build of the extension succeeds in this session. Follow the Task workflow in CLAUDE.md
(branch story/S0.6.1-gdext).
```

#### [ ] T0.6.1.2 — Godot project skeleton and headless smoke test
- **Model:** Sonnet · **Session:** [Cloud] · **Depends on:** T0.6.1.1
```
Task T0.6.1.2 (Story S0.6.1). Read docs/architecture.md §2 and §11. Godot files must be hand-written as
text (project.godot, .tscn, .gd) — keep them minimal and valid for the pinned Godot version.

1. game/project.godot: project name "CityBuilder (working title)", main scene res://scenes/main.tscn,
   Forward+ renderer, window 1920×1080 (resizable), GDScript static-typing warnings set to error for
   untyped declarations if supported, an autoload placeholder section (filled in T0.6.2.1).
2. Folders: game/scenes, game/scripts, game/ui, game/assets, game/tests (each with a .gdignore-free
   README or .gitkeep).
3. game/scenes/main.tscn + game/scripts/main.gd: on _ready, create a CitySim node and print its core version.
4. game/tests/smoke.gd (extends SceneTree): instantiates CitySim, checks get_core_version() is non-empty,
   prints PASS and quits with exit code 0; any failure → quit(1).
5. Run the smoke test headless in this session via tools/godot.sh --headless --path game -s
   res://tests/smoke.gd (the first run may need --import; handle that in the script/CI step).
6. Enable the Godot smoke-test step in CI (Linux job): build the gdext target, download the pinned
   Godot (cache it), run the smoke test.

Follow the Task workflow in CLAUDE.md (same story branch).
```

#### [ ] T0.6.1.3 — Windows build and editor verification
- **Model:** Sonnet · **Session:** [PC] · **Depends on:** T0.6.1.2
```
Task T0.6.1.3 (Story S0.6.1, last task). Read CLAUDE.md and docs/dev-setup-windows.md.

1. Build the Windows gdext preset in a Developer PowerShell. Fix any MSVC-specific errors or warnings
   (keep the code portable).
2. Launch the Godot editor on game/ (path from the pinned version), confirm the extension loads without
   errors in the Output panel, and run the main scene; the core version must print.
3. Run the smoke test headless on Windows too.
4. Add a Windows gdext build step to the CI windows job (build only; no Godot run on Windows CI).
5. Add a "Running the game" section to docs/dev-setup-windows.md (build → open editor → F5).

In your final message, list exactly what you verified and what the user should look at themselves (for
example, the editor Output panel). Follow the Task workflow in CLAUDE.md; last task of S0.6.1, so open the PR.
```

### S0.6.2 — Simulation API for the game
> *As a gameplay/UI developer, I control and observe the simulation from GDScript through one `Sim` autoload, so the UI never touches simulation internals.*

**Acceptance criteria**
- The `Sim` autoload provides lifecycle, commands, speed, save and load, snapshot info and signals.
- Snapshot interpolation alpha is available.
- Command dictionaries are validated in C++.

#### [ ] T0.6.2.1 — CitySim API and Sim autoload
- **Model:** Sonnet · **Session:** [Cloud] · **Depends on:** T0.6.1.3
```
Task T0.6.2.1 (Story S0.6.2). Read docs/architecture.md §5.3, §7, §11.

Extend the CitySim GDExtension class (gdext/src/city_sim.*):
1. new_game(seed: int, content_roots: PackedStringArray, enabled_mods: PackedStringArray,
   era_id: String) → Dictionary {ok, errors, warnings}; loads content (globalize res:// paths with
   ProjectSettings), creates a SimulationRunner, starts it.
2. shutdown(); is_running().
3. submit_command(cmd: Dictionary) → bool: dictionary {"type": "set_speed", "speed": 2} etc., converted
   to typed Commands in C++ with validation (unknown type or bad fields → push_error and return false).
   Support: set_speed, toggle_pause, set_era, save_game, load_game. Put the conversion table in its own
   file (command_marshal.cpp) so later tasks add commands in one place.
4. In _process: read the latest snapshot; emit signals tick_advanced(tick), date_changed(text) (when the
   day changes, using the Localizer), speed_changed(speed, paused), save_completed(path, ok, msg),
   load_completed(path, ok, msg).
5. get_stats() → Dictionary {tick, tps, tick_ms_avg, tick_ms_last, entity_count, speed, paused, date_text,
   time_text}.
6. Loc bridge: on new_game, build a Godot Translation for the current language from the Localizer tables
   and add it to TranslationServer so UI tr("key") works.
7. game/scripts/autoload/sim.gd as autoload "Sim": owns the CitySim node; new_game() with defaults
   (base mod, ancient era, seed from settings); forwards signals. Update main.gd to start a game on launch.
8. Extend smoke.gd: start a new game, wait ~1 s, assert tick > 0, submit toggle_pause, assert the tick stops
   advancing, then quit.

Run the smoke test headless. Follow the Task workflow in CLAUDE.md (branch story/S0.6.2-sim-api).
```

#### [ ] T0.6.2.2 — Snapshot access and interpolation for renderers
- **Model:** Sonnet · **Session:** [Cloud] · **Depends on:** T0.6.2.1
```
Task T0.6.2.2 (Story S0.6.2, last task). Read docs/architecture.md §7.3 and §11.

1. In gdext: a SnapshotView helper owned by CitySim that keeps the previous and current RenderSnapshot,
   computes interpolation alpha = clamp((now - current.published_at) / tick_interval, 0, 1) each frame
   (tick_interval from the current speed; alpha = 1 when paused), and exposes it to C++ renderer nodes
   (a C++ accessor) and to GDScript (get_interpolation_alpha()).
2. A generic extension point: RenderSnapshot gains a named-channel container, e.g.
   std::vector<SnapshotChannel> where a channel is {name, std::vector<float> data, uint32 stride}, filled
   by publish hooks. C++ renderers look up channels by name; GDScript can fetch a channel as
   PackedFloat32Array (for debugging only).
3. A unit test in sim/tests for channel publishing via a publish hook; extend smoke.gd to fetch a test
   channel published by a debug hook.

Run all tests and the smoke test. Follow the Task workflow in CLAUDE.md; last task of S0.6.2, so open the PR.
```

---

## F0.7 — Camera, Input & Debug Tools

### S0.7.1 — City-builder camera
> *As a player, I can pan, rotate, zoom and tilt a smooth camera like in Cities: Skylines, so I can look at my city comfortably from any angle.*

**Acceptance criteria**
- WASD/arrow keys and edge-pan (toggleable).
- Middle-mouse or Q/E rotate.
- Wheel zoom with a tilt curve (steeper when far, more horizontal when near).
- Smoothing, map bounds, and settings in a Resource.
- Verified on the PC.

#### [ ] T0.7.1.1 — Camera rig
- **Model:** Sonnet · **Session:** [PC] · **Depends on:** T0.6.2.2
```
Task T0.7.1.1 (Story S0.7.1). Read CLAUDE.md (GDScript style: static typing) and docs/architecture.md §11.

1. Input actions in project.godot: cam_forward/back/left/right (WASD + arrows), cam_rotate_left/right
   (Q/E), cam_rotate_drag (middle mouse), cam_zoom_in/out (wheel), cam_fast (Shift).
2. game/scenes/camera/city_camera.tscn + scripts/camera/city_camera.gd (class_name CityCamera):
   pivot Node3D at ground level → yaw → pitch → Camera3D on an arm. Pan relative to yaw, speed scales
   with zoom distance; edge-pan when the mouse is within N px of the window edge (toggle); rotation by
   keys and middle-drag; zoom distance from min to max with pitch from a Curve (e.g., 25° near → 70° far);
   exponential smoothing on all motion; clamp the pivot to a configurable Rect2 bounds (meters).
3. game/scripts/camera/camera_settings.gd (Resource) with all tunables (speeds, smoothing, edge size,
   zoom limits, pitch curve, invert options) and a default .tres.
4. Add the camera to main.tscn with a temporary 2 km × 2 km ground plane (a PlaneMesh with a grid shader
   or a checker texture made in code — no binary assets) and a DirectionalLight3D + WorldEnvironment.

Run the game on this PC and tune the defaults until movement feels like a modern city builder. In your
final message, list the controls and what the user should try. Follow the Task workflow in CLAUDE.md
(branch story/S0.7.1-camera); open the PR.
```

### S0.7.2 — Debug overlay, speed controls and dev console
> *As a developer and player, I see performance and simulation stats, control game speed, and run console commands, so I can test and diagnose the game.*

**Acceptance criteria**
- F3 toggles the overlay.
- Speed buttons and hotkeys (Space, 1–3).
- The `~` console with a command registry and history.
- All UI text uses loc keys.

#### [ ] T0.7.2.1 — Debug overlay and speed controls
- **Model:** Sonnet · **Session:** [PC] · **Depends on:** T0.7.1.1
```
Task T0.7.2.1 (Story S0.7.2). Read CLAUDE.md and docs/architecture.md §9, §11.

1. game/ui/debug/debug_overlay.tscn (+ .gd): a CanvasLayer panel (top-left), toggled by F3 (input action
   debug_overlay), updated 4×/second from Sim.get_stats() and Engine.get_frames_per_second(): FPS,
   sim TPS, tick ms (avg/last), entity count, speed/paused, date, time of day, Godot static memory and
   video memory (Performance monitors).
2. game/ui/hud/time_controls.tscn (+ .gd): top-right date/time label + buttons Pause, 1×, 2×, 4× with the
   active state highlighted; hotkeys Space (toggle pause), 1/2/3 (speeds) via input actions; buttons and
   hotkeys send commands through Sim.submit_command.
3. All visible text uses loc keys via tr(); add the keys to game/content/base/loc/en/ui.json (e.g.,
   ui.debug.fps, ui.time.pause). Labels show values via tr("key").format({...}).
4. Add both to main.tscn.

Run the game on this PC; verify speed changes are reflected in TPS and the date advances at the Ancient
cadence (~1 year per 2 minutes at 1×). Report what you verified. Follow the Task workflow in CLAUDE.md
(branch story/S0.7.2-debug-ui).
```

#### [ ] T0.7.2.2 — Developer console
- **Model:** Sonnet · **Session:** [PC] · **Depends on:** T0.7.2.1
```
Task T0.7.2.2 (Story S0.7.2, last task). Read CLAUDE.md.

1. game/ui/debug/dev_console.tscn (+ .gd): toggled by `~` (input action dev_console); a LineEdit input,
   a scrolling RichTextLabel output, command history (up/down), and tab-completion of command names.
   Disabled in release exports unless a project setting allows it.
2. game/scripts/debug/console_commands.gd (autoload "Console"): register(name, callable, help_text,
   arg_spec) with simple typed-argument parsing (int, float, string). Built-ins: help, clear,
   speed <0-3>, pause, save <name>, load <name> (saves under user://saves/), hash (asks the sim for a
   state hash: add get_state_hash() to CitySim; it must be computed on the sim thread between ticks —
   implement it as a command whose result returns via a UiEvent), era <id>, quit.
3. Console help text is developer-facing and may stay in English (document that exception in CLAUDE.md
   under Hard rules #4).

Run the game, exercise every command, and report results (including save → load → hash equality).
Follow the Task workflow in CLAUDE.md; last task of S0.7.2, so open the PR.
```

---

## F0.8 — Walking Skeleton

### S0.8.1 — 10,000 agents walking on a test map
> *As a developer, I can spawn 10,000 debug agents that walk between random points on a test map at ≥ 60 FPS, so the whole architecture (simulation threads, jobs, snapshots, rendering, saves) is proven before real gameplay is built.*

**Acceptance criteria**
- Grid A* pathfinding with deterministic tie-breaking.
- Movement uses `parallel_for`.
- Agents render through a C++ MultiMesh renderer with interpolation.
- Save and load mid-walk preserve hashes.
- `docs/perf.md` records benchmarks (headless in the cloud as indicative, PC as authoritative).

#### [ ] T0.8.1.1 — Test map, grid pathfinding, wandering agents (core)
- **Model:** Sonnet · **Session:** [Cloud] · **Depends on:** T0.6.2.2
```
Task T0.8.1.1 (Story S0.8.1). Read docs/architecture.md §4, §5, §6, §7.2.

Implement in sim/src/debug/ (namespace citysim::debug) — this is throwaway-quality *gameplay* but
production-quality *engineering* (deterministic, tested, uses the job system):
1. Singleton TestMap: width, height (cells), cell_size meters (default 4 m), walkable bitset; generator
   make_test_map(seed, w, h) placing random rectangular obstacles via citysim::rng. Register it for hashing
   and serialization.
2. Grid A* (8-neighbour, octile heuristic, no corner cutting) with deterministic tie-breaking (f, then h,
   then cell index); returns waypoints as cell centers; bounded node budget per search.
3. Components: Position{float x, z}, Heading{float yaw}, PathFollower{std::vector<uint32> cells; uint32
   next; float speed_mps}, DebugWanderer{uint32 trips}. Register them all (hash + serialize).
4. Systems:
   - WanderPlannerSystem (Update): agents without a path pick a random walkable destination via
     rng.at(stream("debug.wander"), entity, trips) and compute a path; to bound cost, at most N path
     searches per tick (default 200), in ascending entity order.
   - MovementSystem (Update): parallel_for over agents (chunk size 1024) advancing along the path by
     speed × movement_seconds_per_tick (from GameClock); updates Position and Heading.
5. Command SpawnDebugAgents{count} spawning agents at random walkable cells; command ClearDebugAgents.
6. Tests: A* correctness on small maps (including unreachable targets); movement identical with 1 and 8
   workers (hash compare over 500 ticks with 2,000 agents); save/load continuation equality with agents.

Run all tests. Follow the Task workflow in CLAUDE.md (branch story/S0.8.1-walking-skeleton).
```

#### [ ] T0.8.1.2 — Agent snapshot channel and headless benchmark
- **Model:** Sonnet · **Session:** [Cloud] · **Depends on:** T0.8.1.1
```
Task T0.8.1.2 (Story S0.8.1). Read docs/architecture.md §7.3, §13, §14.

1. A publish hook that writes a snapshot channel "agents.xform" with stride 4 floats per agent
   (x, y, z, yaw), y = 0 for now; agents in ascending entity order; also a channel "agents.ids" (entity
   ids as floats are lossy — instead add an integer channel type or a parallel std::vector<uint32> to
   SnapshotChannel; choose and document) so renderers can match agents between snapshots for interpolation.
2. citysim_headless --bench "agents=10000,ticks=2000,threads=0,map=512": creates a test map, spawns
   agents, runs ticks as fast as possible (no pacing) and prints ms/tick (avg, p50, p95, max), path
   searches per tick, and ticks/second. Wall-clock timing is allowed in the tool (not in sim/src).
3. Run benchmarks for 10k and 50k agents at threads=1 and threads=0 in this cloud session; create
   docs/perf.md with a table (date, commit, machine = "cloud container (indicative)", results) and the
   architecture §14 budgets for reference.

Run all tests. Follow the Task workflow in CLAUDE.md (same story branch).
```

#### [ ] T0.8.1.3 — AgentRenderer (MultiMesh) and test map in Godot
- **Model:** Sonnet · **Session:** [PC] · **Depends on:** T0.8.1.2, T0.7.2.2
```
Task T0.8.1.3 (Story S0.8.1). Read docs/architecture.md §7.3, §11.

1. gdext: class AgentRenderer : public MultiMeshInstance3D. Each frame: get the previous and current
   snapshots and alpha from CitySim's SnapshotView; for agents present in both (match by id), interpolate
   position (lerp) and yaw (shortest-arc); write the instance transforms directly into the MultiMesh
   buffer (PackedFloat32Array, 12 floats per instance, set_buffer) — no per-agent Godot API calls;
   grow instance_count with headroom to avoid reallocating every frame; use visible_instance_count.
   Mesh: a simple capsule (CapsuleMesh, ~1.8 m tall) with an unshaded or simple material.
2. gdext: TestMapView node that builds a ground mesh from the TestMap (publish the walkable grid once via
   a "testmap.grid" channel or a dedicated CitySim getter) — walkable cells in one color, obstacles as
   extruded boxes in a single MultiMesh.
3. Console commands: spawn_agents <n>, clear_agents, new_test_map <seed> <size>. Set the camera bounds to
   the map size.
4. Add both nodes to main.tscn.

Run on this PC: spawn 1,000 then 10,000 agents; confirm smooth interpolated motion at 1×, 2×, 4× and
when paused. Report FPS from the debug overlay. Follow the Task workflow in CLAUDE.md (same story branch).
```

#### [ ] T0.8.1.4 — End-to-end verification and PC performance baseline
- **Model:** Sonnet · **Session:** [PC] · **Depends on:** T0.8.1.3
```
Task T0.8.1.4 (Story S0.8.1, last task). Read docs/architecture.md §13–§14 and docs/perf.md.

1. Release builds: build windows-release (core + gdext). Run the headless benchmarks from T0.8.1.2 on this
   PC and add rows to docs/perf.md (machine: GTX 1660 Ti / CPU model / 64 GB).
2. In-game (release export template or editor run with the release gdext): measure FPS at 1080p with
   10,000 and 50,000 agents at 1× and 4× (camera zoomed out over the whole map and zoomed in), and
   the sim tick ms from the overlay. Add the results to docs/perf.md.
3. Save mid-walk via the console, note the hash, load, compare hashes; run the same seed+commands
   without save/load and confirm identical hashes at the same tick.
4. If any architecture §14 budget is missed, profile (Tracy build if needed), write the top 3 bottlenecks
   with evidence into docs/perf.md, and propose fixes as new backlog tasks in a "Follow-ups" list at the
   end of this epic file. Do not start large optimizations in this task.

Report verified results honestly, including failures. Follow the Task workflow in CLAUDE.md; last task of
S0.8.1, so open the PR.
```

### S0.8.2 — Time-scale spike
> *As the designer, I can try different movement and day-length settings and get a written recommendation, so the travel-time vs. day-length trade-off (architecture §6) is settled before the Citizens epic.*

**Acceptance criteria**
- `docs/spikes/time-scale.md` compares at least 3 settings, with measurements and a recommendation for the designer to approve.

#### [ ] T0.8.2.1 — Time-scale spike
- **Model:** Sonnet · **Session:** [PC] · **Depends on:** T0.8.1.4
```
Task T0.8.2.1 (Story S0.8.2). Read docs/architecture.md §6 and docs/gdd.md §3.3 (time cadence) and §10.11
(daily life).

This is a spike: produce evidence and a recommendation, not production features.
1. Add console commands to change movement_scale and day_length_ticks at runtime (as commands, so they
   stay deterministic and are recorded).
2. On the test map, for at least 3 configurations (e.g., movement_scale 1/3/6 with day_length 7,200 /
   14,400 / 28,800 ticks), measure: the visual walking speed in m/s (screen-perceived), how many in-game
   hours a 1 km and a 3 km walk takes, and how a citizen schedule (8 h work, 8 h sleep, 2 h errands)
   would fit. Use the debug overlay plus a small temporary measurement script.
3. Write docs/spikes/time-scale.md: the problem, the configurations, the measurements, options for
   trips that don't fit (fast-forwarding unobserved agents, schedule blocks, faster vehicles), and a
   recommended default with reasoning. Mark it "Pending designer approval".

Follow the Task workflow in CLAUDE.md (branch story/S0.8.2-time-spike); open the PR and ask the user to
review the recommendation.
```

---

## Follow-ups
_Filled in by T0.8.1.4 and later reviews._
