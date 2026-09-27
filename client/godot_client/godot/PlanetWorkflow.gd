extends Control

const DialogLayout = preload("res://DialogLayout.gd")
const PlanetArt = preload("res://PlanetArt.gd")

signal command_requested(command: String, data: Dictionary, label: String, mutating: bool)

var _planet_id := 0
var _sector_id := 0
var _citadel_level := -1
var _busy := false
var _pickup_limit := 0
var _dropoff_limit := 0
var _info_label: Label
var _colonist_label: Label
var _quantity: SpinBox
var _pickup: Button
var _dropoff: Button
var _workflow_margin: MarginContainer
var _panel_inner: MarginContainer
var _heading: BoxContainer
var _hero: BoxContainer
var _planet_visual: TextureRect
var _transfer_row: BoxContainer
var _development_status: Label
var _citadel_button: Button
var _stock_commodity: OptionButton
var _goods_label: Label
var _stock_quantity: SpinBox
var _stock_deposit: Button
var _stock_withdraw: Button
var _rename_button: Button
var _labor_status: Label
var _labor_ore: SpinBox
var _labor_organics: SpinBox
var _labor_equipment: SpinBox
var _labor_save: Button
var _labor_max := {"ore": 0, "organics": 0, "equipment": 0}
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
	launch.pressed.connect(_launch_now)
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
	planet_visual.material = PlanetArt.cutout_material()
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
	_build_management_controls(column)
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
	_stock_commodity.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_stock_quantity.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_stock_deposit.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_stock_withdraw.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_stock_row_layout(compact)
	_labor_row_layout(compact)

func open_planet(planet_id: int) -> void:
	if planet_id <= 0:
		return
	_planet_id = planet_id
	_sector_id = 0
	_citadel_level = -1
	visible = true
	_info_label.text = "Loading the planet's confirmed surface records…"
	_colonist_label.text = "Awaiting authoritative colony and ship capacity information."
	_pickup.disabled = true
	_dropoff.disabled = true
	_development_status.text = "Loading colony records and construction state…"
	_citadel_button.disabled = true
	_request_details()

func refresh_details() -> void:
	_request_details()

func _request_details() -> void:
	if _planet_id <= 0:
		return
	command_requested.emit("planet.info", {"planet_id": _planet_id}, "Planet information", false)
	command_requested.emit("planet.colonists.get", {"planet_id": _planet_id}, "Colony and cargo information", false)

func set_planet_info(data: Dictionary) -> void:
	_sector_id = int(data.get("sector_id", _sector_id))
	_citadel_level = int(data.get("citadel_level", data.get("level", -1)))
	var lines: Array[String] = []
	for key in ["name", "planet_name", "class", "type_name", "type_description", "owner", "owner_name", "owner_id", "sector_id", "citadel_level", "citadel"]:
		if data.has(key):
			lines.append("%s: %s" % [key.replace("_", " ").capitalize(), str(data[key])])
	_labor_max = {"ore": int(data.get("max_colonists_ore", 0)), "organics": int(data.get("max_colonists_organics", 0)), "equipment": int(data.get("max_colonists_equipment", 0))}
	_labor_ore.max_value = int(_labor_max["ore"])
	_labor_organics.max_value = int(_labor_max["organics"])
	_labor_equipment.max_value = int(_labor_max["equipment"])
	_labor_ore.value = int(data.get("colonists_ore", 0))
	_labor_organics.value = int(data.get("colonists_org", 0))
	_labor_equipment.value = int(data.get("colonists_eq", 0))
	_update_labor_limits(data)
	_update_labor_summary(data)
	_update_goods(data.get("goods", []))
	var planet_class := PlanetArt.class_for_planet(data)
	if not planet_class.is_empty():
		_planet_visual.texture = _planet_texture(planet_class)
		_planet_visual.material = PlanetArt.cutout_material(planet_class)
	if data.has("planet") and data["planet"] is Dictionary:
		for key in ["name", "class", "type_name", "owner", "citadel_level"]:
			if data["planet"].has(key):
				lines.append("%s: %s" % [key.replace("_", " ").capitalize(), str(data["planet"][key])])
	_info_label.text = "\n".join(lines) if not lines.is_empty() else "The server confirmed the planet, but returned no displayable surface fields."
	_update_development_controls()

