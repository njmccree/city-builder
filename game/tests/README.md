# game/tests

Headless Godot tests. `smoke.gd` checks that the GDExtension loads and the C++ core answers:

    tools/godot.sh --headless --path game -s res://tests/smoke.gd

A fresh checkout needs preparing once (CI does this). Do not use `godot --import` for it: Godot 4.7.2 aborts at
the end of the first scan that finds a GDExtension registering a class. `tools/godot_prepare.sh` lists the
extension first, which avoids that, and then imports the project:

    tools/godot_prepare.sh

`test_scene.gd` loads the temporary simulation test scene (`res://scenes/test_sim.tscn`), presses its keys, and checks
that pause, speed, era, save and load reach the simulation:

    tools/godot.sh --headless --path game -s res://tests/test_scene.gd

`test_cities.gd` and `test_terrain.gd` build a small synthetic map with `citysim_mapgen`, so they need its path in
`CITYSIM_MAPGEN` (without it they print SKIP). `test_terrain.gd` founds a city and checks that the `TerrainRenderer`
shows its 36 tiles and a horizon, picks the ground under the screen, and follows a terrain edit:

    CITYSIM_MAPGEN=build/linux-debug/sim/tools/mapgen/citysim_mapgen \
      tools/godot.sh --headless --path game -s res://tests/test_terrain.gd

`terrain_bench.gd` is not a CI test: it opens a window on the PC, starts the developer map (see `game/README.md`),
and prints the terrain's GPU time, triangles and memory for five camera views, with a screenshot of each
(`user://terrain_bench/`). The numbers go in `docs/perf.md`:

    <godot> --path game --resolution 1920x1080 -s res://tests/terrain_bench.gd

