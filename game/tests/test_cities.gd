extends SceneTree
## Headless test of founding cities through the Godot bridge: the found_city and set_active_city commands, the
## sim_notice signal with its reasons, and the cities in the snapshot. It builds a small synthetic map with
## citysim_mapgen, so it needs that tool: set CITYSIM_MAPGEN to its path (without it the test prints SKIP and
## succeeds, so it cannot fail on a machine that has not built the tool).
## Run: CITYSIM_MAPGEN=build/linux-debug/sim/tools/mapgen/citysim_mapgen \
##      tools/godot.sh --headless --path game -s res://tests/test_cities.gd
## Exits 0 on success and 1 on any failure. Nodes are typed as Node because in `-s` mode this script compiles
## before the autoloads exist.

const MAP_ID: String = "test:map.synthetic"

var _failures: int = 0
var _sim: Node
var _notices: Array[Dictionary] = []
var _root_dir: String = ""


func _initialize() -> void:
	_run()


func _run() -> void:
	await process_frame
	var mapgen: String = OS.get_environment("CITYSIM_MAPGEN")
	if mapgen.is_empty():
		print("SKIP: CITYSIM_MAPGEN is not set (the path of the citysim_mapgen tool)")
		quit(0)
		return
	_sim = root.get_node_or_null("Sim")
	if _sim == null:
		_check(false, "the Sim autoload is there")
		quit(1)
		return

	_root_dir = OS.get_user_data_dir().path_join("test_cities_%d" % OS.get_process_id())
	if not _build_content(mapgen):
		quit(1)
		return

	_sim.sim_notice.connect(
		func(tag: String, key: String, text: String, subject: int) -> void:
			_notices.append({"tag": tag, "key": key, "text": text, "subject": subject})
	)
	var content_roots: PackedStringArray = [_root_dir, ProjectSettings.globalize_path("res://content")]
	var result: Dictionary = _sim.new_game(1, "base:era.ancient", content_roots, ["base", "test"], 0, MAP_ID)
	_check(bool(result["ok"]), "a game with a map starts (%s)" % str(result["errors"]))
	_check((_sim.get_cities() as Array).is_empty(), "there are no cities at first")
	_check(_sim.get_active_city_id() == 0, "and no active city")

	# A site with no detail terrain is refused, with the reason and its English text.
	_check(_sim.found_city(0, 0), "found_city is accepted for queueing")
	await _wait_for(func() -> bool: return _notices.size() >= 1)
	_check(_notices.size() == 1 and _notices[0]["tag"] == "city_found_failed", "a site off the detail terrain is refused")
	_check(_notices[0]["key"] == "city.found.error.no_detail", "...with the reason's key")
	_check(not (_notices[0]["text"] as String).is_empty(), "...and its text: " + str(_notices[0]["text"]))
	_check((_sim.get_cities() as Array).is_empty(), "nothing was founded")

	# The one site that fits the small map is founded and becomes the active city.
	_notices.clear()
	_check(_sim.found_city(2, 3, "Only"), "found_city with a name")
	await _wait_for(func() -> bool: return not (_sim.get_cities() as Array).is_empty())
	var cities: Array = _sim.get_cities()
	_check(cities.size() == 1, "one city exists")
	var city: Dictionary = cities[0]
	_check(
		city["id"] == 1 and city["name"] == "Only" and city["origin_tx"] == 2 and city["origin_ty"] == 3,
		"it has its id, name and area: " + str(city),
	)
	_check(bool(city["active"]) and _sim.get_active_city_id() == 1, "the first city is the active one")
	var tags: PackedStringArray = []
	for notice: Dictionary in _notices:
		tags.append(str(notice["tag"]))
	_check("city_founded" in tags and "active_city_changed" in tags, "the notices say so: " + str(tags))

	# The same site again overlaps; an unknown active city is refused.
	_notices.clear()
	_sim.found_city(2, 3)
	_sim.set_active_city(99)
	await _wait_for(func() -> bool: return _notices.size() >= 2)
	var keys: PackedStringArray = []
	for notice: Dictionary in _notices:
		keys.append(str(notice["key"]))
	_check("city.found.error.overlap" in keys, "an overlapping site is refused")
	_check("city.active.error.unknown" in keys, "an unknown city cannot be made active")

	# Bad dictionaries are refused in C++ before they reach the simulation.
	_check(not _sim.submit_command({"type": "found_city"}), "found_city needs its origin")
	_check(not _sim.submit_command({"type": "found_city", "origin_tx": 1, "origin_ty": 2, "colour": 3}), "an unknown field is refused")
	_check(not _sim.submit_command({"type": "found_city", "origin_tx": 1, "origin_ty": 2, "name": 5}), "a name must be text")
	_check(not _sim.submit_command({"type": "set_active_city", "city_id": 0}), "city ids start at 1")

	_sim.shutdown()
	DirAccess.remove_absolute(_root_dir)  # best effort; the folder is removed below file by file
	_remove_tree(_root_dir)
	print("test_cities: %s" % ("PASS" if _failures == 0 else "%d FAILED" % _failures))
	quit(0 if _failures == 0 else 1)


func _build_content(mapgen: String) -> bool:
	var package_dir: String = _root_dir.path_join("test/maps/synthetic")
	DirAccess.make_dir_recursive_absolute(package_dir)
	var output: Array = []
	var code: int = OS.execute(mapgen, ["--synthetic", "--out", package_dir, "--size-km", "16"], output, true)
	_check(code == 0, "citysim_mapgen runs: " + str(output))
	if code != 0:
		return false
	var mod_dir: String = _root_dir.path_join("test")
	_write(mod_dir.path_join("mod.json"), '{ "id": "test", "name_key": "mod.test.name", "version": "1.0.0", "depends_on": ["base"], "enabled_by_default": false }')
	_write(mod_dir.path_join("defs/maps.json"), FileAccess.get_file_as_string(package_dir.path_join("map_def.json")))
	_write(mod_dir.path_join("loc/en/maps.json"), '{ "mod.test.name": "Test", "map.synthetic.name": "Synthetic", "map.synthetic.desc": "A test map." }')
	return true


func _write(path: String, text: String) -> void:
	DirAccess.make_dir_recursive_absolute(path.get_base_dir())
	var file: FileAccess = FileAccess.open(path, FileAccess.WRITE)
	file.store_string(text)


func _wait_for(condition: Callable) -> void:
	for _i: int in 60:
		if condition.call():
			return
		await create_timer(0.05).timeout


func _remove_tree(path: String) -> void:
	for entry: String in DirAccess.get_directories_at(path):
		_remove_tree(path.path_join(entry))
	for entry: String in DirAccess.get_files_at(path):
		DirAccess.remove_absolute(path.path_join(entry))
	DirAccess.remove_absolute(path)


func _check(condition: bool, message: String) -> void:
	if condition:
		print("ok   ", message)
	else:
		_failures += 1
		printerr("FAIL ", message)
