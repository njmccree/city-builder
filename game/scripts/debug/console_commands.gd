extends Node
## The `Console` autoload: a registry of developer commands and the parser that runs them.
##
## `register()` adds a command with typed arguments; `execute()` splits a line, checks and converts the
## arguments, and calls the command. Output goes out through `line_printed` so any front end (the on-screen
## console, a test) can show it. Everything the built-in commands do to the game goes through `Sim`.
##
## Developer-facing text here is English on purpose (CLAUDE.md, Hard rules #4): it is not player-facing.

signal line_printed(text: String)
signal cleared

const SAVE_DIRECTORY: String = "user://saves/"
const SAVE_EXTENSION: String = ".csav"
const ARG_TYPES: PackedStringArray = ["int", "float", "string"]
## Why the on-screen console is off in release exports unless this project setting is true.
const RELEASE_SETTING: String = "citysim/debug/allow_dev_console_in_release"

var _commands: Dictionary = {} ## name -> {callable, help, args}


func _ready() -> void:
	_register_builtins()
	Sim.state_hash_ready.connect(
		func(tick: int, hash: String) -> void: print_line("state hash at tick %d: %s" % [tick, hash]))


## True when the on-screen console may be opened: always in debug builds, in release only if the project allows.
func is_available() -> bool:
	return not OS.has_feature("release") or bool(ProjectSettings.get_setting(RELEASE_SETTING, false))


## Adds a command. `arg_spec` lists argument types, each "int", "float" or "string"; a trailing "?" makes an
## argument optional (optional ones must come last). The callable gets the converted arguments and may return
## text to print. Registering a name again replaces it.
func register(command_name: String, callable: Callable, help_text: String, arg_spec: PackedStringArray = []) -> void:
	for spec: String in arg_spec:
		if not ARG_TYPES.has(spec.trim_suffix("?")):
			push_error("Console.register(%s): unknown argument type '%s'" % [command_name, spec])
			return
	_commands[command_name] = {"callable": callable, "help": help_text, "args": arg_spec}


func has_command(command_name: String) -> bool:
	return _commands.has(command_name)


## Command names in alphabetical order.
func get_command_names() -> PackedStringArray:
	var names: PackedStringArray = PackedStringArray(_commands.keys())
	names.sort()
	return names


func print_line(text: String) -> void:
	line_printed.emit(text)


## Runs one line such as "speed 2". Prints the result (or the problem) and returns true if the command ran.
func execute(line: String) -> bool:
	var words: PackedStringArray = _split(line)
	if words.is_empty():
		return false
	var command_name: String = words[0]
	if not _commands.has(command_name):
		print_line("Unknown command '%s'. Type 'help'." % command_name)
		return false
	var entry: Dictionary = _commands[command_name]
	var spec: PackedStringArray = entry["args"]
	var converted: Array = []
	var given: int = words.size() - 1
	var required: int = 0
	for arg: String in spec:
		if not arg.ends_with("?"):
			required += 1
	if given < required or given > spec.size():
		print_line("Usage: %s" % usage(command_name))
		return false
	for index: int in given:
		var value: Variant = _convert(words[index + 1], spec[index].trim_suffix("?"))
		if value == null:
			print_line("'%s' is not a valid %s. Usage: %s" % [words[index + 1], spec[index].trim_suffix("?"), usage(command_name)])
			return false
		converted.append(value)
	var callable: Callable = entry["callable"]
	var result: Variant = callable.callv(converted)
	if result is String and not (result as String).is_empty():
		print_line(result)
	return true


## "name <int> [string]" style usage text for a command.
func usage(command_name: String) -> String:
	var entry: Dictionary = _commands[command_name]
	var parts: PackedStringArray = [command_name]
	for arg: String in entry["args"]:
		parts.append("[%s]" % arg.trim_suffix("?") if arg.ends_with("?") else "<%s>" % arg)
	return " ".join(parts)


