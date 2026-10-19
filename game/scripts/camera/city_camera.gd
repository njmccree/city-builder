class_name CityCamera
extends Node3D
## City-builder camera: a pivot on the ground, with yaw, pitch and a Camera3D on an arm.
##
## Input only moves the *target* pan, yaw and zoom; each frame the shown values chase the targets with
## exponential smoothing. The pivot is clamped to the settings' bounds. Pitch follows zoom through a curve.
## Runs on render time (`_process`), never on the simulation clock.

@export var settings: CameraSettings
## Optional: the ground height (meters) at a point (x, z), so the pivot rides on the terrain. Returns NaN where
## it does not know; then the pivot stays at height 0. Unset on the flat test map.
var ground_height: Callable

var _target_pivot: Vector3 = Vector3.ZERO
var _target_yaw: float = 0.0
var _target_zoom: float = 150.0
var _yaw: float = 0.0
var _zoom: float = 150.0
var _default_pitch_curve: Curve

@onready var _yaw_node: Node3D = $Yaw
@onready var _pitch_node: Node3D = $Yaw/Pitch
@onready var _camera: Camera3D = $Yaw/Pitch/Camera3D


func _ready() -> void:
	if settings == null:
		settings = CameraSettings.new()
	_target_pivot = _clamp_to_bounds(position)
	_target_zoom = clampf(_target_zoom, settings.zoom_min, settings.zoom_max)
	snap()


## Moves the shown camera straight onto its targets (no smoothing). Used at start and by tests.
func snap() -> void:
	_yaw = _target_yaw
	_zoom = _target_zoom
	position = _target_pivot
	_apply()


func get_camera() -> Camera3D:
	return _camera


func get_target_pivot() -> Vector3:
	return _target_pivot


func get_target_zoom() -> float:
	return _target_zoom


func get_target_yaw() -> float:
	return _target_yaw


## Moves the pivot to a ground point, keeping the current angle (for "jump to" features).
func focus_on(point: Vector3) -> void:
	_target_pivot = _clamp_to_bounds(Vector3(point.x, 0.0, point.z))
	_target_pivot.y = _ground_at(_target_pivot)


## Sets the rectangle (x, z meters) the pivot may move in, and widens the zoom range so the whole of it fits.
func set_bounds(bounds: Rect2) -> void:
	settings.bounds = bounds
	settings.zoom_max = maxf(settings.zoom_max, maxf(bounds.size.x, bounds.size.y) * 1.1)
	_target_pivot = _clamp_to_bounds(_target_pivot)


func set_zoom_distance(distance: float) -> void:
	_target_zoom = clampf(distance, settings.zoom_min, settings.zoom_max)


## Pitch in degrees for a zoom distance.
func pitch_for_zoom(distance: float) -> float:
	var span: float = settings.zoom_max - settings.zoom_min
	var t: float = 0.0 if span <= 0.0 else clampf((distance - settings.zoom_min) / span, 0.0, 1.0)
	var curve: Curve = settings.pitch_curve if settings.pitch_curve != null else _get_default_pitch_curve()
	return curve.sample(t)


## Advances the camera one frame. `pan` is x = right, y = forward in -1..1; `rotate_input` is -1..1
## (negative turns left). Split from `_process` so tests can drive it without a window.
func step(delta: float, pan: Vector2, rotate_input: float, fast: bool = false) -> void:
	var yaw_basis: Basis = Basis(Vector3.UP, _target_yaw)
	var move: Vector3 = yaw_basis * Vector3(pan.x, 0.0, -pan.y)
	var speed: float = settings.pan_speed_per_distance * _target_zoom
	if fast:
		speed *= settings.fast_multiplier
	_target_pivot = _clamp_to_bounds(_target_pivot + move * speed * delta)
	_target_pivot.y = _ground_at(_target_pivot)
	_target_yaw = wrapf(_target_yaw + rotate_input * deg_to_rad(settings.rotate_speed_deg) * delta, -PI, PI)
	_yaw = _chase(_yaw, _target_yaw, settings.rotation_smoothing, delta, true)
	_zoom = _chase(_zoom, _target_zoom, settings.zoom_smoothing, delta, false)
	position = Vector3(
		_chase(position.x, _target_pivot.x, settings.position_smoothing, delta, false),
		_chase(position.y, _target_pivot.y, settings.position_smoothing, delta, false),
		_chase(position.z, _target_pivot.z, settings.position_smoothing, delta, false))
	_apply()


