class_name TimeControls
extends CanvasLayer
## Top-right date and time of day with Pause / 1x / 2x / 4x buttons. Space pauses, 1 2 3 pick the speed.
##
## Buttons and hotkeys only send commands through `Sim.submit_command`; what is highlighted always follows the
## simulation's own state (read from `Sim.get_stats()`), so it stays right after a load or a rejected command.

## Mirrors the `speed_multipliers` define without its leading 0 (paused): speed 1, 2, 3 = 1x, 2x, 4x.
const SPEED_MULTIPLIERS: Array[int] = [1, 2, 4]

var _speed_buttons: Array[Button] = []

@onready var _date_label: Label = $Panel/Margin/Column/DateLabel
@onready var _time_label: Label = $Panel/Margin/Column/TimeLabel
@onready var _pause_button: Button = $Panel/Margin/Column/Buttons/PauseButton
@onready var _buttons_row: HBoxContainer = $Panel/Margin/Column/Buttons


func _ready() -> void:
	_pause_button.toggle_mode = true
	_pause_button.focus_mode = Control.FOCUS_NONE
	_pause_button.pressed.connect(request_toggle_pause)
	for index: int in SPEED_MULTIPLIERS.size():
		var button: Button = Button.new()
		button.toggle_mode = true
		button.focus_mode = Control.FOCUS_NONE
		button.pressed.connect(request_speed.bind(index + 1))
		_buttons_row.add_child(button)
		_speed_buttons.append(button)
	Sim.speed_changed.connect(func(_speed: int, _paused: bool) -> void: refresh())
	Sim.date_changed.connect(func(_text: String) -> void: refresh())
	refresh()


func _unhandled_input(event: InputEvent) -> void:
	if event.is_action_pressed("time_pause"):
		request_toggle_pause()
	elif event.is_action_pressed("time_speed_1"):
		request_speed(1)
	elif event.is_action_pressed("time_speed_2"):
		request_speed(2)
	elif event.is_action_pressed("time_speed_3"):
		request_speed(3)
	else:
		return
	get_viewport().set_input_as_handled()


func request_toggle_pause() -> void:
	Sim.submit_command({"type": "toggle_pause"})
	refresh()


func request_speed(speed: int) -> void:
	Sim.submit_command({"type": "set_speed", "speed": speed})
	refresh()


## Shows the simulation's current date, time and speed. Safe to call any time.
func refresh() -> void:
	if not Sim.is_running():
		return
	# The text tables are installed when a game starts, which can be after this node became ready, so the
	# captions are set here and not once in _ready().
	_pause_button.text = tr("ui.time.pause")
	for index: int in _speed_buttons.size():
		_speed_buttons[index].text = tr("ui.time.speed").format({"multiplier": SPEED_MULTIPLIERS[index]})
	var stats: Dictionary = Sim.get_stats()
	var paused: bool = stats["paused"]
	var speed: int = stats["speed"]
	_date_label.text = stats["date_text"]
	_time_label.text = stats["time_text"]
	_pause_button.set_pressed_no_signal(paused)
	for index: int in _speed_buttons.size():
		_speed_buttons[index].set_pressed_no_signal(not paused and speed == index + 1)


## Which speed button is lit (1-3), or 0 when none is. For tests.
func get_active_speed() -> int:
	for index: int in _speed_buttons.size():
		if _speed_buttons[index].button_pressed:
			return index + 1
	return 0


## The captions of Pause and the speed buttons, in order. For tests.
func get_button_texts() -> PackedStringArray:
	var texts: PackedStringArray = [_pause_button.text]
	for button: Button in _speed_buttons:
		texts.append(button.text)
	return texts


func is_pause_lit() -> bool:
	return _pause_button.button_pressed
