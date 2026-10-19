extends Node
## The `Sim` autoload: the game's only door to the C++ simulation.
##
## Owns the CitySim node, starts games with the project's default content, and re-emits its signals so the UI
## connects to `Sim` and never needs to know about the node behind it.

signal tick_advanced(tick: int)
signal date_changed(text: String)
signal speed_changed(speed: int, paused: bool)
signal save_completed(path: String, ok: bool, message: String)
signal load_completed(path: String, ok: bool, message: String)
## The answer to a {"type": "state_hash"} command: the tick it was taken at and the hash as 16 hex digits.
signal state_hash_ready(tick: int, hash: String)
## What the simulation reports about a command. `tag` says what happened ("city_found_failed", "city_founded",
## "active_city_changed", "active_city_failed"), `key` is the localization key of the reason (or empty), `text`
## its text, `subject` the city id it is about (or 0).
signal sim_notice(tag: String, key: String, text: String, subject: int)

## Folders searched for mods: the base game and the bundled optional mods.
const DEFAULT_CONTENT_ROOTS: PackedStringArray = ["res://content", "res://content/mods"]
const DEFAULT_MODS: PackedStringArray = ["base"]
const DEFAULT_ERA: String = "base:era.ancient"
const DEFAULT_SEED: int = 1
## Cells per side of the debug test map the walking-skeleton agents walk on (4 m per cell).
const DEFAULT_TEST_MAP_SIZE: int = 256
## The developer content root with the synthetic map, until a real map package exists. Not committed: build it
## with `citysim_mapgen --synthetic --mod game/content_dev` (see game/README.md).
const DEV_CONTENT_ROOT: String = "res://content_dev"
const DEV_MOD: String = "test"
const DEV_MAP_ID: String = "test:map.synthetic"

var core: CitySim


func _ready() -> void:
	core = CitySim.new()
	core.name = "CitySim"
	add_child(core)
	core.tick_advanced.connect(func(tick: int) -> void: tick_advanced.emit(tick))
	core.date_changed.connect(func(text: String) -> void: date_changed.emit(text))
	core.speed_changed.connect(func(speed: int, paused: bool) -> void: speed_changed.emit(speed, paused))
	core.save_completed.connect(
		func(path: String, ok: bool, message: String) -> void: save_completed.emit(path, ok, message)
	)
	core.load_completed.connect(
		func(path: String, ok: bool, message: String) -> void: load_completed.emit(path, ok, message)
	)
	core.state_hash_ready.connect(func(tick: int, hash: String) -> void: state_hash_ready.emit(tick, hash))
	core.sim_notice.connect(
		func(tag: String, key: String, text: String, subject: int) -> void: sim_notice.emit(tag, key, text, subject)
	)


func _exit_tree() -> void:
	if core != null:
		core.shutdown()


## Starts a game and returns {ok, errors, warnings}. Errors are also printed.
func new_game(
	seed: int = DEFAULT_SEED,
	era_id: String = DEFAULT_ERA,
	content_roots: PackedStringArray = DEFAULT_CONTENT_ROOTS,
	enabled_mods: PackedStringArray = DEFAULT_MODS,
	test_map_size: int = DEFAULT_TEST_MAP_SIZE,
	map_id: String = "",
	world_agents: bool = false,
) -> Dictionary:
	var result: Dictionary = core.new_game(seed, content_roots, enabled_mods, era_id, test_map_size, map_id, world_agents)
	for message: String in result["warnings"]:
		push_warning("Sim.new_game: " + message)
	for message: String in result["errors"]:
		push_error("Sim.new_game: " + message)
	return result


## True when the developer map (DEV_CONTENT_ROOT) has been built.
func has_dev_world() -> bool:
	return FileAccess.file_exists(DEV_CONTENT_ROOT.path_join(DEV_MOD).path_join("mod.json"))


## Starts a game on the developer map and founds a city on it (the first city is the active one, so its terrain
## appears). `site` is the city's south-west tile; the default (-1, -1) picks the middle of the detail terrain
## (find_city_site). Returns new_game's {ok, errors, warnings} plus "site" (Vector2i).
func new_world(seed: int = DEFAULT_SEED, world_agents: bool = true, site: Vector2i = Vector2i(-1, -1)) -> Dictionary:
	var roots: PackedStringArray = DEFAULT_CONTENT_ROOTS.duplicate()
	roots.append(DEV_CONTENT_ROOT)
	var mods: PackedStringArray = DEFAULT_MODS.duplicate()
	mods.append(DEV_MOD)
	var result: Dictionary = new_game(seed, DEFAULT_ERA, roots, mods, 0, DEV_MAP_ID, world_agents)
	if not result["ok"]:
		return result
	if site.x < 0 or site.y < 0:
		site = find_city_site()
	result["site"] = site
	found_city(site.x, site.y, "Dev City")
	return result


