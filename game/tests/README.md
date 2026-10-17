# game/tests

Headless Godot tests. `smoke.gd` checks that the GDExtension loads and the C++ core answers:

    tools/godot.sh --headless --path game -s res://tests/smoke.gd

The first run in a fresh checkout needs `--import` once (CI does this):

    tools/godot.sh --headless --path game --import
