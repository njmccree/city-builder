extends SceneTree
## Measurement script for the time-scale spike (S0.8.2, docs/spikes/time-scale.md). Not a test: it runs the game
## windowed, sets each configuration with the `time_scale` console command, and prints what it measures.
## Run (needs a GPU window, and a Release extension for a fair frame rate):
##   godot --path game -s res://tests/spikes/measure_time_scale.gd
## Per configuration, at 1x, it measures:
##   - walk m/s: the mean straight-line distance agents cover per real second (from snapshot positions),
##   - screen px/s: how fast agents cross the screen with the camera 60 m above the middle of the map,
##   - clock min/s: how many minutes the in-game clock face advances per real second, and from that the length
##     of a day in real seconds,
##   - h/km: how many in-game hours it takes to walk 1 km at that pace.
## Times are real seconds from the engine clock; tick counts from the simulation confirm the pace was 20 ticks/s.

## [movement_scale, day_length_ticks]
const CONFIGS: Array = [[1.0, 7200], [3.0, 14400], [6.0, 28800], [6.0, 14400], [12.0, 14400]]
const AGENTS: int = 1500
const SPEED_WINDOW: float = 3.0
const CLOCK_WINDOW: float = 12.0

var _sim: Node
var _console: Node
var _camera_node: Node


func _initialize() -> void:
	_run()


func _minutes(time_text: String) -> int:
	var parts: PackedStringArray = time_text.split(":")
	return int(parts[0]) * 60 + int(parts[1])


func _now() -> float:
	return Time.get_ticks_msec() / 1000.0


func _run() -> void:
	await process_frame
	var main_scene: Node = (load("res://scenes/main.tscn") as PackedScene).instantiate()
	root.add_child(main_scene)
	_sim = root.get_node("Sim")
	_console = root.get_node("Console")
	_camera_node = main_scene.get_node("CityCamera")
	await create_timer(1.0).timeout
	_console.execute("new_test_map 3 128")
	await create_timer(1.5).timeout
	_console.execute("spawn_agents %d" % AGENTS)
	_console.execute("speed 1")
	_camera_node.set_zoom_distance(60.0)
	_camera_node.focus_on(Vector3(256.0, 0.0, 256.0))
	await create_timer(12.0).timeout # every agent gets its first path
	var cam: Camera3D = _camera_node.get_camera()
	print("RESULT scale | day ticks | walk m/s | screen px/s | ticks/s | clock min/s | real s per day | h per km")
	for config: Array in CONFIGS:
		var scale: float = config[0]
		var day_ticks: int = config[1]
		_console.execute("time_scale %s %d" % [str(scale), day_ticks])
		await create_timer(1.0).timeout
		# Measure only a steady 20 ticks per second: a simulation that is catching up after a stall runs faster.
		var waited: float = 0.0
		while absf(float(_sim.get_stats()["tps"]) - 20.0) > 0.5 and waited < 30.0:
			await create_timer(0.5).timeout
			waited += 0.5

		# Start of the window.
		var start_stats: Dictionary = _sim.get_stats()
		var start_time: float = _now()
		var start_positions: PackedFloat32Array = _sim.core.get_channel("agents.xform")
		var start_screen: Dictionary = {}
		for i: int in start_positions.size() / 4:
			var p: Vector3 = Vector3(start_positions[i * 4], 0.9, start_positions[i * 4 + 2])
			if cam.is_position_in_frustum(p):
				start_screen[i] = cam.unproject_position(p)
		var start_clock: int = _minutes(start_stats["time_text"])

		# Speed window.
		await create_timer(SPEED_WINDOW).timeout
		var mid_stats: Dictionary = _sim.get_stats()
		var mid_time: float = _now()
		var mid_positions: PackedFloat32Array = _sim.core.get_channel("agents.xform")
		var walk_sum: float = 0.0
		var walk_count: int = 0
		var screen_sum: float = 0.0
		var screen_count: int = 0
		for i: int in mini(start_positions.size(), mid_positions.size()) / 4:
			var distance: float = Vector2(mid_positions[i * 4] - start_positions[i * 4], mid_positions[i * 4 + 2] - start_positions[i * 4 + 2]).length()
			if distance > 0.01:
				walk_sum += distance
				walk_count += 1
				if start_screen.has(i):
					var p_end: Vector3 = Vector3(mid_positions[i * 4], 0.9, mid_positions[i * 4 + 2])
					if cam.is_position_in_frustum(p_end):
						screen_sum += (cam.unproject_position(p_end) - (start_screen[i] as Vector2)).length()
						screen_count += 1
		var speed_seconds: float = mid_time - start_time
		var walk_mps: float = walk_sum / maxf(walk_count, 1) / speed_seconds
		var screen_pps: float = screen_sum / maxf(screen_count, 1) / speed_seconds

		# Clock window (the rest of it).
		await create_timer(CLOCK_WINDOW - SPEED_WINDOW).timeout
		var end_stats: Dictionary = _sim.get_stats()
		var end_time: float = _now()
		var clock_seconds: float = end_time - start_time
		var minutes: int = (_minutes(end_stats["time_text"]) - start_clock + 1440) % 1440
		var minutes_per_second: float = float(minutes) / clock_seconds
		var ticks_per_second: float = float(int(end_stats["tick"]) - int(start_stats["tick"])) / clock_seconds
		var day_seconds: float = 1440.0 / minutes_per_second
		var hours_per_km: float = (1000.0 / walk_mps) * minutes_per_second / 60.0
		print("RESULT %s | %d | %.2f (%d agents) | %.1f (%d agents) | %.1f | %.2f | %.0f | %.2f" % [
			str(scale), day_ticks, walk_mps, walk_count, screen_pps, screen_count, ticks_per_second,
			minutes_per_second, day_seconds, hours_per_km])
	quit(0)
