extends Node
## Entry scene. For now it only proves the C++ simulation is reachable from GDScript.


func _ready() -> void:
	var sim: CitySim = CitySim.new()
	print("CitySim core version: ", sim.get_core_version())
	sim.free()
