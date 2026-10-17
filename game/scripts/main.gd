extends Node
## Entry scene. Starts a new game with the default content and reports what the simulation does.


func _ready() -> void:
	var result: Dictionary = Sim.new_game()
	if not result["ok"]:
		printerr("The game could not start; see the errors above.")
		return
	print("CitySim core version: ", Sim.core.get_core_version())
	Sim.date_changed.connect(func(text: String) -> void: print("Date: ", text))
