class_name DebugOverlay
extends CanvasLayer
## Top-left panel with live numbers for developers: frame rate, simulation ticks, memory. F3 shows or hides it.
##
## Reads `Sim.get_stats()` a few times a second. Runs on render time, not the simulation clock, and only reads.

const REFRESH_SECONDS: float = 0.25
const BYTES_PER_MB: float = 1024.0 * 1024.0
## Mirrors the `speed_multipliers` define (index 0 is paused); only used to show the number.
const SPEED_MULTIPLIERS: Array[int] = [0, 1, 2, 4]

var _since_refresh: float = 0.0

@onready var _panel: Control = $Panel
@onready var _label: Label = $Panel/Margin/Label


func _ready() -> void:
	refresh()


func _process(delta: float) -> void:
	if not _panel.visible:
		return
	_since_refresh += delta
	if _since_refresh >= REFRESH_SECONDS:
		_since_refresh = 0.0
		refresh()


func _unhandled_input(event: InputEvent) -> void:
	if event.is_action_pressed("debug_overlay"):
		_panel.visible = not _panel.visible
		if _panel.visible:
			refresh()
		get_viewport().set_input_as_handled()


func is_shown() -> bool:
	return _panel.visible


func refresh() -> void:
	_label.text = build_text()


## The overlay text. Public so tests can check it without waiting for the timer.
func build_text() -> String:
	var lines: PackedStringArray = []
	lines.append(tr("ui.debug.fps").format({"fps": Engine.get_frames_per_second()}))
	if not Sim.is_running():
		lines.append(tr("ui.debug.no_game"))
	else:
		var stats: Dictionary = Sim.get_stats()
		var paused: bool = stats["paused"]
		var speed_text: String = tr("ui.debug.speed_paused")
		if not paused:
			var index: int = clampi(stats["speed"], 0, SPEED_MULTIPLIERS.size() - 1)
			speed_text = tr("ui.time.speed").format({"multiplier": SPEED_MULTIPLIERS[index]})
		lines.append(tr("ui.debug.tps").format({"tps": "%.1f" % stats["tps"]}))
		lines.append(tr("ui.debug.tick_ms").format({
			"last": "%.3f" % stats["tick_ms_last"],
			"avg": "%.3f" % stats["tick_ms_avg"],
		}))
		lines.append(tr("ui.debug.tick").format({"tick": stats["tick"]}))
		lines.append(tr("ui.debug.entities").format({"count": stats["entity_count"]}))
		lines.append(tr("ui.debug.speed").format({"speed": speed_text}))
		lines.append(tr("ui.debug.date").format({"date": stats["date_text"]}))
		lines.append(tr("ui.debug.time").format({"time": stats["time_text"]}))
	lines.append(tr("ui.debug.memory").format({
		"static_mb": "%.1f" % (Performance.get_monitor(Performance.MEMORY_STATIC) / BYTES_PER_MB),
		"video_mb": "%.1f" % (Performance.get_monitor(Performance.RENDER_VIDEO_MEM_USED) / BYTES_PER_MB),
	}))
	return "\n".join(lines)
