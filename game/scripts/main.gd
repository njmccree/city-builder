extends Node
## Entry scene. Starts a new game with the default content and reports what the simulation does. Keeps the
## camera inside the test map: whenever a map appears, its size becomes the camera's bounds.

@onready var _camera: CityCamera = $CityCamera
@onready var _map_view: Node3D = $TestMapView


func _ready() -> void:
	_map_view.connect("map_changed", _on_map_changed)
	var result: Dictionary = Sim.new_game()
	if not result["ok"]:
		printerr("The game could not start; see the errors above.")
		return
	print("CitySim core version: ", Sim.core.get_core_version())
	Sim.date_changed.connect(func(text: String) -> void: print("Date: ", text))


func _on_map_changed(size: Vector2) -> void:
	_camera.set_bounds(Rect2(Vector2.ZERO, size))
	_camera.focus_on(Vector3(size.x * 0.5, 0.0, size.y * 0.5))
	_camera.set_zoom_distance(maxf(size.x, size.y) * 0.6)