func _update_goods(goods: Variant) -> void:
	var display: Array[String] = []
	_stock_commodity.clear()
	if goods is Array:
		for item in goods:
			if not item is Dictionary:
				continue
			var code := str(item.get("commodity", ""))
			if code.is_empty() or code == "COLONISTS":
				continue
			var quantity := int(item.get("quantity", 0))
			var capacity := int(item.get("max_capacity", 0))
			display.append("%s: %s / %s" % [code, quantity, capacity])
			_stock_commodity.add_item(code)
			_stock_commodity.set_item_metadata(_stock_commodity.item_count - 1, code)
	if _citadel_level > 0:
		_stock_commodity.add_item("Credits · Citadel")
		_stock_commodity.set_item_metadata(_stock_commodity.item_count - 1, "CREDITS")
	if _stock_commodity.item_count == 0:
		_stock_commodity.add_item("No configured goods")
		_stock_commodity.set_item_metadata(0, "")
	_goods_label.text = "Confirmed stock / capacity\n" + ("\n".join(display) if not display.is_empty() else "No commodity capacity is configured for this planet.")
	_update_development_controls()

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
	_update_labor_limits(data)

func set_busy(busy: bool) -> void:
	_busy = busy
	_quantity.editable = not busy
	_stock_quantity.editable = not busy
	_labor_ore.editable = not busy
	_labor_organics.editable = not busy
	_labor_equipment.editable = not busy
	_stock_commodity.disabled = busy
	_rename_button.disabled = busy
	_update_transfer_buttons()
	_update_development_controls()

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

func _launch_now() -> void:
	if not _busy and _planet_id > 0:
		command_requested.emit("planet.launch", {}, "Launch from planet", true)

func _build_management_controls(column: VBoxContainer) -> void:
	var separator := HSeparator.new()
	column.add_child(separator)
	var title := Label.new()
	title.text = "PLANET DEVELOPMENT"
	title.add_theme_color_override("font_color", Color(0.48, 0.86, 0.84))
	column.add_child(title)
	_development_status = Label.new()
	_development_status.text = "Citadel status is loading."
	_development_status.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	column.add_child(_development_status)
	var action_row := BoxContainer.new()
	action_row.add_theme_constant_override("separation", 10)
	column.add_child(action_row)
	_citadel_button = Button.new()
	_citadel_button.text = "BUILD CITADEL"
	_citadel_button.disabled = true
	_apply_button_style(_citadel_button, true)
	_citadel_button.pressed.connect(_confirm_citadel_construction)
	action_row.add_child(_citadel_button)
	_rename_button = Button.new()
	_rename_button.text = "RENAME PLANET"
	_apply_button_style(_rename_button, false)
	_rename_button.pressed.connect(_open_rename_dialog)
	action_row.add_child(_rename_button)
	var transfer_title := Label.new()
	transfer_title.text = "PLANET STOCK · SHIP TRANSFER"
	transfer_title.add_theme_color_override("font_color", Color(0.48, 0.86, 0.84))
	column.add_child(transfer_title)
	var transfer_hint := Label.new()
	transfer_hint.text = "Move ore, organics, or equipment between your ship and this planet. The server checks cargo and storage limits."
	transfer_hint.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	transfer_hint.add_theme_color_override("font_color", Color(0.72, 0.79, 0.8))
	column.add_child(transfer_hint)
	var labor_title := Label.new()
	labor_title.text = "COLONIST WORK ASSIGNMENTS"
	labor_title.add_theme_color_override("font_color", Color(0.48, 0.86, 0.84))
	column.add_child(labor_title)
	_labor_status = Label.new()
	_labor_status.text = "Assign colonists to mining, organics farming, or equipment production."
	_labor_status.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	column.add_child(_labor_status)
	var labor_row := BoxContainer.new()
	labor_row.add_theme_constant_override("separation", 8)
	column.add_child(labor_row)
	_labor_ore = _labor_spinbox("Ore miners")
	labor_row.add_child(_labor_ore)
	_labor_organics = _labor_spinbox("Organics growers")
	labor_row.add_child(_labor_organics)
	_labor_equipment = _labor_spinbox("Equipment workers")
	labor_row.add_child(_labor_equipment)
	_labor_save = Button.new()
	_labor_save.text = "ASSIGN WORKERS"
	_labor_save.disabled = true
	_apply_button_style(_labor_save, true)
	_labor_save.pressed.connect(_save_labor_assignments)
	column.add_child(_labor_save)
	var labor_hint := Label.new()
	labor_hint.text = "Production runs on the server's planet growth tick (about every 10 minutes). Unassigned colonists remain available for later jobs or citadel construction."
	labor_hint.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	labor_hint.add_theme_color_override("font_color", Color(0.66, 0.74, 0.76))
	column.add_child(labor_hint)
	var stock_row := BoxContainer.new()
	stock_row.add_theme_constant_override("separation", 10)
	column.add_child(stock_row)
	_stock_commodity = OptionButton.new()
	_stock_commodity.add_item("No configured goods")
	_stock_commodity.set_item_metadata(0, "")
	_stock_commodity.item_selected.connect(func(_index: int) -> void:
		_update_development_controls()
	)
	stock_row.add_child(_stock_commodity)
	_goods_label = Label.new()
	_goods_label.text = "Commodity inventory loads with planet information."
	_goods_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_goods_label.add_theme_color_override("font_color", Color(0.72, 0.81, 0.81))
	column.add_child(_goods_label)
	_stock_quantity = SpinBox.new()
	_stock_quantity.min_value = 1
	_stock_quantity.max_value = 1
	_stock_quantity.step = 1
	_stock_quantity.value = 1
	stock_row.add_child(_stock_quantity)
	_stock_deposit = Button.new()
	_stock_deposit.text = "DEPOSIT TO PLANET"
	_stock_deposit.disabled = true
	_apply_button_style(_stock_deposit, false)
	_stock_deposit.pressed.connect(_send_stock_transfer.bind("planet.deposit"))
	stock_row.add_child(_stock_deposit)
	_stock_withdraw = Button.new()
	_stock_withdraw.text = "WITHDRAW TO SHIP"
	_stock_withdraw.disabled = true
	_apply_button_style(_stock_withdraw, false)
	_stock_withdraw.pressed.connect(_send_stock_transfer.bind("planet.withdraw"))
	stock_row.add_child(_stock_withdraw)
	var note := Label.new()
	note.text = "Genesis planets are owned by their creator. There is no separate planet claim command."
	note.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	note.add_theme_color_override("font_color", Color(0.59, 0.68, 0.7))
	column.add_child(note)

