extends SceneTree
## Headless smoke test: the GDExtension loads and the C++ core answers.
## Run: tools/godot.sh --headless --path game -s res://tests/smoke.gd
## Exits 0 on success and 1 on any failure.


func _initialize() -> void:
	if not ClassDB.class_exists("CitySim"):
		printerr("FAIL: class CitySim is not registered (is the extension built into game/bin/?)")
		quit(1)
		return

	var sim: CitySim = CitySim.new()
	var version: String = sim.get_core_version()
	sim.free()
	if version.is_empty():
		printerr("FAIL: get_core_version() returned an empty string")
		quit(1)
		return

	print("PASS: CitySim core version ", version)
	quit(0)
