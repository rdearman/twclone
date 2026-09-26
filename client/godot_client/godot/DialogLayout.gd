class_name DialogLayout
extends RefCounted

## AcceptDialog's built-in description label is a direct child, not a layout
## container. Put it and custom content in one VBox so neither draws over the
## other. Keep space at the bottom for the dialog's built-in buttons.
static func attach(dialog: AcceptDialog, content: Control) -> void:
	if dialog == null or content == null:
		return
	var description := dialog.get_label()
	description.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	description.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	description.custom_minimum_size.x = 280
	dialog.dialog_autowrap = true
	var stack := VBoxContainer.new()
	stack.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	stack.offset_left = 16
	stack.offset_top = 48
	stack.offset_right = -16
	stack.offset_bottom = -58
	stack.add_theme_constant_override("separation", 8)
	dialog.add_child(stack)
	dialog.remove_child(description)
	stack.add_child(description)
	content.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	stack.add_child(content)

static func prepare_label(label: Label) -> void:
	if label == null:
		return
	label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	label.size_flags_horizontal = Control.SIZE_EXPAND_FILL

static func popup(dialog: Window, minimum_size: Vector2i) -> void:
	if dialog == null:
		return
	dialog.popup_centered_clamped(minimum_size, 0.82)