func _stock_row_layout(compact: bool) -> void:
	if not is_instance_valid(_stock_commodity):
		return
	var row := _stock_commodity.get_parent() as BoxContainer
	if row:
		row.vertical = compact
		row.add_theme_constant_override("separation", 6 if compact else 10)
	var actions := _citadel_button.get_parent() as BoxContainer
	if actions:
		actions.vertical = compact
		actions.add_theme_constant_override("separation", 6 if compact else 10)

func _labor_spinbox(label: String) -> SpinBox:
	var spin := SpinBox.new()
	spin.min_value = 0
	spin.max_value = 0
	spin.step = 1
	spin.value = 0
	spin.prefix = label + ": "
	spin.focus_mode = Control.FOCUS_ALL
	spin.get_line_edit().select_all_on_focus = true
	spin.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	return spin

func _labor_row_layout(compact: bool) -> void:
	if not is_instance_valid(_labor_ore):
		return
	var row := _labor_ore.get_parent() as BoxContainer
	if row:
		row.vertical = compact
		row.add_theme_constant_override("separation", 5 if compact else 8)
	for spin in [_labor_ore, _labor_organics, _labor_equipment]:
		spin.size_flags_horizontal = Control.SIZE_EXPAND_FILL

func _update_labor_limits(data: Dictionary) -> void:
	if not is_instance_valid(_labor_ore):
		return
	var unassigned := int(data.get("colonists_unassigned", data.get("planet_colonists", 0)))
	var assigned := int(_labor_ore.value) + int(_labor_organics.value) + int(_labor_equipment.value)
	var available_population := maxi(0, unassigned + assigned)
	for entry in [
		{"field": "ore", "spin": _labor_ore},
		{"field": "organics", "spin": _labor_organics},
		{"field": "equipment", "spin": _labor_equipment},
	]:
		var class_limit := int(_labor_max[entry["field"]])
		var population_limit := maxi(available_population, int(entry["spin"].value))
		# Older server builds omit class limits. Keep the fields editable using
		# the confirmed colony population; the server remains authoritative.
		entry["spin"].max_value = mini(class_limit, population_limit) if class_limit > 0 else population_limit

func _update_labor_summary(data: Dictionary) -> void:
	var unassigned := int(data.get("colonists_unassigned", 0))
	var ore_rate := int(data.get("colonists_ore", 0))
	var org_rate := int(data.get("colonists_org", 0)) * int(data.get("organics_production_per_worker", 0))
	var equipment_rate := int(data.get("colonists_eq", 0)) * int(data.get("equipment_production_per_worker", 0))
	_labor_status.text = "Unassigned: %s · Workers — ore %s, organics %s, equipment %s\nEstimated per growth tick: ore %s, organics %s, equipment %s" % [
		_format_count(unassigned), _format_count(int(data.get("colonists_ore", 0))),
		_format_count(int(data.get("colonists_org", 0))), _format_count(int(data.get("colonists_eq", 0))),
		_format_count(ore_rate), _format_count(org_rate), _format_count(equipment_rate)
	]
	_labor_save.disabled = _busy or _planet_id <= 0

func _format_count(value: int) -> String:
	return str(value)

