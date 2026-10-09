# CLAUDE.md

A deep, multi-era city builder: a **Godot 4** front end on top of a **C++20 simulation core**.
- Design: `docs/gdd.md`
- Architecture (binding): `docs/architecture.md`
- Backlog and task prompts: `docs/backlog/`

Read the architecture sections that a Task cites before writing code.

## Repository layout
- `sim/`: C++20 simulation core library, tests, and CLI tools. **No Godot dependency.**
- `gdext/`: GDExtension bridge (C++). The only C++ code that includes Godot headers.
- `third_party/godot-cpp/`: git submodule.
- `game/`: Godot project (GDScript, scenes, assets). `game/content/` holds JSON data; the base game is the `base` mod.
- `tools/`: developer scripts.
- `docs/`: design, architecture, backlog, versions, perf notes.

## Hard rules
1. **Never include Godot headers in `sim/`.**
2. **Determinism (architecture §4):**
   - No `rand()`, `<random>`, `std::random_device`, or wall-clock time in simulation logic.
   - Use `citysim::rng`.
   - Don't let iteration over unordered containers affect state.
   - Every external change goes through a Command.
   - Every stateful component is registered in the `ComponentRegistry`.
3. **All content is data** (architecture §8). Don't hard-code content ids in C++ outside tests.
4. **All player-facing text uses localization keys** (architecture §9).
5. **Never commit build outputs** (`build/`, `game/bin/`, `game/.godot/`) or large binaries outside Git LFS.
6. **Never use `-ffast-math`.** Keep warnings clean.

## Code style
- C++:
  - `clang-format` using the repo's `.clang-format`
  - namespaces `citysim::<module>`
  - types `PascalCase`
  - functions and variables `snake_case`
  - members end in `_`
  - constants `k_snake_case`
  - headers use `#pragma once`
- One class per header/source pair. Public headers go in `sim/include/citysim/<module>/`, implementations in `sim/src/<module>/`, tests in `sim/tests/<module>/`.
- GDScript: follow the official Godot style guide, use static typing everywhere (`var x: int`), and `class_name` for reusable scripts.
- Comments explain *why*. Each system's header comment states which clock it runs on (architecture §6).

## Build & test
**Cloud sessions (Linux):**
```
cmake --preset linux-debug
cmake --build --preset linux-debug
ctest --preset linux-debug --output-on-failure
```
**PC (Windows, from a "Developer PowerShell for VS 2022"):**
```
cmake --preset windows-debug
cmake --build --preset windows-debug
ctest --preset windows-debug --output-on-failure
```
- Content validation: `build/<preset>/sim/tools/validate/citysim_validate game/content`
- Godot smoke test (once it exists): `godot --headless --path game -s res://tests/smoke.gd`

## Task workflow (backlog)
- Every Task has an id such as `T0.2.3.1`. Its Story, acceptance criteria and dependencies are in `docs/backlog/`.
- **Branch:** one per Story, `story/<StoryID>-<short-slug>` (e.g., `story/S0.2.3-scheduler`), created from `main`. If the session already has a designated branch, use that one.
- Before committing:
  - The build passes and **all** tests pass.
  - Changed C++ files are formatted with clang-format.
  - The Task's checkbox is ticked in its backlog file.
- **Commit message:** `<TaskID>: <summary>`.
- **Last Task of a Story:** open a PR titled `<StoryID>: <story title>`. The body lists the Tasks and confirms each Story acceptance criterion.
- If a Task conflicts with `docs/architecture.md` or is ambiguous, **stop and ask**. Don't invent architecture. If a deviation is agreed, update `docs/architecture.md` in the same PR.
- **Session tags:**
  - `[Cloud]` Tasks need only build and test.
  - `[PC]` Tasks need the Godot editor, a GPU, or Windows. Don't claim visual or manual verification you could not perform; say what the user should check instead.
