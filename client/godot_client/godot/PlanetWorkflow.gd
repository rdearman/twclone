extends Control

const OBJECT_ATLAS := preload("res://assets/sector_objects_atlas.png")
const DialogLayout = preload("res://DialogLayout.gd")

signal command_requested(command: String, data: Dictionary, label: String, mutating: bool)

var _planet_id := 0
var _busy := false
var _pickup_limit := 0
var _dropoff_limit := 0
var _info_label: Label
var _colonist_label: Label
var _quantity: SpinBox
var _pickup: Button
var _dropoff: Button
var _launch_dialog: ConfirmationDialog
var _workflow_margin: MarginContainer
var _panel_inner: MarginContainer
var _heading: BoxContainer
var _hero: BoxContainer
var _planet_visual: TextureRect
var _transfer_row: BoxContainer
var _dim: ColorRect

func _ready() -> void:
	set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	z_index = 55
	mouse_filter = Control.MOUSE_FILTER_IGNORE
	visible = false
	var dim := ColorRect.new()
	dim.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	dim.offset_top = 88
	dim.offset_bottom = -62
	dim.color = Color(0.006, 0.014, 0.025, 0.985)
	dim.mouse_filter = Control.MOUSE_FILTER_STOP
	add_child(dim)
	_dim = dim
	var margin := MarginContainer.new()
	margin.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	margin.offset_top = 88
	margin.offset_bottom = -62
	margin.add_theme_constant_override("margin_left", 36)
	margin.add_theme_constant_override("margin_right", 36)
	margin.add_theme_constant_override("margin_top", 30)
	margin.add_theme_constant_override("margin_bottom", 30)
	add_child(margin)
	_workflow_margin = margin
	var panel := PanelContainer.new()
	panel.add_theme_stylebox_override("panel", _panel_style(Color(0.012, 0.033, 0.052, 0.99), Color(0.32, 0.66, 0.69, 0.95)))
	margin.add_child(panel)
	var inner := MarginContainer.new()
	inner.add_theme_constant_override("margin_left", 26)
	inner.add_theme_constant_override("margin_right", 26)
	inner.add_theme_constant_override("margin_top", 22)
	inner.add_theme_constant_override("margin_bottom", 22)
	panel.add_child(inner)
	_panel_inner = inner
	var column := VBoxContainer.new()
	column.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	column.add_theme_constant_override("separation", 14)
	var scroll := ScrollContainer.new()
	scroll.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	scroll.vertical_scroll_mode = ScrollContainer.SCROLL_MODE_AUTO
	inner.add_child(scroll)
	scroll.add_child(column)
	var heading := BoxContainer.new()
	heading.vertical = false
	column.add_child(heading)
	_heading = heading
	var title := Label.new()
	title.text = "PLANET SURFACE"
	title.add_theme_font_size_override("font_size", 24)
	title.add_theme_color_override("font_color", Color(0.95, 0.86, 0.66))
	title.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	heading.add_child(title)
	var launch := Button.new()
	launch.text = "LAUNCH TO SECTOR"
	_apply_button_style(launch, true)
	launch.pressed.connect(_confirm_launch)
	heading.add_child(launch)
	var hero := BoxContainer.new()
	hero.vertical = false
	hero.add_theme_constant_override("separation", 26)
	column.add_child(hero)
	_hero = hero
	var planet_visual := TextureRect.new()
	planet_visual.custom_minimum_size = Vector2(300, 250)
	planet_visual.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	planet_visual.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	planet_visual.texture = _planet_texture()
	hero.add_child(planet_visual)
	_planet_visual = planet_visual
	_info_label = Label.new()
	_info_label.text = "Loading the planet's confirmed surface records…"
	_info_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_info_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_info_label.custom_minimum_size.y = 110
	hero.add_child(_info_label)
	var separator := HSeparator.new()
	column.add_child(separator)
	var transfer_title := Label.new()
	transfer_title.text = "COLONIST TRANSFER"
	transfer_title.add_theme_color_override("font_color", Color(0.48, 0.86, 0.84))
	column.add_child(transfer_title)
	_colonist_label = Label.new()
	_colonist_label.text = "Awaiting authoritative colony and ship capacity information."
	_colonist_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	column.add_child(_colonist_label)
	var transfer_row := BoxContainer.new()
	transfer_row.vertical = false
	transfer_row.add_theme_constant_override("separation", 10)
	column.add_child(transfer_row)
	_transfer_row = transfer_row
	_quantity = SpinBox.new()
	_quantity.min_value = 1
	_quantity.max_value = 1
	_quantity.step = 1
	_quantity.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	transfer_row.add_child(_quantity)
	_pickup = Button.new()
	_pickup.text = "PICK UP FROM PLANET"
	_pickup.disabled = true
	_apply_button_style(_pickup, false)
	_pickup.pressed.connect(_confirm_transfer.bind("pickup"))
	transfer_row.add_child(_pickup)
	_dropoff = Button.new()
	_dropoff.text = "DROP OFF ON PLANET"
	_dropoff.disabled = true
	_apply_button_style(_dropoff, false)
	_dropoff.pressed.connect(_confirm_transfer.bind("dropoff"))
	transfer_row.add_child(_dropoff)
	_launch_dialog = ConfirmationDialog.new()
	_launch_dialog.title = "Launch from planet"
	_launch_dialog.dialog_text = "Launch directly back into the current sector? Sector hazards may engage on entry."
	_launch_dialog.ok_button_text = "LAUNCH"
	_launch_dialog.confirmed.connect(func() -> void:
		if _planet_id > 0 and not _busy:
			command_requested.emit("planet.launch", {}, "Launch from planet", true)
	)
	add_child(_launch_dialog)
	resized.connect(_update_responsive_layout)
	call_deferred("_update_responsive_layout")

