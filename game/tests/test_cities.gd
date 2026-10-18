extends SceneTree
## Headless test of founding cities through the Godot bridge (and of the set_river_level debug command): the
## found_city and set_active_city commands, the
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
	var result: Dictionary = _sim.new_game(1, "base:era.ancient", content_roots, ["base", "test"], 0, MAP_ID, true)
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

	# Debug agents walk on the real terrain of the active city: spawned on its walkable ground, published with
	# their height. (The walk grid is built in the tick after the city was founded, so this waits a little.)
	await _wait_for(func() -> bool: return _sim.get_active_city_id() == 1)
	_check(_sim.submit_command({"type": "spawn_agents", "count": 40}), "spawn_agents is accepted in a world game")
	await _wait_for(func() -> bool: return _sim.core.get_channel("agents.xform").size() >= 4 * 40)
	var agents: PackedFloat32Array = _sim.core.get_channel("agents.xform")
	_check(agents.size() == 4 * 40, "40 agents are published with x, y, z and yaw: %d floats" % agents.size())
	var above_ground: int = 0
	for i: int in range(0, agents.size(), 4):
		above_ground += 1 if agents[i + 1] > 0.5 else 0
	_check(above_ground > 0, "and they stand on the terrain, not at y = 0: %d of 40 are above 0.5 m" % above_ground)

	# The river level debug command: an unknown reach is refused with its reason, a known one is accepted.
	_notices.clear()
	_check(_sim.set_river_level(999, 1.0), "set_river_level is accepted for queueing")
	await _wait_for(func() -> bool: return not _notices.is_empty())
	_check(
		not _notices.is_empty() and _notices[0]["tag"] == "river_level_failed"
		and _notices[0]["key"] == "water.river_level.error.unknown_reach",
		"an unknown reach is refused: " + str(_notices),
	)
	_check(not (_notices[0]["text"] as String).is_empty(), "...with its text: " + str(_notices[0]["text"]))
	_check(_sim.set_river_level(0, 0.0), "a known reach is accepted")
	_check(not _sim.submit_command({"type": "set_river_level", "reach": 0}), "set_river_level needs its level")
	_check(not _sim.submit_command({"type": "set_river_level", "reach": -1, "offset_m": 1.0}), "reach indices start at 0")
	_check(not _sim.submit_command({"type": "set_river_level", "reach": 0, "offset_m": 5000.0}), "an absurd level is refused")

	# Terraforming: a brush on the active city is accepted and reported; a bad one is refused with its reason.
	_notices.clear()
	_check(_sim.terraform(1, "raise", 4000.0, 4000.0, 30.0, 1.0), "terraform is accepted for queueing")
	await _wait_for(func() -> bool: return not _notices.is_empty())
	_check(not _notices.is_empty() and _notices[0]["tag"] == "terraform_applied", "the stroke is reported: " + str(_notices))
	var last: PackedFloat32Array = _sim.core.get_channel("world.terraform.last")
	_check(last.size() == 5 and last[0] == 1.0 and last[2] > 0.0 and last[3] > 0.0, "with its volume and cost: " + str(last))
	_notices.clear()
	_sim.terraform(1, "raise", 4000.0, 4000.0, 1.0, 1.0)
	await _wait_for(func() -> bool: return not _notices.is_empty())
	_check(not _notices.is_empty() and _notices[0]["key"] == "terraform.error.parameters", "a radius of 1 m is refused: " + str(_notices))
	_check(not _sim.submit_command({"type": "terraform", "city_id": 1, "op": "melt", "x": 0, "z": 0, "radius_m": 10, "strength": 1}), "an unknown op is refused")
	_check(not _sim.submit_command({"type": "terraform", "city_id": 0, "op": "raise", "x": 0, "z": 0, "radius_m": 10, "strength": 1}), "city ids start at 1")
	_check(not _sim.submit_command({"type": "terraform", "city_id": 1, "op": "raise", "x": 0, "z": 0, "radius_m": 10}), "strength is needed")

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
