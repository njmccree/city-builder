class_name DevConsole
extends CanvasLayer
## The on-screen developer console: a text box under a scrolling log. The `~` key (action `dev_console`)
## opens and closes it. Up/Down walk the command history and Tab completes a command name.
##
## It only talks to the `Console` autoload, which owns the commands. Unavailable (never shown) in release
## exports unless the project setting allows it; see `Console.is_available()`.

const MAX_HISTORY: int = 100

var _history: PackedStringArray = []
var _history_index: int = 0 ## == _history.size() while typing a new line

@onready var _panel: Control = $Panel
@onready var _log: RichTextLabel = $Panel/Column/Log
@onready var _line: LineEdit = $Panel/Column/Input


func _ready() -> void:
	_panel.visible = false
	_line.text_submitted.connect(_on_submitted)
	_line.gui_input.connect(_on_input_gui_input)
	Console.line_printed.connect(_on_line_printed)
	Console.cleared.connect(_log.clear)
	if not Console.is_available():
		set_process_input(false)
	else:
		_log.append_text("Developer console. Type 'help'.\n")


func is_open() -> bool:
	return _panel.visible


func set_open(open: bool) -> void:
	if open and not Console.is_available():
		return
	_panel.visible = open
	if open:
		_line.grab_focus()
	else:
		_line.release_focus()


## `_input`, not `_unhandled_input`, so the key still closes the console while the text box has focus.
func _input(event: InputEvent) -> void:
	if event.is_action_pressed("dev_console"):
		set_open(not _panel.visible)
		get_viewport().set_input_as_handled()


func _on_submitted(line: String) -> void:
	_line.clear()
	if line.strip_edges().is_empty():
		return
	_log.append_text("> %s\n" % line)
	if _history.is_empty() or _history[_history.size() - 1] != line:
		_history.append(line)
		if _history.size() > MAX_HISTORY:
			_history.remove_at(0)
	_history_index = _history.size()
	Console.execute(line)


func _on_line_printed(text: String) -> void:
	_log.append_text(text.replace("[", "[lb]") + "\n")


func _on_input_gui_input(event: InputEvent) -> void:
	var key: InputEventKey = event as InputEventKey
	if key == null or not key.pressed:
		return
	match key.keycode:
		KEY_UP:
			_walk_history(-1)
		KEY_DOWN:
			_walk_history(1)
		KEY_TAB:
			_complete()
		_:
			return
	_line.accept_event()


func _walk_history(step: int) -> void:
	if _history.is_empty():
		return
	_history_index = clampi(_history_index + step, 0, _history.size())
	_line.text = "" if _history_index == _history.size() else _history[_history_index]
	_line.caret_column = _line.text.length()


func _complete() -> void:
	var matches: PackedStringArray = []
	var completed: String = Console.complete(_line.text, matches)
	if matches.size() > 1:
		_log.append_text("%s\n" % "  ".join(matches))
	_line.text = completed + (" " if matches.size() == 1 else "")
	_line.caret_column = _line.text.length()
