extends SceneTree
## Headless test of the debug overlay and the time controls inside main.tscn: the text is localized, F3 toggles
## the overlay, and buttons and hotkeys change the simulation's speed and pause state through the Sim autoload.
## The overlay and controls are typed as Node here: in `-s` mode this script compiles before the autoloads exist.
## It cannot judge layout or looks. Run: tools/godot.sh --headless --path game -s res://tests/test_debug_ui.gd
## Exits 0 on success and 1 on any failure.

const ACTIONS: PackedStringArray = ["debug_overlay", "time_pause", "time_speed_1", "time_speed_2", "time_speed_3"]

var _failures: int = 0
var _sim: Node


func _initialize() -> void:
	_run()


func _run() -> void:
	_sim = root.get_node_or_null("Sim")
	if _sim == null:
		_check(false, "the Sim autoload is there")
		quit(1)
		return
	for action: String in ACTIONS:
		_check(InputMap.has_action(action) and not InputMap.action_get_events(action).is_empty(),
			"input action %s exists with an event" % action)

	var packed: PackedScene = load("res://scenes/main.tscn")
	if packed == null:
		_check(false, "main.tscn loads")
		quit(1)
		return
	var main_scene: Node = packed.instantiate()
	root.add_child(main_scene)
	await create_timer(1.0).timeout

	var overlay: Node = main_scene.get_node_or_null("DebugOverlay")
	var controls: Node = main_scene.get_node_or_null("TimeControls")
	_check(overlay != null and controls != null, "main.tscn has the overlay and the time controls")
	if overlay == null or controls == null:
		quit(1)
		return

	# Text comes from the localization tables, not raw keys.
	var text: String = overlay.build_text()
	_check(text.contains("FPS:") and not text.contains("ui.debug"), "overlay text is localized (no raw keys)")
	_check(text.contains("Sim ticks/s:") and text.contains("Entities:") and text.contains("Memory:"),
		"overlay shows ticks/s, entities and memory")
	_check(text.contains("3001 BCE"), "overlay shows the date")

	# F3 toggles the overlay.
	_check(overlay.is_shown(), "overlay starts visible")
	await _press("debug_overlay")
	_check(not overlay.is_shown(), "F3 hides the overlay")
	await _press("debug_overlay")
	_check(overlay.is_shown(), "F3 shows it again")

	# The captions are translated (not raw keys) even though the game started after the node was ready.
	var captions: PackedStringArray = controls.get_button_texts()
	_check(captions == PackedStringArray(["Pause", "1x", "2x", "4x"]), "the buttons read Pause 1x 2x 4x (%s)" % str(captions))

	# Speed buttons and hotkeys reach the simulation, and the highlight follows its state.
	controls.request_speed(2)
	await create_timer(0.4).timeout
	_check(_sim.get_stats()["speed"] == 2 and controls.get_active_speed() == 2, "button: speed 2 is set and lit")
	await _press("time_speed_3")
	await create_timer(0.4).timeout
	_check(_sim.get_stats()["speed"] == 3 and controls.get_active_speed() == 3, "key 3: speed 3 is set and lit")
	# The shown ticks/s is smoothed and needs about 3 s to settle: 20 per second at 1x, so 80 at 4x.
	await create_timer(3.0).timeout
	var fast_tps: float = _sim.get_stats()["tps"]
	_check(absf(fast_tps - 80.0) < 6.0, "ticks per second is about 80 at 4x (%.1f)" % fast_tps)
	await _press("time_speed_1")
	await create_timer(3.5).timeout
	var slow_tps: float = _sim.get_stats()["tps"]
	_check(_sim.get_stats()["speed"] == 1 and controls.get_active_speed() == 1, "key 1: speed 1 is set and lit")
	_check(absf(slow_tps - 20.0) < 3.0, "ticks per second is about 20 at 1x (%.1f)" % slow_tps)

	# Pause stops the clock and lights Pause only.
	await _press("time_pause")
	await create_timer(0.5).timeout
	_check(_sim.get_stats()["paused"] and controls.is_pause_lit() and controls.get_active_speed() == 0,
		"Space pauses and lights Pause only")
	var paused_tick: int = _sim.get_stats()["tick"]
	await create_timer(0.5).timeout
	_check(_sim.get_stats()["tick"] == paused_tick, "no ticks while paused")
	_check(overlay.build_text().contains("Paused"), "overlay says Paused")
	await _press("time_pause")
	await create_timer(0.5).timeout
	_check(not _sim.get_stats()["paused"] and not controls.is_pause_lit(), "Space resumes")

	main_scene.queue_free()
	if _failures == 0:
		print("PASS: debug ui")
	quit(0 if _failures == 0 else 1)


## Sends an input action as a key press through the real input path, so _unhandled_input sees it.
func _press(action: String) -> void:
	var event: InputEventAction = InputEventAction.new()
	event.action = action
	event.pressed = true
	Input.parse_input_event(event)
	var release: InputEventAction = InputEventAction.new()
	release.action = action
	release.pressed = false
	Input.parse_input_event(release)
	# Unhandled input is delivered on the next frame.
	await process_frame


func _check(condition: bool, what: String) -> void:
	if condition:
		return
	_failures += 1
	printerr("FAIL: ", what)
