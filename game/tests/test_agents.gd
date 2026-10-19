extends SceneTree
## Headless test of the agent renderer and test-map view inside main.tscn: the map appears with obstacles,
## spawned agents are drawn (one instance each, written straight from the snapshot), clearing removes them,
## a new test map changes the camera bounds, and a save made mid-walk loads back to the same state hash.
## It cannot judge how smooth or good the result looks. Run:
##   tools/godot.sh --headless --path game -s res://tests/test_agents.gd
## Exits 0 on success and 1 on any failure. Nodes are typed as Node because in `-s` mode this script compiles
## before the autoloads exist.

var _failures: int = 0
var _sim: Node
var _console: Node
var _lines: PackedStringArray = []


func _initialize() -> void:
	_run()


func _run() -> void:
	await process_frame
	_sim = root.get_node("Sim")
	_console = root.get_node("Console")
	_console.line_printed.connect(func(text: String) -> void: _lines.append(text))

	var packed: PackedScene = load("res://scenes/main.tscn")
	var main_scene: Node = packed.instantiate()
	root.add_child(main_scene)
	await create_timer(1.0).timeout

	var map_view: Node = main_scene.get_node("TestMapView")
	var renderer: Node = main_scene.get_node("AgentRenderer")
	var camera: Node = main_scene.get_node("CityCamera")
	_check(map_view.get_map_size() == Vector2(1024.0, 1024.0), "the default test map is 256 cells of 4 m (%s)" % str(map_view.get_map_size()))
	_check(map_view.get_obstacle_count() > 50, "the map shows obstacles (%d)" % map_view.get_obstacle_count())
	_check(camera.settings.bounds.size == Vector2(1024.0, 1024.0), "the camera bounds follow the map size")
	_check(renderer.get_agent_count() == 0, "no agents at the start")

	# Agents appear.
	_console.execute("spawn_agents 1000")
	await create_timer(1.5).timeout
	var channel: PackedFloat32Array = _sim.core.get_channel("agents.xform")
	_check(channel.size() == 4000, "the snapshot has 1000 agents (%d floats)" % channel.size())
	_check(renderer.get_agent_count() == 1000, "the renderer draws 1000 (%d)" % renderer.get_agent_count())
	_check(renderer.multimesh.visible_instance_count == 1000, "the MultiMesh shows 1000 instances")
	_check(renderer.multimesh.instance_count >= 1000, "the MultiMesh has room for them")

	# Agents move, and stay on the map.
	var first_x: float = channel[0]
	var first_z: float = channel[2]
	await create_timer(1.5).timeout
	var later: PackedFloat32Array = _sim.core.get_channel("agents.xform")
	var moved: int = 0
	var inside: bool = true
	for i: int in 1000:
		if absf(later[i * 4] - channel[i * 4]) > 0.01 or absf(later[i * 4 + 2] - channel[i * 4 + 2]) > 0.01:
			moved += 1
		if later[i * 4] < 0.0 or later[i * 4] > 1024.0 or later[i * 4 + 2] < 0.0 or later[i * 4 + 2] > 1024.0:
			inside = false
	_check(moved > 900, "most agents have walked (%d of 1000 moved)" % moved)
	_check(inside, "every agent is inside the map")
	_check(first_x != later[0] or first_z != later[2], "the first agent moved")

	# The instance buffer holds the agents' transforms (positions in the last three floats of each row).
	var buffer: PackedFloat32Array = renderer.multimesh.buffer
	var good: bool = buffer.size() >= 12000
	for i: int in 1000:
		var origin_x: float = buffer[i * 12 + 3]
		var origin_y: float = buffer[i * 12 + 7]
		var origin_z: float = buffer[i * 12 + 11]
		# North is -z in the scene (Sim.to_view), so the map's instances have z from 0 down to -1024.
		if absf(origin_y - 0.9) > 0.001 or origin_x < -1.0 or origin_x > 1025.0 or origin_z < -1025.0 or origin_z > 1.0:
			good = false
	_check(good, "every instance sits on the ground inside the map")

	# More agents; the renderer grows with headroom.
	_console.execute("spawn_agents 9000")
	await create_timer(2.0).timeout
	_check(renderer.get_agent_count() == 10000, "10,000 agents are drawn (%d)" % renderer.get_agent_count())

	# Pause and speeds do not break drawing (interpolation alpha is 1 while paused).
	_console.execute("speed 0")
	await create_timer(0.5).timeout
	_check(renderer.get_agent_count() == 10000, "agents stay drawn while paused")
	_console.execute("speed 3")
	await create_timer(0.5).timeout

	# Save mid-walk, keep walking, load: the state hash comes back.
	_console.execute("speed 0")
	await create_timer(0.5).timeout
	_lines.clear()
	_console.execute("save agentstest")
	await create_timer(1.0).timeout
	_console.execute("hash")
	await create_timer(0.5).timeout
	var saved_hash: String = _hash_in_output()
	_console.execute("speed 3")
	await create_timer(1.5).timeout
	_lines.clear()
	_console.execute("load agentstest")
	await create_timer(1.5).timeout
	_console.execute("hash")
	await create_timer(0.5).timeout
	var loaded_hash: String = _hash_in_output()
	_check(saved_hash.length() == 16 and saved_hash == loaded_hash, "save mid-walk -> load -> same hash (%s vs %s)" % [saved_hash, loaded_hash])
	_check(renderer.get_agent_count() == 10000, "the loaded game still has 10,000 agents")

	# Clearing.
	_console.execute("clear_agents")
	await create_timer(1.0).timeout
	_check(renderer.get_agent_count() == 0, "clear_agents removes them")

	# A new map changes the bounds.
	_console.execute("new_test_map 7 64")
	await create_timer(1.5).timeout
	_check(map_view.get_map_size() == Vector2(256.0, 256.0), "the new map is 64 cells (%s)" % str(map_view.get_map_size()))
	_check(camera.settings.bounds.size == Vector2(256.0, 256.0), "the camera bounds shrank to the new map")
	_console.execute("new_test_map 7 5")
	_check(_lines[_lines.size() - 1].contains("between 16 and 1024"), "a silly map size is refused")

	DirAccess.remove_absolute(ProjectSettings.globalize_path("user://saves/agentstest.csav"))
	main_scene.queue_free()
	if _failures == 0:
		print("PASS: agents")
	quit(0 if _failures == 0 else 1)


func _hash_in_output() -> String:
	for index: int in range(_lines.size() - 1, -1, -1):
		if _lines[index].begins_with("state hash at tick"):
			return _lines[index].split(": ")[1]
	return ""


func _check(condition: bool, what: String) -> void:
	if condition:
		return
	_failures += 1
	printerr("FAIL: ", what)
