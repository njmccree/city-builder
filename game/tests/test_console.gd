extends SceneTree
## Headless test of the developer console: parsing, every built-in command, completion, the on-screen toggle,
## and save -> load -> state hash equality. It cannot judge how the console looks.
## Run: tools/godot.sh --headless --path game -s res://tests/test_console.gd
## Exits 0 on success and 1 on any failure. Nodes are typed as Node because in `-s` mode this script compiles
## before the autoloads exist.

var _failures: int = 0
var _sim: Node
var _console: Node
var _lines: PackedStringArray = []


func _initialize() -> void:
	_run()


func _run() -> void:
	await process_frame
	_sim = root.get_node_or_null("Sim")
	_console = root.get_node_or_null("Console")
	if _sim == null or _console == null:
		_check(false, "the Sim and Console autoloads are there")
		quit(1)
		return
	_console.line_printed.connect(func(text: String) -> void: _lines.append(text))
	_check(bool(_sim.new_game()["ok"]), "a game starts")
	await create_timer(0.5).timeout

	# Parsing and errors.
	_check(not _console.execute("nonsense"), "an unknown command is refused")
	_check(_last().contains("Unknown command"), "...with a message")
	_check(not _console.execute("speed"), "a missing argument is refused")
	_check(_last().contains("Usage: speed <int>"), "...with the usage")
	_check(not _console.execute("speed fast"), "a wrong argument type is refused")
	_check(not _console.execute("speed 1 2"), "too many arguments are refused")
	_check(not _console.execute("   "), "an empty line does nothing")
	_check(_console.execute("help"), "help runs")
	var help_text: String = _lines[_lines.size() - 1]
	for command_name: String in ["help", "clear", "speed", "pause", "save", "load", "hash", "era", "quit"]:
		_check(help_text.contains(command_name) and _console.has_command(command_name), "help lists " + command_name)
	_check(_console.execute("help speed") and _last().contains("speed <int>"), "help <command> describes one")

	# Custom registration with typed arguments.
	var seen: Array = []
	var probe: Callable = func(count: int, ratio: float, label: String) -> String:
		seen.append([count, ratio, label])
		return "probed"
	_console.register("probe", probe, "test command", PackedStringArray(["int", "float", "string"]))
	_check(_console.execute("probe 3 0.5 hello") and seen == [[3, 0.5, "hello"]], "typed arguments are converted")
	_check(not _console.execute("probe 3 x hello"), "a bad float is refused")

	# Completion.
	var matches: PackedStringArray = []
	_check(_console.complete("sa", matches) == "save" and matches == PackedStringArray(["save"]), "sa completes to save")
	_check(_console.complete("s", matches) == "s" and matches.size() >= 2, "s is ambiguous and lists the candidates")
	_check(_console.complete("zzz", matches) == "zzz" and matches.is_empty(), "no match leaves the text")

	# speed / pause reach the simulation.
	_check(_console.execute("speed 9") and _last().contains("must be"), "speed outside 0-3 is rejected")
	_console.execute("speed 2")
	await create_timer(0.5).timeout
	_check(_sim.get_stats()["speed"] == 2 and not _sim.get_stats()["paused"], "speed 2 takes effect")
	_console.execute("speed 0")
	await create_timer(0.5).timeout
	_check(_sim.get_stats()["paused"], "speed 0 pauses")
	_console.execute("pause")
	await create_timer(0.5).timeout
	_check(not _sim.get_stats()["paused"], "pause toggles back")
	_console.execute("era base:era.colonial")
	_console.execute("era base:era.ancient")

	# Save names are plain file names.
	_console.execute("save ../evil")
	_check(_last().contains("letters, digits"), "a save name with a path is refused")
	_console.execute("load does_not_exist_zz")
	_check(_last().contains("No save at"), "loading a missing save says so")

	# save -> load -> hash equality, taken while paused so the state cannot move between the steps.
	_console.execute("speed 3")
	await create_timer(1.0).timeout
	_console.execute("speed 0")
	await create_timer(0.5).timeout
	_lines.clear()
	_console.execute("save consoletest")
	await create_timer(0.5).timeout
	_console.execute("hash")
	await create_timer(0.5).timeout
	var first_hash: String = _hash_in_output()
	_check(first_hash.length() == 16, "hash prints 16 hex digits (%s)" % first_hash)
	_lines.clear()
	_console.execute("speed 3")
	await create_timer(1.5).timeout
	_console.execute("hash")
	await create_timer(0.5).timeout
	var moved_hash: String = _hash_in_output()
	_check(moved_hash != first_hash, "the hash changes when the game runs on")
	_lines.clear()
	_console.execute("load consoletest")
	await create_timer(1.0).timeout
	_console.execute("hash")
	await create_timer(0.5).timeout
	var loaded_hash: String = _hash_in_output()
	_check(loaded_hash == first_hash, "save -> load -> hash is equal (%s vs %s)" % [first_hash, loaded_hash])

	# The on-screen console opens and closes with its key action and runs commands.
	var screen: Node = (load("res://ui/debug/dev_console.tscn") as PackedScene).instantiate()
	root.add_child(screen)
	await process_frame
	_check(not screen.is_open(), "the on-screen console starts closed")
	await _press("dev_console")
	_check(screen.is_open(), "the key opens it")
	await _press("dev_console")
	_check(not screen.is_open(), "the key closes it")

	_console.execute("clear")
	DirAccess.remove_absolute(ProjectSettings.globalize_path("user://saves/consoletest.csav"))
	if _failures == 0:
		print("PASS: console")
	quit(0 if _failures == 0 else 1)


func _last() -> String:
	return "" if _lines.is_empty() else _lines[_lines.size() - 1]


func _hash_in_output() -> String:
	for index: int in range(_lines.size() - 1, -1, -1):
		if _lines[index].begins_with("state hash at tick"):
			return _lines[index].split(": ")[1]
	return ""


func _press(action: String) -> void:
	var event: InputEventAction = InputEventAction.new()
	event.action = action
	event.pressed = true
	Input.parse_input_event(event)
	await process_frame


func _check(condition: bool, what: String) -> void:
	if condition:
		return
	_failures += 1
	printerr("FAIL: ", what)
