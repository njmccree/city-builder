extends SceneTree
## Headless test of the city camera: its input actions exist, the camera scene loads, and panning, rotation,
## zoom limits, the pitch curve and the bounds clamp behave. It cannot judge how the camera *feels*.
## Run: tools/godot.sh --headless --path game -s res://tests/test_camera.gd
## Exits 0 on success and 1 on any failure.

const ACTIONS: PackedStringArray = [
	"cam_forward", "cam_back", "cam_left", "cam_right", "cam_rotate_left", "cam_rotate_right",
	"cam_rotate_drag", "cam_zoom_in", "cam_zoom_out", "cam_fast",
]

var _failures: int = 0


func _initialize() -> void:
	_run()


func _run() -> void:
	for action: String in ACTIONS:
		_check(InputMap.has_action(action), "input action %s exists" % action)
		if InputMap.has_action(action):
			_check(not InputMap.action_get_events(action).is_empty(), "input action %s has an event" % action)

	var packed: PackedScene = load("res://scenes/camera/city_camera.tscn")
	if packed == null:
		_check(false, "the camera scene loads")
		quit(1)
		return
	var camera: CityCamera = packed.instantiate()
	root.add_child(camera)
	# The node's @onready children are only ready once the tree has processed a frame.
	await process_frame
	var settings: CameraSettings = camera.settings
	_check(settings != null and settings.pitch_curve != null, "the default settings carry a pitch curve")
	settings.position_smoothing = 0.0
	settings.rotation_smoothing = 0.0
	settings.zoom_smoothing = 0.0

	camera.focus_on(Vector3(1000.0, 0.0, 1000.0))
	camera.snap()

	# Forward at yaw 0 is -Z in Godot, and the speed scales with the zoom distance.
	camera.set_zoom_distance(100.0)
	camera.step(0.1, Vector2(0.0, 1.0), 0.0)
	var moved: Vector3 = camera.get_target_pivot() - Vector3(1000.0, 0.0, 1000.0)
	_check(is_equal_approx(moved.z, -settings.pan_speed_per_distance * 100.0 * 0.1), "forward pans along -Z by speed x distance")
	_check(is_zero_approx(moved.x), "forward does not drift sideways")
	var near_step: float = absf(moved.z)
	camera.focus_on(Vector3(1000.0, 0.0, 1000.0))
	camera.set_zoom_distance(400.0)
	camera.step(0.1, Vector2(0.0, 1.0), 0.0)
	_check(absf(camera.get_target_pivot().z - 1000.0) > near_step * 3.9, "pan speed grows with zoom distance")

	# After a quarter turn right, forward is along -X... confirm panning follows the yaw.
	camera.focus_on(Vector3(1000.0, 0.0, 1000.0))
	var turn_time: float = (PI / 2.0) / deg_to_rad(settings.rotate_speed_deg)
	camera.step(turn_time, Vector2.ZERO, 1.0)
	_check(is_equal_approx(camera.get_target_yaw(), PI / 2.0), "rotating right for a quarter turn sets yaw to 90 degrees")
	camera.step(0.1, Vector2(0.0, 1.0), 0.0)
	var turned: Vector3 = camera.get_target_pivot() - Vector3(1000.0, 0.0, 1000.0)
	_check(turned.x < -1.0 and absf(turned.z) < 0.001, "pan is relative to the yaw (forward goes along -X at yaw 90)")

	# Bounds.
	camera.focus_on(Vector3(-500.0, 0.0, 9999.0))
	var pivot: Vector3 = camera.get_target_pivot()
	_check(is_equal_approx(pivot.x, settings.bounds.position.x) and is_equal_approx(pivot.z, settings.bounds.end.y),
		"the pivot is clamped to the bounds")
	camera.step(1.0, Vector2(-1.0, -1.0), 0.0, true)
	pivot = camera.get_target_pivot()
	_check(settings.bounds.has_point(Vector2(pivot.x, pivot.z)) or pivot.x == settings.bounds.position.x
		or pivot.z == settings.bounds.end.y, "panning cannot leave the bounds")

	# Zoom limits and pitch curve.
	camera.set_zoom_distance(1.0)
	_check(is_equal_approx(camera.get_target_zoom(), settings.zoom_min), "zoom stops at the minimum")
	camera.set_zoom_distance(1.0e6)
	_check(is_equal_approx(camera.get_target_zoom(), settings.zoom_max), "zoom stops at the maximum")
	_check(is_equal_approx(camera.pitch_for_zoom(settings.zoom_min), 25.0), "pitch is 25 degrees fully zoomed in")
	_check(is_equal_approx(camera.pitch_for_zoom(settings.zoom_max), 70.0), "pitch is 70 degrees fully zoomed out")

	# The Camera3D really sits above and behind the pivot, looking at it.
	camera.focus_on(Vector3(1000.0, 0.0, 1000.0))
	camera.set_zoom_distance(200.0)
	camera.snap()
	var cam: Camera3D = camera.get_camera()
	var offset: Vector3 = cam.global_position - camera.global_position
	_check(offset.y > 0.0 and offset.length() > 190.0 and offset.length() < 210.0, "the camera is above the pivot at the zoom distance")
	var look: Vector3 = -cam.global_transform.basis.z
	_check(look.dot((camera.global_position - cam.global_position).normalized()) > 0.999, "the camera looks at the pivot")

	# Smoothing moves toward the target without overshooting it.
	settings.position_smoothing = 12.0
	camera.focus_on(Vector3(1100.0, 0.0, 1000.0))
	camera.step(0.016, Vector2.ZERO, 0.0)
	_check(camera.position.x > 1000.0 and camera.position.x < 1100.0, "smoothing eases toward the target")

	# While a text box has the keyboard, held camera keys do nothing; once it lets go, they work again.
	camera.focus_on(Vector3(1000.0, 0.0, 1000.0))
	camera.set_zoom_distance(100.0)
	camera.snap()
	var box: LineEdit = LineEdit.new()
	root.add_child(box)
	box.grab_focus()
	await process_frame
	Input.action_press("cam_forward")
	Input.action_press("cam_rotate_right")
	var held_before: Vector3 = camera.get_target_pivot()
	var yaw_before: float = camera.get_target_yaw()
	for i: int in 10:
		await process_frame
	_check(camera.get_target_pivot() == held_before and camera.get_target_yaw() == yaw_before,
		"held camera keys are ignored while a text box has focus")
	box.release_focus()
	for i: int in 10:
		await process_frame
	_check(camera.get_target_pivot() != held_before, "held camera keys work again after the text box lets go")
	Input.action_release("cam_forward")
	Input.action_release("cam_rotate_right")
	box.queue_free()

	# main.tscn composes the camera, ground, light and environment.
	var main_packed: PackedScene = load("res://scenes/main.tscn")
	_check(main_packed != null, "main.tscn loads")
	if main_packed != null:
		var main_scene: Node = main_packed.instantiate()
		_check(main_scene.get_node_or_null("CityCamera") is CityCamera, "main.tscn has the CityCamera")
		_check(main_scene.get_node_or_null("Ground") is MeshInstance3D, "main.tscn has the ground")
		_check(main_scene.get_node_or_null("Sun") is DirectionalLight3D, "main.tscn has the light")
		_check(main_scene.get_node_or_null("WorldEnvironment") is WorldEnvironment, "main.tscn has the environment")
		main_scene.free()

	if _failures == 0:
		print("PASS: camera")
	quit(0 if _failures == 0 else 1)


func _check(condition: bool, what: String) -> void:
	if condition:
		return
	_failures += 1
	printerr("FAIL: ", what)
