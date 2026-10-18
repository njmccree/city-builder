extends Control
## Temporary test scene: shows what the simulation is doing and lets you control it with the keyboard or the
## buttons. Everything goes through the `Sim` autoload, as the real UI will. It is replaced by the real main
## scene and UI in the camera and debug-tool Tasks.
##
## Keys: Space pause/resume | 1 2 3 speed | E next era | S save | L load | N new game (next seed)

const SAVE_PATH: String = "user://saves/test.csav"
const ERAS: Array[String] = [
	"base:era.ancient",
	"base:era.colonial",
	"base:era.modern",
	"base:era.utopian",
]
const SPEED_MULTIPLIERS: Array[int] = [1, 2, 4]
const MAX_LOG_LINES: int = 8

var _stats_label: Label
var _log_label: Label
var _seed: int = Sim.DEFAULT_SEED
var _era_index: int = 0
var _log_lines: Array[String] = []


func _ready() -> void:
	_build_ui()
	Sim.save_completed.connect(_on_save_completed)
	Sim.load_completed.connect(_on_load_completed)
	Sim.speed_changed.connect(_on_speed_changed)
	_new_game()


func _process(_delta: float) -> void:
	if not Sim.is_running():
		_stats_label.text = "No game is running. Press N to start one."
		return
	var stats: Dictionary = Sim.get_stats()
	var speed: int = stats["speed"]
	var paused: bool = stats["paused"]
	var pace: String = "PAUSED" if paused else "%dx" % SPEED_MULTIPLIERS[clampi(speed - 1, 0, 2)]
	_stats_label.text = (
		"Date:      %s\n" % stats["date_text"]
		+ "Time:      %s\n" % stats["time_text"]
		+ "Speed:     %s\n" % pace
		+ "Tick:      %d\n" % stats["tick"]
		+ "Ticks/s:   %.1f\n" % stats["tps"]
		+ "Tick ms:   %.3f (average %.3f)\n" % [stats["tick_ms_last"], stats["tick_ms_avg"]]
		+ "Entities:  %d\n" % stats["entity_count"]
		+ "Seed:      %d\n" % _seed
		+ "Blend:     %.2f (interpolation alpha)\n" % Sim.core.get_interpolation_alpha()
		+ "Frame:     %d fps" % Engine.get_frames_per_second()
	)


func _unhandled_key_input(event: InputEvent) -> void:
	var key: InputEventKey = event as InputEventKey
	if key == null or not key.pressed or key.echo:
		return
	match key.keycode:
		KEY_SPACE:
			_toggle_pause()
		KEY_1:
			_set_speed(1)
		KEY_2:
			_set_speed(2)
		KEY_3:
			_set_speed(3)
		KEY_E:
			_next_era()
		KEY_S:
			_save()
		KEY_L:
			_load()
		KEY_N:
			_seed += 1
			_new_game()


func _new_game() -> void:
	_era_index = 0
	var result: Dictionary = Sim.new_game(_seed, ERAS[_era_index])
	if result["ok"]:
		_say("New game, seed %d, %s." % [_seed, ERAS[_era_index]])
	else:
		_say("The game could not start: " + str(result["errors"]))


func _toggle_pause() -> void:
	if Sim.submit_command({"type": "toggle_pause"}):
		_say("Pause toggled.")


func _set_speed(speed: int) -> void:
	if Sim.submit_command({"type": "set_speed", "speed": speed}):
		_say("Speed %dx requested." % SPEED_MULTIPLIERS[speed - 1])


func _next_era() -> void:
	_era_index = (_era_index + 1) % ERAS.size()
	if Sim.submit_command({"type": "set_era", "era_id": ERAS[_era_index]}):
		_say("Era changed to %s (the calendar now runs at that era's pace)." % ERAS[_era_index])


func _save() -> void:
	if Sim.submit_command({"type": "save_game", "path": SAVE_PATH}):
		_say("Saving...")


func _load() -> void:
	if not FileAccess.file_exists(SAVE_PATH):
		_say("There is no save yet. Press S first.")
		return
	if Sim.submit_command({"type": "load_game", "path": SAVE_PATH}):
		_say("Loading...")


func _on_save_completed(path: String, ok: bool, message: String) -> void:
	if ok:
		_say("Saved: " + path)
	else:
		_say("Save FAILED: " + message)


func _on_load_completed(path: String, ok: bool, message: String) -> void:
	if ok:
		_say("Loaded: " + path + ("" if message.is_empty() else "\nWarning: " + message))
	else:
		_say("Load FAILED: " + message)


func _on_speed_changed(_speed: int, _paused: bool) -> void:
	pass  # the stats panel reads the speed itself; kept to show the signal arrives


func _say(text: String) -> void:
	_log_lines.append(text)
	while _log_lines.size() > MAX_LOG_LINES:
		_log_lines.remove_at(0)
	_log_label.text = "\n".join(_log_lines)
	print(text)


func _build_ui() -> void:
	set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	# An opaque backdrop so it is obvious the scene is drawing, and text stays readable.
	var backdrop: ColorRect = ColorRect.new()
	backdrop.color = Color(0.10, 0.12, 0.16)
	backdrop.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	add_child(backdrop)

	var margin: MarginContainer = MarginContainer.new()
	margin.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	for side: String in ["left", "top", "right", "bottom"]:
		margin.add_theme_constant_override("margin_" + side, 24)
	add_child(margin)

	var column: VBoxContainer = VBoxContainer.new()
	column.add_theme_constant_override("separation", 12)
	margin.add_child(column)

	var title: Label = Label.new()
	title.text = "CityBuilder (working title): simulation test scene"
	title.add_theme_font_size_override("font_size", 24)
	column.add_child(title)

	_stats_label = Label.new()
	_stats_label.add_theme_font_size_override("font_size", 20)
	column.add_child(_stats_label)

	var buttons: HBoxContainer = HBoxContainer.new()
	buttons.add_theme_constant_override("separation", 8)
	column.add_child(buttons)
	_add_button(buttons, "Pause / resume (Space)", _toggle_pause)
	_add_button(buttons, "1x", _set_speed.bind(1))
	_add_button(buttons, "2x", _set_speed.bind(2))
	_add_button(buttons, "4x", _set_speed.bind(3))
	_add_button(buttons, "Next era (E)", _next_era)
	_add_button(buttons, "Save (S)", _save)
	_add_button(buttons, "Load (L)", _load)
	_add_button(buttons, "New game (N)", func() -> void:
		_seed += 1
		_new_game()
	)

	var help: Label = Label.new()
	help.text = "Saves go to: %s" % ProjectSettings.globalize_path(SAVE_PATH)
	column.add_child(help)

	_log_label = Label.new()
	_log_label.modulate = Color(0.75, 0.9, 1.0)
	column.add_child(_log_label)


func _add_button(parent: Control, text: String, action: Callable) -> void:
	var button: Button = Button.new()
	button.text = text
	button.focus_mode = Control.FOCUS_NONE  # so Space is the pause key, not "press the focused button"
	button.pressed.connect(action)
	parent.add_child(button)
