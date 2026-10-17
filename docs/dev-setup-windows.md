# Windows development setup

Needed for `[PC]` Tasks. Cloud (Linux) sessions need none of this.
Pinned versions live in [versions.md](versions.md). The Godot version is still `TBD` there; use the latest stable Godot 4 until it is pinned, then switch to the pinned one.

## 1. Install the tools

Use `winget` or the installers; either works.

| Tool | Requirement | Notes |
|---|---|---|
| Git | recent | https://git-scm.com |
| Git LFS | 3.x | Run `git lfs install` once after installing. |
| Visual Studio 2022 Build Tools | Workload **Desktop development with C++** | Includes MSVC and the Windows SDK. Also includes CMake and Ninja. |
| CMake | >= 3.28 | EnTT v4 requires 3.28. The copy bundled with older VS 2022 builds (3.27) is too old; install a newer one. |
| Ninja | any recent | Same: the VS-bundled one works in the Developer PowerShell. |
| Python 3 | 3.8+ | Tick "Add python.exe to PATH". |
| Godot 4 editor | version in `versions.md` | Standard build, **not** .NET. Save it to a stable folder, e.g. `C:\Tools\Godot\`. |

Optional: VS Code with clangd (or the C/C++ extension) and the Godot Tools extension; clang-format (VS "C++ Clang tools" component or LLVM) for formatting C++ before commits.

```powershell
winget install Git.Git GitHub.GitLFS Python.Python.3.12 Kitware.CMake Ninja-build.Ninja
winget install Microsoft.VisualStudio.2022.BuildTools --override "--add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
git lfs install
```

Set `GODOT_EXE` to the editor path so scripts find it:

```powershell
[Environment]::SetEnvironmentVariable('GODOT_EXE', 'C:\Tools\Godot\Godot_v4.x-stable_win64.exe', 'User')
```

## 2. Open a Developer PowerShell

Build commands need `cl.exe` on `PATH`. Start menu → **Developer PowerShell for VS 2022**, then:

```powershell
cd path\to\city-builder
git submodule update --init --recursive
```

## 3. Check your environment

```powershell
powershell -ExecutionPolicy Bypass -File tools\check-env.ps1
```

It prints a PASS/FAIL/WARN table and exits 1 if a required tool is missing or too old. Godot, clang-format and VS Code are optional (WARN).

## 4. Build and test

```powershell
cmake --preset windows-debug
cmake --build --preset windows-debug
ctest --preset windows-debug --output-on-failure
```

Content validation:

```powershell
build\windows-debug\sim\tools\validate\citysim_validate.exe game\content
```

## 5. Running the game

Needs the Godot editor from `docs/versions.md` (4.7.2) and `GODOT_EXE` set (see step 1).

```powershell
git submodule update --init --recursive
cmake --preset windows-debug-gdext
cmake --build --preset windows-debug-gdext
```

This builds the GDExtension into `game\bin\` (`citysim_gdext.windows.editor.x86_64.dll`). Then:

1. Open the editor: `& $env:GODOT_EXE --editor --path game` (or Godot's Project Manager → Import → `game\project.godot`).
2. Check the **Output** panel: no errors about loading `citysim.gdextension`.
3. Press **F5**. The Output panel prints the CitySim core version.

First run on a fresh checkout: Godot 4.7.2 can abort at the end of its first scan when it discovers a GDExtension. Write `.godot\extension_list.cfg` containing `res://bin/citysim.gdextension` first (what `tools/godot_prepare.sh` does on Linux), or just start the editor a second time.

Headless smoke test (use the `_console.exe` build to see output):

```powershell
& "C:\Tools\Godot\Godot_v4.7.2-stable_win64_console.exe" --headless --path game -s res://tests/smoke.gd
```

It prints `PASS: CitySim core version ...` and exits 0.

## Troubleshooting

- **`cl.exe` not found / generator errors:** you are not in the Developer PowerShell.
- **Wrong CMake picked up:** run `Get-Command cmake`; it must be >= 3.28. The Developer PowerShell puts the VS-bundled CMake first, so install a newer CMake and re-order PATH if needed.
- **EnTT compile errors (`C2061`, `C2676` in `entt/...`):** your MSVC is too old. VS 2022 17.8 (MSVC 19.38) fails; 17.14 (MSVC 19.44) builds and passes all tests. Update via the Visual Studio Installer (needs about 19 GB free on C:).
- **Stale cache after changing presets:** delete `build\windows-debug` and reconfigure.
- **`LNK2038` RuntimeLibrary mismatch building the GDExtension:** the top-level `CMakeLists.txt` forces the DLL CRT for MSVC gdext builds; delete the build folder and reconfigure if you changed the CRT.