## A 4 x 4 tile city area that lies entirely in the detail terrain, as near the middle of it as possible
## (Vector2i(-1, -1) if there is none). The simulation still checks land and overlap when the city is founded.
func find_city_site() -> Vector2i:
	var info: Dictionary = core.get_map_info()
	if info.is_empty():
		return Vector2i(-1, -1)
	var detail: Dictionary = {}
	var low: Vector2i = Vector2i(1 << 30, 1 << 30)
	var high: Vector2i = Vector2i(-(1 << 30), -(1 << 30))
	for tile: Vector2i in info["detail_tiles"]:
		detail[tile] = true
		low = Vector2i(mini(low.x, tile.x), mini(low.y, tile.y))
		high = Vector2i(maxi(high.x, tile.x), maxi(high.y, tile.y))
	var middle: Vector2 = Vector2(low + high) * 0.5 - Vector2(1.5, 1.5)
	var best: Vector2i = Vector2i(-1, -1)
	var best_distance: float = INF
	for ty: int in range(low.y, high.y - 2):
		for tx: int in range(low.x, high.x - 2):
			var fits: bool = true
			for dy: int in 4:
				for dx: int in 4:
					fits = fits and detail.has(Vector2i(tx + dx, ty + dy))
			var distance: float = Vector2(tx, ty).distance_to(middle)
			if fits and distance < best_distance:
				best = Vector2i(tx, ty)
				best_distance = distance
	return best


## The map's {map_id, width_m, height_m, tiles_x, tiles_y, detail_tiles}; empty without a map.
func get_map_info() -> Dictionary:
	return core.get_map_info()


## A city-local position (east, height, north meters from the city's south-west corner, the frame commands and
## snapshots use) as a position in the scene. Godot's +z points south when seen from above with x to the east,
## so north is -z; otherwise the world would be drawn as its mirror image (docs/architecture.md section 11).
static func to_view(local: Vector3) -> Vector3:
	return Vector3(local.x, local.y, -local.z)


## A scene position as a city-local one (east, height, north): the inverse of to_view().
static func from_view(view: Vector3) -> Vector3:
	return Vector3(view.x, view.y, -view.z)


func shutdown() -> void:
	core.shutdown()


func is_running() -> bool:
	return core.is_running()


## Submits a command dictionary, e.g. {"type": "set_speed", "speed": 2}. Returns false if it was rejected.
func submit_command(command: Dictionary) -> bool:
	return core.submit_command(command)


func get_stats() -> Dictionary:
	return core.get_stats()


## Founds a city on the 8 x 8 km area whose south-west tile is (origin_tx, origin_ty). An empty name gives
## "City N". Returns false if the command was rejected; whether the site was acceptable arrives later as a
## `sim_notice` ("city_founded", or "city_found_failed" with the reason).
func found_city(origin_tx: int, origin_ty: int, city_name: String = "") -> bool:
	var command: Dictionary = {"type": "found_city", "origin_tx": origin_tx, "origin_ty": origin_ty}
	if not city_name.is_empty():
		command["name"] = city_name
	return core.submit_command(command)


## Makes a city the one being viewed (its terrain tiles are the active ones).
func set_active_city(city_id: int) -> bool:
	return core.submit_command({"type": "set_active_city", "city_id": city_id})


## Reshapes the ground of the active city with a round brush, instantly (the cost is reported, nothing is charged
## yet). `op` is "raise", "lower", "flatten", "smooth" or "level"; `x` and `z` are the brush center in the city's
## local frame (meters from its south-west corner); `radius_m` is 2-200, `strength` 0-1; `target_height_m`
## (meters above sea level) is used by "flatten" and "level". Returns false if the command was malformed; a
## brush the simulation refuses arrives later as a `sim_notice` ("terraform_failed" with the reason), and a stroke
## that changed the ground as "terraform_applied" (the volume and cost are in the "world.terraform.last" channel).
func terraform(city_id: int, op: String, x: float, z: float, radius_m: float, strength: float, target_height_m: float = 0.0) -> bool:
	return core.submit_command({
		"type": "terraform", "city_id": city_id, "op": op, "x": x, "z": z,
		"radius_m": radius_m, "strength": strength, "target_height_m": target_height_m,
	})


## Debug and testing: raises (or lowers) the water of a river reach by `offset_m` meters above its base level.
## The flood spreads over the next ticks. A reach index or level that is refused arrives as a `sim_notice`
## ("river_level_failed"). Returns false if the command itself was malformed.
func set_river_level(reach: int, offset_m: float) -> bool:
	return core.submit_command({"type": "set_river_level", "reach": reach, "offset_m": offset_m})


## The founded cities: [{id, name, origin_tx, origin_ty, active}].
func get_cities() -> Array:
	return core.get_cities()


## The id of the city being viewed, 0 if there is none.
func get_active_city_id() -> int:
	return core.get_active_city_id()