## Completes the first word of `text`: returns the longest shared prefix of the matching command names,
## and the matches themselves through `matches` (so the caller can list them).
func complete(text: String, matches: PackedStringArray) -> String:
	matches.clear()
	if text.contains(" "):
		return text
	for command_name: String in get_command_names():
		if command_name.begins_with(text):
			matches.append(command_name)
	if matches.is_empty():
		return text
	var prefix: String = matches[0]
	for candidate: String in matches:
		while not candidate.begins_with(prefix):
			prefix = prefix.left(prefix.length() - 1)
	return prefix


func _split(line: String) -> PackedStringArray:
	var words: PackedStringArray = []
	for word: String in line.strip_edges().split(" ", false):
		words.append(word)
	return words


func _convert(word: String, type_name: String) -> Variant:
	match type_name:
		"int":
			return int(word) if word.is_valid_int() else null
		"float":
			return float(word) if word.is_valid_float() else null
		_:
			return word


## Save names are plain file names: letters, digits, '_' and '-'. Anything else could escape the saves folder.
func _save_path(save_name: String) -> String:
	for index: int in save_name.length():
		var character: String = save_name[index]
		if not (character.is_valid_identifier() or character == "-" or character.is_valid_int()):
			return ""
	return SAVE_DIRECTORY + save_name + SAVE_EXTENSION


func _register_builtins() -> void:
	register("help", _cmd_help, "List the commands, or describe one.", ["string?"])
	register("clear", _cmd_clear, "Clear the console.")
	register("speed", _cmd_speed, "Set the game speed: 0 pauses, 1-3 are 1x, 2x, 4x.", ["int"])
	register("pause", _cmd_pause, "Toggle pause.")
	register("save", _cmd_save, "Save the game to user://saves/<name>.csav.", ["string"])
	register("load", _cmd_load, "Load user://saves/<name>.csav.", ["string"])
	register("hash", _cmd_hash, "Print the hash of the whole game state (taken between ticks).")
	register("era", _cmd_era, "Change era, e.g. era base:era.colonial.", ["string"])
	register("quit", _cmd_quit, "Quit the game.")


func _cmd_clear() -> String:
	cleared.emit()
	return ""


func _cmd_pause() -> String:
	Sim.submit_command({"type": "toggle_pause"})
	return "Pause toggled."


func _cmd_hash() -> String:
	Sim.submit_command({"type": "state_hash"})
	return "Hash requested..."


func _cmd_era(era_id: String) -> String:
	Sim.submit_command({"type": "set_era", "era_id": era_id})
	return "Era change requested."


func _cmd_quit() -> String:
	get_tree().quit()
	return ""


func _cmd_help(command_name: String = "") -> String:
	if command_name.is_empty():
		var lines: PackedStringArray = []
		for name_in_list: String in get_command_names():
			lines.append("%s - %s" % [usage(name_in_list), _commands[name_in_list]["help"]])
		return "\n".join(lines)
	if not _commands.has(command_name):
		return "Unknown command '%s'." % command_name
	return "%s - %s" % [usage(command_name), _commands[command_name]["help"]]


func _cmd_speed(speed: int) -> String:
	if speed < 0 or speed > 3:
		return "Speed must be 0 (pause) or 1-3."
	if speed == 0:
		if not bool(Sim.get_stats()["paused"]):
			Sim.submit_command({"type": "toggle_pause"})
		return "Paused."
	if bool(Sim.get_stats()["paused"]):
		Sim.submit_command({"type": "toggle_pause"})
	Sim.submit_command({"type": "set_speed", "speed": speed})
	return "Speed %d." % speed


func _cmd_save(save_name: String) -> String:
	var path: String = _save_path(save_name)
	if path.is_empty():
		return "Save names may use letters, digits, '_' and '-' only."
	if Sim.submit_command({"type": "save_game", "path": path}):
		return "Saving %s ..." % path
	return ""


func _cmd_load(save_name: String) -> String:
	var path: String = _save_path(save_name)
	if path.is_empty():
		return "Save names may use letters, digits, '_' and '-' only."
	if not FileAccess.file_exists(path):
		return "No save at %s." % path
	if Sim.submit_command({"type": "load_game", "path": path}):
		return "Loading %s ..." % path
	return ""
