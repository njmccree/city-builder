class_name UiInput
extends RefCounted
## Helpers for input that must not reach the game while the player is typing.


## True while a text box (such as the developer console's input line) has the keyboard. Game code that reads
## held keys directly (`Input.is_action_pressed`, `Input.get_vector`) is not stopped by the text box the way
## key events are, so it asks this first.
static func is_typing(node: Node) -> bool:
	var viewport: Viewport = node.get_viewport()
	if viewport == null:
		return false
	var focus: Control = viewport.gui_get_focus_owner()
	return focus is LineEdit or focus is TextEdit