func _update_responsive_layout() -> void:
	var compact := size.x < 760.0
	_dim.offset_top = 128.0 if compact else 88.0
	_workflow_margin.offset_top = 128.0 if compact else 88.0
	var column: VBoxContainer = _panel_inner.get_child(0).get_child(0)
	column.add_theme_constant_override("separation", 8 if compact else 14)
	_workflow_margin.add_theme_constant_override("margin_left", 12 if compact else 36)
	_workflow_margin.add_theme_constant_override("margin_right", 12 if compact else 36)
	_workflow_margin.add_theme_constant_override("margin_top", 12 if compact else 30)
	_panel_inner.add_theme_constant_override("margin_left", 12 if compact else 26)
	_panel_inner.add_theme_constant_override("margin_right", 12 if compact else 26)
	_panel_inner.add_theme_constant_override("margin_top", 12 if compact else 22)
	_panel_inner.add_theme_constant_override("margin_bottom", 12 if compact else 22)
	_heading.vertical = compact
	_hero.vertical = compact
	_hero.add_theme_constant_override("separation", 10 if compact else 26)
	_planet_visual.custom_minimum_size = Vector2(120, 105) if compact else Vector2(300, 250)
	_info_label.add_theme_font_size_override("font_size", 13 if compact else 16)
	_colonist_label.add_theme_font_size_override("font_size", 12 if compact else 16)
	_transfer_row.vertical = compact
	_transfer_row.add_theme_constant_override("separation", 6 if compact else 10)
	_quantity.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_pickup.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_dropoff.size_flags_horizontal = Control.SIZE_EXPAND_FILL

func open_planet(planet_id: int) -> void:
	if planet_id <= 0:
		return
	_planet_id = planet_id
	visible = true
	_info_label.text = "Loading the planet's confirmed surface records…"
	_colonist_label.text = "Awaiting authoritative colony and ship capacity information."
	_pickup.disabled = true
	_dropoff.disabled = true
	_request_details()

func refresh_details() -> void:
	_request_details()

func _request_details() -> void:
	if _planet_id <= 0:
		return
	command_requested.emit("planet.info", {"planet_id": _planet_id}, "Planet information", false)
	command_requested.emit("planet.colonists.get", {"planet_id": _planet_id}, "Colony and cargo information", false)

func set_planet_info(data: Dictionary) -> void:
	var lines: Array[String] = []
	for key in ["name", "planet_name", "type", "owner", "owner_name", "sector_id", "citadel_level", "citadel", "ore_on_hand", "organics_on_hand", "equipment_on_hand"]:
		if data.has(key):
			lines.append("%s: %s" % [key.replace("_", " ").capitalize(), str(data[key])])
	if data.has("planet") and data["planet"] is Dictionary:
		for key in ["name", "type", "owner", "citadel_level", "ore_on_hand", "organics_on_hand", "equipment_on_hand"]:
			if data["planet"].has(key):
				lines.append("%s: %s" % [key.replace("_", " ").capitalize(), str(data["planet"][key])])
	_info_label.text = "\n".join(lines) if not lines.is_empty() else "The server confirmed the planet, but returned no displayable surface fields."