func _process(delta: float) -> void:
	# Held keys are read directly here, so a focused text box (the developer console) cannot stop them; ask.
	if UiInput.is_typing(self):
		step(delta, Vector2.ZERO, 0.0)
		return
	var pan: Vector2 = Input.get_vector("cam_left", "cam_right", "cam_back", "cam_forward")
	if pan == Vector2.ZERO and _can_edge_pan():
		pan = _edge_pan_input()
	var rotate_input: float = Input.get_axis("cam_rotate_left", "cam_rotate_right")
	step(delta, pan, rotate_input, Input.is_action_pressed("cam_fast"))


func _unhandled_input(event: InputEvent) -> void:
	if event.is_action_pressed("cam_zoom_in"):
		_zoom_notch(1)
	elif event.is_action_pressed("cam_zoom_out"):
		_zoom_notch(-1)
	elif event is InputEventMouseMotion and Input.is_action_pressed("cam_rotate_drag"):
		var motion: InputEventMouseMotion = event
		var direction: float = -1.0 if settings.invert_drag_rotation else 1.0
		_target_yaw = wrapf(
			_target_yaw - motion.relative.x * deg_to_rad(settings.drag_rotate_deg_per_px) * direction, -PI, PI)


func _zoom_notch(direction: int) -> void:
	var notches: int = -direction if settings.invert_zoom else direction
	# In: the distance shrinks by the step factor; out: it grows by the same factor.
	var factor: float = settings.zoom_step_factor if notches > 0 else 1.0 / settings.zoom_step_factor
	set_zoom_distance(_target_zoom * factor)


func _can_edge_pan() -> bool:
	if not settings.edge_pan_enabled or settings.edge_pan_margin_px <= 0:
		return false
	if Input.is_action_pressed("cam_rotate_drag"):
		return false
	var window: Window = get_window()
	return window != null and window.has_focus()


## Edge pan reads the cursor against the viewport; outside the window it does nothing.
func _edge_pan_input() -> Vector2:
	var viewport: Viewport = get_viewport()
	var size: Vector2 = viewport.get_visible_rect().size
	var mouse: Vector2 = viewport.get_mouse_position()
	if mouse.x < 0.0 or mouse.y < 0.0 or mouse.x > size.x or mouse.y > size.y:
		return Vector2.ZERO
	var margin: float = float(settings.edge_pan_margin_px)
	var pan: Vector2 = Vector2.ZERO
	if mouse.x < margin:
		pan.x = -1.0
	elif mouse.x > size.x - margin:
		pan.x = 1.0
	if mouse.y < margin:
		pan.y = 1.0
	elif mouse.y > size.y - margin:
		pan.y = -1.0
	return pan


func _apply() -> void:
	_yaw_node.rotation.y = _yaw
	_pitch_node.rotation_degrees.x = -pitch_for_zoom(_zoom)
	_camera.position = Vector3(0.0, 0.0, _zoom)


func _ground_at(point: Vector3) -> float:
	if not ground_height.is_valid():
		return 0.0
	var height: float = ground_height.call(point.x, point.z)
	return 0.0 if is_nan(height) else height


func _clamp_to_bounds(point: Vector3) -> Vector3:
	var bounds: Rect2 = settings.bounds
	return Vector3(
		clampf(point.x, bounds.position.x, bounds.end.x),
		0.0,
		clampf(point.z, bounds.position.y, bounds.end.y))


## Frame-rate independent exponential approach. A rate of 0 means no smoothing.
func _chase(current: float, target: float, rate: float, delta: float, is_angle: bool) -> float:
	if rate <= 0.0:
		return target
	var weight: float = 1.0 - exp(-rate * delta)
	if is_angle:
		return current + angle_difference(current, target) * weight
	return lerpf(current, target, weight)


func _get_default_pitch_curve() -> Curve:
	if _default_pitch_curve == null:
		_default_pitch_curve = Curve.new()
		_default_pitch_curve.min_value = 0.0
		_default_pitch_curve.max_value = 90.0
		_default_pitch_curve.add_point(Vector2(0.0, 25.0))
		_default_pitch_curve.add_point(Vector2(1.0, 70.0))
	return _default_pitch_curve
