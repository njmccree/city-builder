extends SceneTree
## Terrain performance and screenshots on the PC (T1.6.1.1; docs/perf.md). Not a CI test: it needs a window and
## a GPU, and the developer map (citysim_mapgen --synthetic --mod game/content_dev).
## Run (from the repository root, a release build of the extension in game/bin/):
##   <godot> --path game --resolution 1920x1080 -s res://tests/terrain_bench.gd
## For each camera view it waits until every wanted chunk is built, then averages 120 frames with the terrain
## shown and 120 with it hidden: the difference is the terrain's GPU cost. Screenshots go to
## user://terrain_bench/<view>.png; the numbers are printed.

const FRAMES: int = 120
## name, zoom distance (m), camera yaw (degrees)
const VIEWS: Array = [
	["close", 120.0, 30.0],
	["street", 400.0, 30.0],
	["district", 1500.0, 30.0],
	["city", 4500.0, 30.0],
	["overview", 9000.0, 30.0],
]

var _failures: int = 0


func _initialize() -> void:
	_run()


func _run() -> void:
	DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_DISABLED)
	var main: Node = (load("res://scenes/main.tscn") as PackedScene).instantiate()
	root.add_child(main)
	await process_frame
	var sim: Node = root.get_node("Sim")
	if not sim.has_dev_world():
		printerr("No dev map: run citysim_mapgen --synthetic --mod game/content_dev first")
		quit(1)
		return
	var result: Dictionary = sim.new_world(1, false)
	if not result["ok"]:
		printerr("new_world failed: ", result["errors"])
		quit(1)
		return
	var terrain: Node3D = main.get_node("TerrainRenderer")
	var camera: Node3D = main.get_node("CityCamera")
	await _wait_ready(terrain, 600)

	var viewport_rid: RID = root.get_viewport_rid()
	RenderingServer.viewport_set_measure_render_time(viewport_rid, true)
	var out_dir: String = "user://terrain_bench"
	DirAccess.make_dir_recursive_absolute(out_dir)
	print("resolution: ", root.get_visible_rect().size, "  GPU: ", RenderingServer.get_video_adapter_name())
	print("view      zoom_m  gpu_ms_all  gpu_ms_no_terrain  terrain_gpu_ms  cpu_frame_ms  fps   chunks  tris_terrain  tris_frame  draw_calls  vram_mb  chunk_meshes  mesh_mb")
	for view: Array in VIEWS:
		camera.focus_on(Vector3(4096.0, 0.0, 4096.0))
		camera.set_zoom_distance(float(view[1]))
		camera.snap()
		await _wait_ready(terrain, 600)
		var shown: Dictionary = await _measure(viewport_rid)
		var stats: Dictionary = terrain.get_stats()
		var image: Image = root.get_texture().get_image()
		image.save_png(out_dir.path_join("%s.png" % view[0]))
		terrain.visible = false
		var hidden: Dictionary = await _measure(viewport_rid)
		terrain.visible = true
		await process_frame
		print("%-8s %7.0f %11.2f %18.2f %15.2f %13.2f %5.0f %8d %13d %11d %11d %8.0f %13d %8.1f" % [
			view[0], view[1], shown["gpu"], hidden["gpu"], shown["gpu"] - hidden["gpu"], shown["cpu"],
			shown["fps"], int(stats["chunks_drawn"]), int(stats["triangles_drawn"]), int(shown["primitives"]),
			int(shown["draw_calls"]), shown["vram"] / 1048576.0, int(stats["chunk_meshes"]),
			float(stats["mesh_bytes"]) / 1048576.0])
	print("screenshots: ", ProjectSettings.globalize_path(out_dir))
	sim.shutdown()
	quit(0)


func _wait_ready(terrain: Node3D, frames: int) -> void:
	for _i: int in frames:
		await process_frame
		if bool(terrain.get_stats()["ready"]):
			# A few more frames so the last uploads are on screen.
			for _j: int in 10:
				await process_frame
			return
	printerr("the terrain did not get ready: ", terrain.get_stats())


func _measure(viewport_rid: RID) -> Dictionary:
	for _i: int in 10:
		await process_frame
	var gpu: float = 0.0
	var cpu: float = 0.0
	var start: int = Time.get_ticks_usec()
	for _i: int in FRAMES:
		await process_frame
		gpu += RenderingServer.viewport_get_measured_render_time_gpu(viewport_rid)
		cpu += RenderingServer.viewport_get_measured_render_time_cpu(viewport_rid)
	var seconds: float = float(Time.get_ticks_usec() - start) / 1000000.0
	return {
		"gpu": gpu / FRAMES,
		"cpu": cpu / FRAMES,
		"fps": FRAMES / seconds,
		"primitives": Performance.get_monitor(Performance.RENDER_TOTAL_PRIMITIVES_IN_FRAME),
		"draw_calls": Performance.get_monitor(Performance.RENDER_TOTAL_DRAW_CALLS_IN_FRAME),
		"vram": Performance.get_monitor(Performance.RENDER_VIDEO_MEM_USED),
	}
