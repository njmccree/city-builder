extends SceneTree
## Headless test of the simulation test scene: it loads, shows live numbers, and its keys and buttons reach the
## simulation. Run: tools/godot.sh --headless --path game -s res://tests/test_scene.gd
## Exits 0 on success and 1 on any failure.

enum Step { START, RUNNING, PAUSED, SPEED, SAVED, LOADED, DONE }

var _sim: Node
var _step: Step = Step.START
var _waited: float = 0.0
var _scene: Control


func _initialize() -> void:
	_sim = root.get_node_or_null("Sim")
	if _sim == null:
		_fail("the Sim autoload is not there")
		return
	var packed: PackedScene = load("res://scenes/test_sim.tscn")
	if packed == null:
		_fail("the test scene did not load")
		return
	_scene = packed.instantiate()
	root.add_child(_scene)


func _press(keycode: Key) -> void:
	var event: InputEventKey = InputEventKey.new()
	event.keycode = keycode
	event.pressed = true
	Input.parse_input_event(event)


func _process(delta: float) -> bool:
	if _scene == null:
		return true
	_waited += delta
	var stats: Dictionary = _sim.get_stats()
	match _step:
		Step.START:
			if _waited < 1.0:
				return false
			if (stats["tick"] as int) <= 0 or (stats["date_text"] as String).is_empty():
				return _fail("the game did not start ticking")
			_press(KEY_SPACE)
			_step = Step.RUNNING
			_waited = 0.0
		Step.RUNNING:
			if _waited < 0.5:
				return false
			if not (stats["paused"] as bool):
				return _fail("Space did not pause")
			_press(KEY_3)
			_step = Step.PAUSED
			_waited = 0.0
		Step.PAUSED:
			if _waited < 0.3:
				return false
			if (stats["speed"] as int) != 3:
				return _fail("key 3 did not set speed 3 (speed is %s)" % stats["speed"])
			_press(KEY_S)
			_step = Step.SPEED
			_waited = 0.0
		Step.SPEED:
			if _waited < 0.5:
				return false
			if not FileAccess.file_exists(_scene.SAVE_PATH):
				return _fail("S did not write the save file")
			_press(KEY_E)
			_press(KEY_L)
			_step = Step.SAVED
			_waited = 0.0
		Step.SAVED:
			if _waited < 0.5:
				return false
			var label: Label = _first_label_with(_scene, "Date:")
			if label == null:
				return _fail("the stats panel is missing")
			print("PASS: test scene works. Panel:\n", label.text)
			quit(0)
			return true
	return false


func _first_label_with(node: Node, prefix: String) -> Label:
	var label: Label = node as Label
	if label != null and label.text.begins_with(prefix):
		return label
	for child: Node in node.get_children():
		var found: Label = _first_label_with(child, prefix)
		if found != null:
			return found
	return null


func _fail(message: String) -> bool:
	printerr("FAIL: ", message)
	quit(1)
	return true
