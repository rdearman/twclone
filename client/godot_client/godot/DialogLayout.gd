class_name DialogLayout
extends RefCounted

## Put custom content beside AcceptDialog's built-in description label.
## Adding it directly to the dialog bypasses the internal VBoxContainer and
## causes labels and controls to occupy the same top-left area.
static func attach(dialog: AcceptDialog, content: Control) -> void:
	if dialog == null or content == null:
		return
	var description := dialog.get_label()
	description.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	description.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	description.custom_minimum_size.x = 280
	dialog.dialog_autowrap = true
	content.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	var content_parent := description.get_parent()
	if content_parent != null:
		content_parent.add_child(content)
	else:
		dialog.add_child(content)

static func prepare_label(label: Label) -> void:
	if label == null:
		return
	label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	label.size_flags_horizontal = Control.SIZE_EXPAND_FILL

static func popup(dialog: Window, minimum_size: Vector2i) -> void:
	if dialog == null:
		return
	dialog.popup_centered_clamped(minimum_size, 0.82)
