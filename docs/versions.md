# Pinned versions

| Component | Version/Tag | Why pinned | Updated |
|---|---|---|---|
| Godot | 4.7.2 | Latest stable 4.x when pinned. The editor used for the headless smoke test (`tools/godot.sh`); `game/bin/citysim.gdextension` sets `compatibility_minimum = "4.7"` | 2026-10-17 |
| godot-cpp | commit 2bb1acf (master, v10.0 line), `GODOTCPP_API_VERSION=4.7` | Submodule at `third_party/godot-cpp`. godot-cpp 10.x is versioned independently of Godot and has no release tags yet (the last `godot-4.x-stable` tag is 4.5), so its README says to pick the target with `api_version`; we pin a commit and bind the 4.7 API | 2026-10-17 |
| CMake (minimum) | 3.28 | EnTT v4 requires 3.28 (found building on Windows, PR #3); Architecture §3 | 2026-10-17 |
| C++ standard | C++20 | Architecture §3 | 2026-10-09 |
| EnTT | v4.0.0 | ECS; latest stable release | 2026-10-09 |
| Catch2 | v3.16.1 | Unit tests | 2026-10-09 |
| nlohmann/json | v3.12.0 | Content files | 2026-10-09 |
| spdlog | v1.17.0 | Logging | 2026-10-09 |
| zstd | v1.5.7 | Save compression (static lib only) | 2026-10-09 |
| xxHash | v0.8.4 | State and content hashing | 2026-10-09 |
| Tracy (optional, `CITYSIM_TRACY=ON`) | v0.14.1 | Profiler zones; only fetched when enabled | 2026-10-17 |
