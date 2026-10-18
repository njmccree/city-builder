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
) -> Dictionary:
	var result: Dictionary = core.new_game(seed, content_roots, enabled_mods, era_id, test_map_size, map_id)
	for message: String in result["warnings"]:
		push_warning("Sim.new_game: " + message)
	for message: String in result["errors"]:
		push_error("Sim.new_game: " + message)
	return result


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