func _save_labor_assignments() -> void:
	if _busy or _planet_id <= 0:
		return
	command_requested.emit("planet.colonists.allocate", {
		"planet_id": _planet_id,
		"ore": int(_labor_ore.value),
		"organics": int(_labor_organics.value),
		"equipment": int(_labor_equipment.value)
	}, "Assign planet workers", true)

func _update_development_controls() -> void:
	if not is_instance_valid(_citadel_button):
		return
	var at_maximum := _citadel_level >= 6
	var has_state := _citadel_level >= 0
	_citadel_button.text = "BUILD CITADEL" if _citadel_level <= 0 else "UPGRADE CITADEL · LEVEL %d" % (_citadel_level + 1)
	_citadel_button.disabled = _busy or not has_state or at_maximum
	if at_maximum:
		_development_status.text = "Citadel level 6 · maximum level reached."
	elif has_state:
		_development_status.text = "Citadel level %d. Construction consumes planet colonists, ore, organics, and equipment; the server checks the exact requirements." % _citadel_level
	else:
		_development_status.text = "Citadel level has not been confirmed by the server yet."
	var credits_selected := str(_stock_commodity.get_item_metadata(_stock_commodity.selected)) == "CREDITS"
	var stock_code := str(_stock_commodity.get_item_metadata(_stock_commodity.selected))
	var treasury_locked := credits_selected and _citadel_level < 1
	_stock_deposit.disabled = _busy or treasury_locked or stock_code.is_empty()
	_stock_withdraw.disabled = _busy or treasury_locked or stock_code.is_empty()
	if treasury_locked:
		_stock_deposit.tooltip_text = "A citadel is required for planet credits storage."
		_stock_withdraw.tooltip_text = "A citadel is required for treasury withdrawals."
	else:
		_stock_deposit.tooltip_text = "The server validates ownership, cargo, and accepted commodities."
		_stock_withdraw.tooltip_text = "The server validates planet stock and available ship holds."
	_rename_button.disabled = _busy or _planet_id <= 0
	_labor_save.disabled = _busy or _planet_id <= 0

func _confirm_citadel_construction() -> void:
	if _busy or _planet_id <= 0 or _citadel_level < 0 or _citadel_level >= 6:
		return
	var is_build := _citadel_level == 0
	var dialog := ConfirmationDialog.new()
	dialog.title = "Build citadel" if is_build else "Upgrade citadel"
	dialog.dialog_text = "The server will verify ownership and deduct this planet type's required colonists, ore, organics, and equipment before timed construction starts. Continue?"
	dialog.ok_button_text = "START CONSTRUCTION"
	add_child(dialog)
	dialog.confirmed.connect(func() -> void:
		var command := "citadel.build" if is_build else "citadel.upgrade"
		var payload := {"planet_id": _planet_id}
		command_requested.emit(command, payload, "Build citadel" if is_build else "Upgrade citadel", true)
		dialog.queue_free()
	)
	dialog.canceled.connect(dialog.queue_free)
	DialogLayout.popup(dialog, Vector2i(620, 260))

func _send_stock_transfer(command: String) -> void:
	if _busy or _planet_id <= 0 or _stock_quantity.value < 1:
		return
	var commodity := str(_stock_commodity.get_item_metadata(_stock_commodity.selected))
	if commodity == "CREDITS" and _citadel_level < 1:
		return
	command_requested.emit(command, {
		"planet_id": _planet_id,
		"commodity": commodity,
		"quantity": int(_stock_quantity.value),
	}, "Deposit %s" % commodity if command == "planet.deposit" else "Withdraw %s" % commodity, true)

func _open_rename_dialog() -> void:
	if _busy or _planet_id <= 0:
		return
	var dialog := ConfirmationDialog.new()
	dialog.title = "Rename planet"
	dialog.dialog_text = "Choose a planet name between 3 and 32 characters."
	dialog.ok_button_text = "RENAME"
	var input := LineEdit.new()
	input.placeholder_text = "New planet name"
	input.max_length = 32
	dialog.add_child(input)
	add_child(dialog)
	dialog.get_ok_button().disabled = true
	input.text_changed.connect(func(value: String) -> void:
		dialog.get_ok_button().disabled = value.strip_edges().length() < 3
	)
	dialog.confirmed.connect(func() -> void:
		var new_name := input.text.strip_edges()
		if new_name.length() >= 3:
			command_requested.emit("planet.rename", {"planet_id": _planet_id, "new_name": new_name}, "Rename planet", true)
		dialog.queue_free()
	)
	dialog.canceled.connect(dialog.queue_free)
	DialogLayout.popup(dialog, Vector2i(520, 220))

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

func _planet_texture(planet_class: String = "M") -> Texture2D:
	return PlanetArt.texture_for_class(planet_class)

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
