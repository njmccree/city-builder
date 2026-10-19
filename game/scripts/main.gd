extends Node
## Entry scene. Starts a new game with the default content and reports what the simulation does. Keeps the
## camera inside the test map: whenever a map appears, its size becomes the camera's bounds. In a world game
## (the dev console's `new_world`) the terrain renderer replaces the test ground: the camera is kept inside the
## active city's area and rides on its ground.

## The side of a city area in meters (4 tiles of 2,048 m).
const CITY_AREA_M: float = 8192.0

@onready var _camera: CityCamera = $CityCamera
@onready var _map_view: Node3D = $TestMapView
@onready var _terrain: Node3D = $TerrainRenderer
@onready var _ground: Node3D = $Ground


func _ready() -> void:
	_map_view.connect("map_changed", _on_map_changed)
	_terrain.connect("terrain_changed", _on_terrain_changed)
	var result: Dictionary = Sim.new_game()
	if not result["ok"]:
		printerr("The game could not start; see the errors above.")
		return
	print("CitySim core version: ", Sim.core.get_core_version())
	Sim.date_changed.connect(func(text: String) -> void: print("Date: ", text))


func _on_terrain_changed(city_id: int, _origin_tile: Vector2i) -> void:
	var world: bool = city_id != 0
	_ground.visible = not world
	_map_view.visible = not world
	if not world:
		_camera.ground_height = Callable()
		return
	# The camera works in scene coordinates (north is -z); the terrain answers in city-local ones.
	_camera.ground_height = func(x: float, z: float) -> float: return _terrain.get_height_at(x, -z)
	_camera.set_bounds(Rect2(Vector2(0.0, -CITY_AREA_M), Vector2(CITY_AREA_M, CITY_AREA_M)))
	_camera.focus_on(Sim.to_view(Vector3(CITY_AREA_M * 0.5, 0.0, CITY_AREA_M * 0.5)))
	_camera.set_zoom_distance(1500.0)


func _on_map_changed(size: Vector2) -> void:
	_ground.visible = true
	_map_view.visible = true
	_camera.ground_height = Callable()
	# The map runs east along x and north along -z (Sim.to_view).
	_camera.set_bounds(Rect2(Vector2(0.0, -size.y), size))
	_camera.focus_on(Sim.to_view(Vector3(size.x * 0.5, 0.0, size.y * 0.5)))
	_camera.set_zoom_distance(maxf(size.x, size.y) * 0.6)
