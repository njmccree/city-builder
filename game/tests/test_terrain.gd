extends SceneTree
## Headless test of the TerrainRenderer: on a small synthetic map (built with citysim_mapgen, so it needs that
## tool: set CITYSIM_MAPGEN to its path; without it the test prints SKIP and succeeds) a city is founded, and
## the renderer must show its 36 tiles (4 x 4 plus the ring) and a horizon, pick the ground under the screen's
## center, answer heights, and follow a terrain edit. Headless Godot draws nothing, but meshes are still made
## through the RenderingServer, so the whole path runs.
## Run: CITYSIM_MAPGEN=build/linux-debug/sim/tools/mapgen/citysim_mapgen \
##      tools/godot.sh --headless --path game -s res://tests/test_terrain.gd
## Exits 0 on success and 1 on any failure. Nodes are typed as Node because in `-s` mode this script compiles
## before the autoloads exist.

const MAP_ID: String = "test:map.synthetic"

var _failures: int = 0
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
	var sim: Node = root.get_node_or_null("Sim")
	_root_dir = OS.get_user_data_dir().path_join("test_terrain_%d" % OS.get_process_id())
	var output: Array = []
	var code: int = OS.execute(mapgen, ["--synthetic", "--mod", _root_dir, "--size-km", "16"], output, true)
	_check(code == 0, "citysim_mapgen --mod runs: " + str(output))
	if code != 0 or sim == null:
		quit(1)
		return

	# A camera above the middle of the city looking down at it, and the renderer.
	var camera: Camera3D = Camera3D.new()
	camera.far = 60000.0
	root.add_child(camera)
	camera.look_at_from_position(Vector3(4096.0, 3000.0, 4096.0 - 1500.0), Vector3(4096.0, 0.0, 4096.0))
	camera.make_current()
	var terrain: Node3D = ClassDB.instantiate("TerrainRenderer")
	root.add_child(terrain)
	var changes: Array[int] = []
	terrain.connect("terrain_changed", func(city_id: int, _tile: Vector2i) -> void: changes.append(city_id))

	var roots: PackedStringArray = [_root_dir, ProjectSettings.globalize_path("res://content")]
	var result: Dictionary = sim.new_game(1, "base:era.ancient", roots, ["base", "test"], 0, MAP_ID)
	_check(bool(result["ok"]), "a game on the synthetic map starts (%s)" % str(result["errors"]))
	_check(not bool(terrain.has_terrain()), "no terrain before a city exists")
	var info: Dictionary = sim.get_map_info()
	_check(info.get("map_id", "") == MAP_ID and int(info["tiles_x"]) == 8, "the map info is there: %s" % str(info.keys()))
	_check((info["detail_tiles"] as Array).size() == 16, "with its 16 detail tiles")
	var site: Vector2i = sim.find_city_site()
	_check(site == Vector2i(2, 3), "the only site that fits is found: %s" % str(site))
	sim.found_city(site.x, site.y, "Terrain")

	await _wait_for(func() -> bool: return bool(terrain.get_stats()["ready"]), 400)
	var stats: Dictionary = terrain.get_stats()
	_check(bool(stats["ready"]), "the terrain gets ready: %s" % str(stats))
	_check(changes == [1], "terrain_changed says which city: %s" % str(changes))
	_check(terrain.get_city_origin_tile() == site, "its local frame starts at the city's south-west tile")
	_check(int(stats["tiles"]) == 36, "36 tiles (4 x 4 and the ring) are shown: %d" % int(stats["tiles"]))
	_check(int(stats["chunks_drawn"]) >= 36, "at least one chunk per tile is drawn: %d" % int(stats["chunks_drawn"]))
	_check(int(stats["chunks_drawn"]) < 36 * 64, "and far fewer than every fine chunk: %d" % int(stats["chunks_drawn"]))
	_check(int(stats["horizon_triangles"]) > 0, "a horizon exists: %d triangles" % int(stats["horizon_triangles"]))

	# Picking: the ray through the screen's center lands near the point the camera looks at, on the ground.
	var center: Vector2 = root.get_visible_rect().size * 0.5
	var hit: Variant = terrain.pick_terrain(center)
	_check(hit is Vector3, "the ground under the screen's center is found: %s" % str(hit))
	if hit is Vector3:
		var point: Vector3 = hit
		_check(Vector2(point.x, point.z).distance_to(Vector2(4096.0, 4096.0)) < 400.0, "near where the camera looks: %s" % str(point))
		_check(absf(point.y - float(terrain.get_height_at(point.x, point.z))) < 0.05, "at the ground's height")
	_check(terrain.pick_terrain(Vector2(center.x, -100000.0)) == null, "a ray into the sky hits nothing")
	_check(not is_nan(float(terrain.get_height_at(-30000.0, 4000.0))), "heights outside the active tiles come from the map")

	# An edit on dry ground (rivers cannot be terraformed): the ground rises, and the renderer catches up.
	var spot: Vector2 = Vector2(-1.0, -1.0)
	for i: int in 64:
		var probe: Vector2 = Vector2(500.0 + (i % 8) * 1000.0, 500.0 + floori(i / 8.0) * 1000.0)
		if float(terrain.get_height_at(probe.x, probe.y)) > 3.0:
			spot = probe
			break
	_check(spot.x >= 0.0, "there is dry ground to edit: %s" % str(spot))
	var before: float = terrain.get_height_at(spot.x, spot.y)
	sim.terraform(1, "raise", spot.x, spot.y, 40.0, 1.0)
	await _wait_for(func() -> bool: return float(terrain.get_height_at(spot.x, spot.y)) > before + 0.1, 100)
	_check(float(terrain.get_height_at(spot.x, spot.y)) > before + 0.1, "the edit shows in the heights")
	await _wait_for(func() -> bool: return bool(terrain.get_stats()["ready"]), 200)
	_check(bool(terrain.get_stats()["ready"]), "and the changed chunks are built again")

	# A game without a city shows nothing.
	sim.shutdown()
	await _wait_for(func() -> bool: return not bool(terrain.has_terrain()), 50)
	_check(not bool(terrain.has_terrain()) and changes.back() == 0, "the terrain goes when the game does")

	terrain.queue_free()
	_remove_tree(_root_dir)
	print("test_terrain: %s" % ("PASS" if _failures == 0 else "%d FAILED" % _failures))
	quit(0 if _failures == 0 else 1)


func _wait_for(condition: Callable, frames: int) -> void:
	for _i: int in frames:
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
