extends SceneTree
## Headless smoke test: the GDExtension loads, a game starts, ticks advance, and commands reach the simulation.
## Run: tools/godot.sh --headless --path game -s res://tests/smoke.gd
## Exits 0 on success and 1 on any failure.

const SAVE_PATH: String = "user://smoke_test/game.csav"

enum Step { RUNNING, PAUSING, PAUSED, SAVING, DONE }

var _sim: CitySim
var _step: Step = Step.RUNNING
var _waited: float = 0.0
var _tick_when_paused: int = 0
var _saved: bool = false
var _save_ok: bool = false
var _dates: Array[String] = []


func _initialize() -> void:
	if not ClassDB.class_exists("CitySim"):
		_fail("class CitySim is not registered (is the extension built into game/bin/?)")
		return

	_sim = CitySim.new()
	root.add_child(_sim)  # its _process drives the signals
	if _sim.get_core_version().is_empty():
		_fail("get_core_version() returned an empty string")
		return
	_sim.date_changed.connect(func(text: String) -> void: _dates.append(text))
	_sim.save_completed.connect(
		func(_path: String, ok: bool, _message: String) -> void:
			_saved = true
			_save_ok = ok
	)

	# Commands before a game exists, and bad commands, are rejected.
	if _sim.is_running():
		_fail("is_running() is true before new_game()")
		return

	var result: Dictionary = _sim.new_game(
		7, ["res://content", "res://content/mods"], ["base"], "base:era.ancient"
	)
	if not result["ok"]:
		_fail("new_game failed: " + str(result["errors"]))
		return
	if not _sim.is_running():
		_fail("is_running() is false after new_game()")
		return

	var bad_game: Dictionary = _sim.new_game(7, ["res://content"], ["base"], "base:era.nonexistent")
	if bad_game["ok"] or (bad_game["errors"] as Array).is_empty():
		_fail("new_game accepted an unknown era")
		return
	# A failed new_game ends the game that was running; start it again.
	result = _sim.new_game(7, ["res://content", "res://content/mods"], ["base"], "base:era.ancient")
	if not result["ok"]:
		_fail("new_game failed the second time: " + str(result["errors"]))
		return

	for bad: Dictionary in [
		{},
		{"type": "no_such_command"},
		{"type": "set_speed"},
		{"type": "set_speed", "speed": 9},
		{"type": "set_speed", "speed": "fast"},
		{"type": "set_speed", "speed": 2, "extra": 1},
		{"type": "set_era", "era_id": "base:era.nonexistent"},
		{"type": "save_game", "path": ""},
	]:
		print("(expecting an error for ", bad, ")")
		if _sim.submit_command(bad):
			_fail("a bad command was accepted: " + str(bad))
			return
	if not _sim.submit_command({"type": "set_speed", "speed": 2}):
		_fail("set_speed was rejected")
		return


func _process(delta: float) -> bool:
	if _sim == null:
		return true  # _initialize failed and already quit
	_waited += delta
	var stats: Dictionary = _sim.get_stats()
	match _step:
		Step.RUNNING:
			if _waited < 1.0:
				return false
			if (stats["tick"] as int) <= 0:
				return _fail("the tick did not advance in a second")
			if (stats["speed"] as int) != 2:
				return _fail("set_speed 2 had no effect: speed is " + str(stats["speed"]))
			if (stats["date_text"] as String).is_empty() or _dates.is_empty():
				return _fail("no date reached the UI")
			print("date: ", stats["date_text"], "  time: ", stats["time_text"], "  tick: ", stats["tick"])
			_sim.submit_command({"type": "toggle_pause"})
			_step = Step.PAUSING
			_waited = 0.0
		Step.PAUSING:
			if _waited < 0.5:  # the pause takes effect in the next tick
				return false
			if not (stats["paused"] as bool):
				return _fail("toggle_pause did not pause")
			_tick_when_paused = stats["tick"]
			_step = Step.PAUSED
			_waited = 0.0
		Step.PAUSED:
			if _waited < 0.5:
				return false
			if (stats["tick"] as int) != _tick_when_paused:
				return _fail("the tick advanced while paused")
			# Saving works while paused.
			if not _sim.submit_command({"type": "save_game", "path": SAVE_PATH}):
				return _fail("save_game was rejected")
			_step = Step.SAVING
			_waited = 0.0
		Step.SAVING:
			if not _saved:
				if _waited > 3.0:
					return _fail("no save_completed signal")
				return false
			if not _save_ok:
				return _fail("the save failed")
			print("PASS: CitySim core version ", _sim.get_core_version())
			_sim.shutdown()
			if _sim.is_running():
				return _fail("is_running() is true after shutdown()")
			quit(0)
			return true
	return false


func _fail(message: String) -> bool:
	printerr("FAIL: ", message)
	if _sim != null:
		_sim.shutdown()
	quit(1)
	return true
