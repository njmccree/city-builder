# game/tests

Headless Godot tests. `smoke.gd` checks that the GDExtension loads and the C++ core answers:

    tools/godot.sh --headless --path game -s res://tests/smoke.gd

A fresh checkout needs preparing once (CI does this). Do not use `godot --import` for it: Godot 4.7.2 aborts at
the end of the first scan that finds a GDExtension registering a class. `tools/godot_prepare.sh` lists the
extension first, which avoids that, and then imports the project:

    tools/godot_prepare.sh
