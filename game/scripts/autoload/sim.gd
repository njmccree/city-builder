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

## Folders searched for mods: the base game and the bundled optional mods.
const DEFAULT_CONTENT_ROOTS: PackedStringArray = ["res://content", "res://content/mods"]
const DEFAULT_MODS: PackedStringArray = ["base"]
const DEFAULT_ERA: String = "base:era.ancient"
const DEFAULT_SEED: int = 1

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


func _exit_tree() -> void:
	if core != null:
		core.shutdown()


## Starts a game and returns {ok, errors, warnings}. Errors are also printed.
func new_game(
	seed: int = DEFAULT_SEED,
	era_id: String = DEFAULT_ERA,
	content_roots: PackedStringArray = DEFAULT_CONTENT_ROOTS,
	enabled_mods: PackedStringArray = DEFAULT_MODS,
) -> Dictionary:
	var result: Dictionary = core.new_game(seed, content_roots, enabled_mods, era_id)
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