func set_colonist_state(data: Dictionary) -> void:
	var planet_count: Variant = data.get("planet_colonists", null)
	var ship_count: Variant = data.get("ship_colonists", null)
	var holds: Variant = data.get("ship_holds_available", null)
	_colonist_label.text = "Planet unassigned: %s   ·   Aboard ship: %s   ·   Free holds: %s" % [_display(planet_count), _display(ship_count), _display(holds)]
	var pickup_max := 0
	if planet_count != null and holds != null:
		pickup_max = mini(int(planet_count), int(holds))
	var dropoff_max := int(ship_count) if ship_count != null else 0
	_pickup_limit = pickup_max
	_dropoff_limit = dropoff_max
	_update_transfer_buttons()
	_quantity.max_value = max(1, maxi(pickup_max, dropoff_max))
	_quantity.value = 1

func set_busy(busy: bool) -> void:
	_busy = busy
	_quantity.editable = not busy
	_update_transfer_buttons()

func _update_transfer_buttons() -> void:
	_pickup.disabled = _busy or _pickup_limit <= 0
	_dropoff.disabled = _busy or _dropoff_limit <= 0

func _confirm_transfer(action: String) -> void:
	if _planet_id <= 0 or _busy or _quantity.value < 1:
		return
	var maximum := _pickup_limit if action == "pickup" else _dropoff_limit
	if maximum <= 0:
		return
	var confirmed_quantity := mini(int(_quantity.value), maximum)
	_quantity.value = confirmed_quantity
	var dialog := ConfirmationDialog.new()
	dialog.title = "Confirm colonist transfer"
	dialog.dialog_text = "%s %d colonists? The server will enforce available stock and cargo capacity." % [action.capitalize(), confirmed_quantity]
	dialog.ok_button_text = "CONFIRM TRANSFER"
	add_child(dialog)
	dialog.confirmed.connect(func() -> void:
		command_requested.emit("planet.colonists.set", {"planet_id": _planet_id, "action": action, "quantity": confirmed_quantity}, "Colonist transfer", true)
		dialog.queue_free()
	)
	dialog.canceled.connect(dialog.queue_free)
	DialogLayout.popup(dialog, Vector2i(500, 210))

func _confirm_launch() -> void:
	if not _busy and _planet_id > 0:
		DialogLayout.popup(_launch_dialog, Vector2i(520, 210))

func launch_confirmed(sector_id: int) -> void:
	visible = false
	_planet_id = 0
	if sector_id > 0:
		# The gameplay view performs the authoritative sector refresh after this transition.
		pass

func _display(value: Variant) -> String:
	return "Unknown" if value == null else str(value)

func _panel_style(fill: Color, border: Color) -> StyleBoxFlat:
	var style := StyleBoxFlat.new()
	style.bg_color = fill
	style.border_color = border
	style.set_border_width_all(1)
	style.set_corner_radius_all(10)
	return style

func _planet_texture() -> AtlasTexture:
	var texture := AtlasTexture.new()
	texture.atlas = OBJECT_ATLAS
	texture.region = Rect2(Vector2(627, 627), Vector2(627, 627))
	return texture

func _apply_button_style(button: Button, primary: bool) -> void:
	button.custom_minimum_size.y = 42
	button.add_theme_color_override("font_color", Color(0.92, 0.93, 0.88))
	button.add_theme_color_override("font_hover_color", Color(1.0, 0.95, 0.82))
	button.add_theme_color_override("font_disabled_color", Color(0.43, 0.51, 0.54))
	button.add_theme_stylebox_override("normal", _button_box(Color(0.027, 0.083, 0.103), Color(0.26, 0.53, 0.57)))
	button.add_theme_stylebox_override("hover", _button_box(Color(0.047, 0.14, 0.16), Color(0.46, 0.82, 0.77)))
	button.add_theme_stylebox_override("focus", _button_box(Color(0.06, 0.15, 0.16), Color(0.98, 0.73, 0.42)))
	button.add_theme_stylebox_override("disabled", _button_box(Color(0.017, 0.035, 0.047), Color(0.13, 0.22, 0.26)))
	if primary:
		button.add_theme_color_override("font_color", Color(0.99, 0.9, 0.71))

func _button_box(fill: Color, border: Color) -> StyleBoxFlat:
	var style := StyleBoxFlat.new()
	style.bg_color = fill
	style.border_color = border
	style.set_border_width_all(1)
	style.set_corner_radius_all(7)
	style.content_margin_left = 12
	style.content_margin_right = 12
	return style
