extends Control

signal reconnect_requested
signal warp_requested(destination: int)
signal warp_activated(destination: int)
signal command_requested(command: String, data: Dictionary, label: String, mutating: bool)
signal trade_requested(direction: String, port_id: int, sector_id: int, commodity: String, quantity: int)
signal trade_confirmation(accepted: bool)
signal tavern_enter_requested
signal route_engage_requested
signal route_cancel_requested

const SectorViewScript = preload("res://SectorView.gd")
const CommandMenuScript = preload("res://CommandMenu.gd")
const PortWorkflowScript = preload("res://PortWorkflow.gd")
const PlanetWorkflowScript = preload("res://PlanetWorkflow.gd")
const LocalSectorNotes = preload("res://LocalSectorNotes.gd")
const COMMAND_RAIL_TOAST_X := 310.0

var sector_view
var sector_title: Label
var player_label: Label
var credits_label: Label
var turns_label: Label
var cargo_label: Label
var shields_label: Label
var fighters_label: Label
var freshness_label: Label
var contents_list: VBoxContainer
var selection_hint: Label
var selection_title: Label
var selection_detail: Label
var context_action_list: VBoxContainer
var context_action_buttons: Array[Button] = []
var details_button: Button
var action_button: Button
var notification_label: Label
var offline_overlay: PanelContainer
var transition_cover: ColorRect
var warp_confirmation: ConfirmationDialog
var command_menu
var port_workflow
var planet_workflow
var trade_quote_dialog: ConfirmationDialog
var center_split: SplitContainer
var gameplay_body: HBoxContainer
var command_panel: PanelContainer
var information_panel: PanelContainer
var information_title: Label
var information_text: Label
var information_scroll: ScrollContainer
var selection_panel: PanelContainer
var panel_column: VBoxContainer
var contents_scroll: ScrollContainer
var toast_panel: PanelContainer
var brand_label: Label
var sector_subtitle: Label
var top_row: HBoxContainer
var _snapshot: Dictionary = {}
var _selected_key := ""
var _rows: Dictionary = {}
var _hovered_key := ""
var _planet_mode := false
var _route_panel: PanelContainer
var _route_label: Label
var _local_notes
var _note_input: LineEdit
var _note_status: Label

func _ready() -> void:
	_local_notes = LocalSectorNotes.new()
	_build()
	_render_snapshot(_snapshot)
	resized.connect(_update_responsive_layout)
	_update_responsive_layout()

func set_snapshot(snapshot: Dictionary) -> void:
	var previous_sector = _snapshot.get("hud", {}).get("sector_id", null)
	var next_sector = snapshot.get("hud", {}).get("sector_id", null)
	if port_workflow != null and port_workflow.visible and previous_sector != null and next_sector != null and int(previous_sector) != int(next_sector):
		port_workflow.close_for_location_change()
		command_menu.set_tavern_access(false)
	_snapshot = snapshot.duplicate(true)
	_render_snapshot(_snapshot)

func set_connection_notice(disconnected: bool, message: String = "") -> void:
	offline_overlay.visible = disconnected
	if disconnected:
		var status: Label = offline_overlay.get_node("Margin/Row/Status")
		status.text = message if not message.is_empty() else "DISCONNECTED · showing last confirmed state"

func show_notification(message: String) -> void:
	notification_label.text = message
	notification_label.visible = not message.is_empty()
	if not message.is_empty():
		var timer := get_tree().create_timer(4.0)
		timer.timeout.connect(func() -> void:
			if is_instance_valid(notification_label) and notification_label.text == message:
				notification_label.visible = false
		)

func show_hover_hint(kind: String, title: String) -> void:
	_hovered_key = "%s:%s" % [kind.to_lower(), title]
	notification_label.text = "PREVIEW · HOVER · %s · %s" % [kind.to_upper(), title]
	notification_label.visible = true

func clear_hover_hint(_key: String) -> void:
	_hovered_key = ""
	if _selected_key.is_empty():
		notification_label.text = "Select an object to see its contextual actions."
		notification_label.visible = true
	else:
		# Selection details live only in the Sector Contents panel. Do not put a
		# second selection summary over the Information panel/artwork boundary.
		notification_label.visible = false

func set_action_pending(pending: bool) -> void:
	if not is_instance_valid(action_button):
		return
	action_button.disabled = pending or not _can_warp_selected()
	action_button.text = "MOVING…" if pending else _warp_action_text()
	details_button.disabled = pending or _selected_key.is_empty()

func play_warp_transition() -> void:
	transition_cover.visible = true
	transition_cover.color = Color(0.003, 0.009, 0.018, 0.0)
	var tween := create_tween()
	tween.tween_property(transition_cover, "color", Color(0.003, 0.009, 0.018, 0.8), 0.19)
	await tween.finished
	await get_tree().create_timer(0.08).timeout
	tween = create_tween()
	tween.tween_property(transition_cover, "color", Color(0.003, 0.009, 0.018, 0.0), 0.2)
	await tween.finished
	transition_cover.visible = false

func _build() -> void:
	var base := ColorRect.new()
	base.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	base.color = Color(0.018, 0.032, 0.055)
	base.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(base)

	var margin := MarginContainer.new()
	margin.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	margin.add_theme_constant_override("margin_left", 18)
	margin.add_theme_constant_override("margin_right", 18)
	margin.add_theme_constant_override("margin_top", 14)
	margin.add_theme_constant_override("margin_bottom", 14)
	add_child(margin)
	var layout := VBoxContainer.new()
	layout.add_theme_constant_override("separation", 10)
	margin.add_child(layout)

	var top := PanelContainer.new()
	top.custom_minimum_size.y = 68
	top.add_theme_stylebox_override("panel", _panel_style(Color(0.012, 0.027, 0.048, 0.96), Color(0.27, 0.46, 0.52, 0.8)))
	layout.add_child(top)
	var top_margin := _inner_margin(top, 17, 10)
	top_row = HBoxContainer.new()
	top_row.add_theme_constant_override("separation", 24)
	top_margin.add_child(top_row)
	brand_label = _label("TRADE WARS", 23, Color(0.95, 0.92, 0.82))
	top_row.add_child(brand_label)
	var sector_box := VBoxContainer.new()
	sector_box.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	top_row.add_child(sector_box)
	sector_title = _label("SECTOR", 17, Color(0.41, 0.84, 0.84))
	sector_box.add_child(sector_title)
	sector_subtitle = _label("CURRENT LOCATION", 10, Color(0.59, 0.68, 0.73))
	sector_box.add_child(sector_subtitle)
	player_label = _label("CAPTAIN", 15, Color(0.88, 0.91, 0.89))
	top_row.add_child(player_label)
	credits_label = _label("— CR", 16, Color(0.94, 0.82, 0.6))
	credits_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	credits_label.custom_minimum_size.x = 150
	top_row.add_child(credits_label)
	turns_label = _label("— TURNS", 15, Color(0.79, 0.87, 0.87))
	turns_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	turns_label.custom_minimum_size.x = 125
	top_row.add_child(turns_label)

	gameplay_body = HBoxContainer.new()
	gameplay_body.size_flags_vertical = Control.SIZE_EXPAND_FILL
	gameplay_body.add_theme_constant_override("separation", 10)
	layout.add_child(gameplay_body)
	center_split = SplitContainer.new()
	center_split.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	center_split.size_flags_vertical = Control.SIZE_EXPAND_FILL
	center_split.split_offset = -370
	center_split.dragger_visibility = SplitContainer.DRAGGER_HIDDEN_COLLAPSED
	gameplay_body.add_child(center_split)
	sector_view = SectorViewScript.new()
	sector_view.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	sector_view.size_flags_vertical = Control.SIZE_EXPAND_FILL
	center_split.add_child(sector_view)
	sector_view.object_selected.connect(_on_art_object_selected)
	sector_view.warp_selected.connect(_on_art_warp_selected)
	sector_view.warp_activated.connect(request_warp_now)

	command_panel = PanelContainer.new()
	command_panel.custom_minimum_size.x = 340
	command_panel.add_theme_stylebox_override("panel", _panel_style(Color(0.014, 0.041, 0.063, 0.96), Color(0.28, 0.61, 0.66, 0.82)))
	center_split.add_child(command_panel)
	var panel_margin := _inner_margin(command_panel, 17, 15)
	panel_column = VBoxContainer.new()
	panel_column.add_theme_constant_override("separation", 12)
	panel_margin.add_child(panel_column)
	var panel_header := HBoxContainer.new()
	panel_column.add_child(panel_header)
	var panel_title := _label("SECTOR CONTENTS", 16, Color(0.48, 0.86, 0.84))
	panel_title.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	panel_header.add_child(panel_title)
	selection_hint = _label("No object selected", 11, Color(0.68, 0.77, 0.79))
	panel_column.add_child(selection_hint)
	var rule := ColorRect.new()
	rule.custom_minimum_size.y = 1
	rule.color = Color(0.32, 0.56, 0.6, 0.7)
	panel_column.add_child(rule)
	selection_panel = PanelContainer.new()
	selection_panel.add_theme_stylebox_override("panel", _panel_style(Color(0.026, 0.071, 0.093, 0.95), Color(0.22, 0.43, 0.48, 0.7)))
	panel_column.add_child(selection_panel)
	var selection_margin := _inner_margin(selection_panel, 12, 10)
	var selection_column := VBoxContainer.new()
	selection_column.add_theme_constant_override("separation", 6)
	selection_margin.add_child(selection_column)
	selection_title = _label("NO SELECTION", 13, Color(0.51, 0.82, 0.83))
	selection_column.add_child(selection_title)
	selection_detail = _label("Choose an illustrated object or a contents row.", 13, Color(0.85, 0.87, 0.84))
	selection_detail.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	selection_column.add_child(selection_detail)
	context_action_list = VBoxContainer.new()
	context_action_list.add_theme_constant_override("separation", 5)
	selection_column.add_child(context_action_list)
	action_button = null
	details_button = Button.new()
	details_button.text = "CLEAR SELECTION"
	details_button.custom_minimum_size.y = 31
	details_button.disabled = true
	details_button.pressed.connect(_on_view_details)
	_apply_button_style(details_button, false)
	selection_column.add_child(details_button)
	var note_title := _label("PRIVATE SECTOR NOTE", 10, Color(0.48, 0.78, 0.75))
	selection_column.add_child(note_title)
	_note_input = LineEdit.new()
	_note_input.placeholder_text = "e.g. Batiredigo sells slaves"
	_note_input.tooltip_text = "Stored locally on this client and never sent as a gameplay command."
	_note_input.max_length = 240
	selection_column.add_child(_note_input)
	var note_row := HBoxContainer.new()
	var save_note := Button.new()
	save_note.text = "SAVE NOTE"
	save_note.tooltip_text = "Save a private note for this sector"
	save_note.pressed.connect(_save_sector_note)
	note_row.add_child(save_note)
	var remove_note := Button.new()
	remove_note.text = "REMOVE"
	remove_note.tooltip_text = "Remove this private sector note"
	remove_note.pressed.connect(_remove_sector_note)
	note_row.add_child(remove_note)
	selection_column.add_child(note_row)
	_note_status = _label("Private · local only", 10, Color(0.54, 0.65, 0.67))
	selection_column.add_child(_note_status)
	contents_scroll = ScrollContainer.new()
	contents_scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	panel_column.add_child(contents_scroll)
	contents_list = VBoxContainer.new()
	contents_list.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	contents_list.add_theme_constant_override("separation", 5)
	contents_scroll.add_child(contents_list)
	command_menu = CommandMenuScript.new()
	command_menu.command_requested.connect(func(command: String, data: Dictionary, label: String, mutating: bool) -> void:
		command_requested.emit(command, data, label, mutating)
	)
	gameplay_body.add_child(command_menu)
	gameplay_body.move_child(command_menu, 0)
	_build_information_panel()
	gameplay_body.move_child(information_panel, 1)
	command_menu.open_for(_snapshot, {})
	port_workflow = PortWorkflowScript.new()
	port_workflow.trade_requested.connect(func(direction: String, port_id: int, sector_id: int, commodity: String, quantity: int) -> void:
		trade_requested.emit(direction, port_id, sector_id, commodity, quantity)
	)
	port_workflow.tavern_enter_requested.connect(func() -> void:
		tavern_enter_requested.emit()
	)
	port_workflow.tavern_entered.connect(func() -> void:
		command_menu.set_tavern_access(true)
		show_notification("Entered the StarDock tavern.")
	)
	port_workflow.tavern_exited.connect(func() -> void:
		command_menu.set_tavern_access(false)
		show_notification("Returned to the StarDock port.")
	)
	port_workflow.back_requested.connect(func() -> void:
		command_menu.set_tavern_access(false)
	)
	add_child(port_workflow)
	planet_workflow = PlanetWorkflowScript.new()
	planet_workflow.command_requested.connect(func(command: String, data: Dictionary, label: String, mutating: bool) -> void:
		command_requested.emit(command, data, label, mutating)
	)
	add_child(planet_workflow)
	trade_quote_dialog = ConfirmationDialog.new()
	trade_quote_dialog.title = "Confirm quoted trade"
	trade_quote_dialog.confirmed.connect(func() -> void: trade_confirmation.emit(true))
	trade_quote_dialog.canceled.connect(func() -> void: trade_confirmation.emit(false))
	add_child(trade_quote_dialog)

	var bottom := PanelContainer.new()
	bottom.custom_minimum_size.y = 48
	bottom.add_theme_stylebox_override("panel", _panel_style(Color(0.012, 0.027, 0.048, 0.96), Color(0.27, 0.46, 0.52, 0.8)))
	layout.add_child(bottom)
	var bottom_margin := _inner_margin(bottom, 17, 8)
	var bottom_row := HBoxContainer.new()
	bottom_row.add_theme_constant_override("separation", 30)
	bottom_margin.add_child(bottom_row)
	cargo_label = _label("CARGO  —/—", 13, Color(0.86, 0.88, 0.83))
	bottom_row.add_child(cargo_label)
	shields_label = _label("SHIELDS  —", 13, Color(0.76, 0.88, 0.86))
	bottom_row.add_child(shields_label)
	fighters_label = _label("FIGHTERS  —", 13, Color(0.91, 0.77, 0.57))
	bottom_row.add_child(fighters_label)
	freshness_label = _label("LINK UNKNOWN · STATE WAITING", 9, Color(0.51, 0.59, 0.62))
	freshness_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	freshness_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	bottom_row.add_child(freshness_label)

	toast_panel = PanelContainer.new()
	toast_panel.set_anchors_preset(Control.PRESET_BOTTOM_LEFT)
	toast_panel.position = Vector2(COMMAND_RAIL_TOAST_X, -79)
	toast_panel.grow_vertical = Control.GROW_DIRECTION_BEGIN
	toast_panel.custom_minimum_size = Vector2(390, 43)
	toast_panel.add_theme_stylebox_override("panel", _panel_style(Color(0.012, 0.04, 0.057, 0.94), Color(0.28, 0.72, 0.75, 0.8)))
	add_child(toast_panel)
	var toast_margin := _inner_margin(toast_panel, 13, 8)
	notification_label = _label("Select an object to see its contextual details.", 13, Color(0.91, 0.92, 0.87))
	notification_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	toast_margin.add_child(notification_label)
	_route_panel = PanelContainer.new()
	_route_panel.set_anchors_preset(Control.PRESET_TOP_WIDE)
	_route_panel.offset_left = COMMAND_RAIL_TOAST_X
	_route_panel.offset_top = 76
	_route_panel.offset_right = -20
	_route_panel.custom_minimum_size.y = 48
	_route_panel.add_theme_stylebox_override("panel", _panel_style(Color(0.04, 0.10, 0.10, 0.97), Color(0.84, 0.64, 0.3, 0.9)))
	_route_panel.visible = false
	add_child(_route_panel)
	var route_row := HBoxContainer.new()
	_route_panel.add_child(route_row)
	_route_label = _label("ROUTE", 12, Color(0.95, 0.88, 0.7))
	_route_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	route_row.add_child(_route_label)
	var engage := Button.new()
	engage.text = "ENGAGE AUTONAV"
	engage.tooltip_text = "Execute the confirmed route one server-confirmed warp at a time"
	engage.pressed.connect(func() -> void: route_engage_requested.emit())
	route_row.add_child(engage)
	var cancel := Button.new()
	cancel.text = "CANCEL ROUTE"
	cancel.tooltip_text = "Discard the plotted route without moving"
	cancel.pressed.connect(func() -> void: route_cancel_requested.emit())
	route_row.add_child(cancel)

	offline_overlay = PanelContainer.new()
	offline_overlay.set_anchors_preset(Control.PRESET_TOP_WIDE)
	offline_overlay.offset_left = 18
	offline_overlay.offset_top = 88
	offline_overlay.offset_right = -18
	offline_overlay.custom_minimum_size.y = 48
	offline_overlay.visible = false
	offline_overlay.mouse_filter = Control.MOUSE_FILTER_IGNORE
	offline_overlay.add_theme_stylebox_override("panel", _panel_style(Color(0.19, 0.09, 0.06, 0.92), Color(0.91, 0.64, 0.37, 0.95)))
	add_child(offline_overlay)
	var offline_margin := MarginContainer.new()
	offline_margin.name = "Margin"
	offline_margin.add_theme_constant_override("margin_left", 14)
	offline_margin.add_theme_constant_override("margin_right", 14)
	offline_overlay.add_child(offline_margin)
	var offline_row := HBoxContainer.new()
	offline_row.name = "Row"
	offline_margin.add_child(offline_row)
	var offline_status := _label("DISCONNECTED · showing last confirmed state", 13, Color(0.98, 0.88, 0.74))
	offline_status.name = "Status"
	offline_status.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	offline_row.add_child(offline_status)
	var reconnect := Button.new()
	reconnect.text = "RECONNECT"
	_apply_button_style(reconnect, true)
	reconnect.pressed.connect(func() -> void: reconnect_requested.emit())
	offline_row.add_child(reconnect)
	transition_cover = ColorRect.new()
	transition_cover.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	transition_cover.color = Color(0.003, 0.009, 0.018, 0.0)
	transition_cover.mouse_filter = Control.MOUSE_FILTER_IGNORE
	transition_cover.z_index = 40
	transition_cover.visible = false
	add_child(transition_cover)

func _render_snapshot(snapshot: Dictionary) -> void:
	if not is_instance_valid(sector_view):
		return
	var sector: Dictionary = snapshot.get("sector", {})
	var hud: Dictionary = snapshot.get("hud", {})
	var freshness: Dictionary = snapshot.get("freshness", {})
	var sector_for_display: Dictionary = sector.duplicate(true)
	var active_ship_id = hud.get("ship_id", null)
	if active_ship_id != null:
		var other_ships: Array = []
		for ship in sector.get("ships", []):
			if not (ship is Dictionary):
				continue
			var listed_ship_id = ship.get("id", ship.get("ship_id", null))
			if listed_ship_id == null or str(listed_ship_id) != str(active_ship_id):
				other_ships.append(ship)
		sector_for_display["ships"] = other_ships
	if not _planet_mode:
		sector_title.text = "SECTOR %s" % _display(hud.get("sector_id", sector.get("id")))
		if sector.get("name") != null and not str(sector.get("name")).is_empty():
			sector_title.text += "  ·  " + str(sector["name"]).to_upper()
		sector_subtitle.text = "CURRENT LOCATION"
	player_label.text = str(hud.get("player_name", "CAPTAIN")).to_upper()
	if hud.get("ship_name") != null:
		player_label.text += "  ·  " + str(hud["ship_name"]).to_upper()
	credits_label.text = "%s CR" % _format_integer(hud.get("credits"))
	turns_label.text = "%s TURNS" % _display(hud.get("turns_remaining"))
	cargo_label.text = "CARGO  %s/%s" % [_display(hud.get("cargo_used")), _display(hud.get("cargo_total"))]
	shields_label.text = "SHIELDS  %s" % _display(hud.get("shields"))
	fighters_label.text = "FIGHTERS  %s" % _display(hud.get("fighters"))
	var overall := str(freshness.get("overall", "unavailable")).to_upper()
	var connected := not bool(snapshot.get("disconnected", false)) and bool(snapshot.get("authenticated", false))
	freshness_label.text = "LINK %s · DATA %s" % ["ONLINE" if connected else "OFFLINE", overall]
	freshness_label.add_theme_color_override("font_color", Color(0.48, 0.59, 0.61) if overall == "FRESH" else _freshness_color(overall))
	sector_view.compose(sector_for_display, _selected_key)
	_build_contents(sector_for_display)
	if not _selected_key.is_empty() and not _rows.has(_selected_key):
		_selected_key = ""
		_render_selection({})
	_refresh_note_editor()
	if is_instance_valid(command_menu):
		command_menu.update_snapshot(snapshot)

func _build_contents(sector: Dictionary) -> void:
	for child in contents_list.get_children():
		child.queue_free()
	_rows.clear()
	var has_any := false
	for item in _ordered_entities(sector.get("ports", []), ["id", "port_id"]):
		if item is Dictionary:
			_add_entity_row("PORT", item, "port", ["id", "port_id"])
			has_any = true
	for item in _ordered_entities(sector.get("planets", []), ["id", "planet_id"]):
		if item is Dictionary:
			_add_entity_row("PLANET", item, "planet", ["id", "planet_id"])
			has_any = true
	for item in _ordered_entities(sector.get("ships", []), ["id", "ship_id"]):
		if item is Dictionary:
			_add_entity_row("SHIP", item, "ship", ["id", "ship_id"])
			has_any = true
	var counts: Dictionary = sector.get("counts", {})
	if int(counts.get("fighters", 0)) > 0:
		_add_static_row("DEFENCES", "%s fighters reported" % counts["fighters"])
		has_any = true
	if int(counts.get("mines", 0)) > 0:
		_add_static_row("HAZARD", "%s mines reported" % counts["mines"])
		has_any = true
	if sector.has("beacon") and not str(sector["beacon"]).strip_edges().is_empty():
		_add_static_row("BEACON", str(sector["beacon"]))
		has_any = true
	var destinations: Array = sector.get("adjacent_sector_ids", []).duplicate()
	destinations.sort()
	for destination in destinations:
		if typeof(destination) == TYPE_INT:
			_add_destination_row(destination)
			has_any = true
	if not has_any:
		var empty := _label("No ports, planets, ships, hazards, or adjacent sectors were reported.", 13, Color(0.68, 0.75, 0.78))
		empty.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
		contents_list.add_child(empty)

func _add_entity_row(kind_label: String, source: Dictionary, kind: String, id_fields: Array) -> void:
	var identity = null
	for field in id_fields:
		if source.has(field) and str(source[field]) != "":
			identity = source[field]
			break
	var name := str(source.get("name", source.get("ship_name", "Unnamed " + kind_label.to_lower())))
	if identity == null:
		_add_static_row(kind_label, name + " · selection unavailable")
		return
	var key := "%s:%s" % [kind, str(identity)]
	var row := _row_button(kind_label, name)
	row.pressed.connect(_on_row_selected.bind(key, "row"))
	row.focus_entered.connect(_on_row_focused.bind(key))
	row.gui_input.connect(_on_row_gui_input.bind(key))
	row.mouse_entered.connect(_on_row_hovered.bind(kind, name))
	row.mouse_exited.connect(_on_row_unhovered.bind(key))
	contents_list.add_child(row)
	_rows[key] = row
	_style_selected_row(key)

func _add_destination_row(destination: int) -> void:
	var key := "warp:%d" % destination
	var row := _row_button("WARP", "Sector %d" % destination)
	row.pressed.connect(_on_warp_selected.bind(key, destination))
	row.focus_entered.connect(_on_warp_focused.bind(key, destination))
	row.gui_input.connect(_on_warp_row_gui_input.bind(destination))
	row.mouse_entered.connect(_on_row_hovered.bind("WARP", "Sector %d" % destination))
	row.mouse_exited.connect(_on_row_unhovered.bind(key))
	contents_list.add_child(row)
	_rows[key] = row
	_style_selected_row(key)

func _add_static_row(kind_label: String, description: String) -> void:
	var line := HBoxContainer.new()
	var kind := _label(kind_label, 10, Color(0.42, 0.75, 0.77))
	kind.custom_minimum_size.x = 82
	line.add_child(kind)
	var value := _label(description, 13, Color(0.82, 0.86, 0.84))
	value.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	value.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	line.add_child(value)
	contents_list.add_child(line)

func _ordered_entities(value: Variant, id_fields: Array) -> Array:
	var ordered: Array = value.duplicate() if value is Array else []
	ordered.sort_custom(func(left: Dictionary, right: Dictionary) -> bool:
		var left_id = null
		var right_id = null
		for field in id_fields:
			if left.has(field):
				left_id = left[field]
				break
		for field in id_fields:
			if right.has(field):
				right_id = right[field]
				break
		if typeof(left_id) == TYPE_INT and typeof(right_id) == TYPE_INT:
			return left_id < right_id
		return str(left_id) < str(right_id)
	)
	return ordered

func _row_button(kind: String, title: String) -> Button:
	var row := Button.new()
	row.text = "   %s     %s" % [kind, title]
	row.alignment = HORIZONTAL_ALIGNMENT_LEFT
	row.custom_minimum_size.y = 44
	row.focus_mode = Control.FOCUS_ALL
	row.add_theme_font_size_override("font_size", 13)
	row.add_theme_color_override("font_color", Color(0.88, 0.91, 0.89))
	return row

func _style_selected_row(key: String) -> void:
	if not _rows.has(key):
		return
	var row: Button = _rows[key]
	var selected := key == _selected_key
	row.add_theme_stylebox_override("normal", _panel_style(Color(0.04, 0.15, 0.18, 0.96) if selected else Color(0.022, 0.06, 0.078, 0.86), Color(0.32, 0.86, 0.82, 0.95) if selected else Color(0.16, 0.33, 0.38, 0.75)))
	row.add_theme_stylebox_override("hover", _panel_style(Color(0.05, 0.18, 0.2, 0.98), Color(0.42, 0.84, 0.81, 0.9)))
	row.add_theme_stylebox_override("focus", _panel_style(Color(0.05, 0.18, 0.2, 0.98), Color(0.95, 0.71, 0.42, 1.0)))

func _on_row_selected(key: String, _source: String) -> void:
	_select(key, true)

func _on_row_focused(key: String) -> void:
	_select(key, false)

func _on_row_gui_input(event: InputEvent, key: String) -> void:
	if event is InputEventMouseButton and event.pressed and event.button_index == MOUSE_BUTTON_RIGHT:
		_select(key, true)

func _on_warp_row_gui_input(event: InputEvent, destination: int) -> void:
	if event is InputEventMouseButton and event.pressed and event.button_index == MOUSE_BUTTON_LEFT and event.double_click:
		request_warp_now(destination)
		get_viewport().set_input_as_handled()

func _on_row_hovered(kind: String, title: String) -> void:
	show_hover_hint(kind, title)

func _on_row_unhovered(key: String) -> void:
	clear_hover_hint(key)

func _on_warp_selected(key: String, destination: int) -> void:
	_select_warp(key, destination, true)

func _on_warp_focused(key: String, destination: int) -> void:
	_select_warp(key, destination, false)

func _select_warp(key: String, destination: int, notify: bool) -> void:
	_selected_key = key
	sector_view.select_warp(destination)
	_style_all_rows()
	var descriptor := {"kind": "warp", "name": "Sector %d" % destination, "key": key, "data": {"sector_id": destination}}
	_render_selection(descriptor)
	_set_selected_notification("WARP", "Sector %d" % destination)

func _select(key: String, notify: bool) -> void:
	_selected_key = key
	if key.begins_with("warp:"):
		_select_warp(key, int(key.trim_prefix("warp:")), notify)
		return
	sector_view.select_object(key, "row")
	_style_all_rows()
	_render_selection(sector_view.selected_object())
	var selected: Dictionary = sector_view.selected_object()
	notification_label.visible = false

func _on_art_object_selected(key: String, _source: String) -> void:
	_selected_key = key
	_style_all_rows()
	_render_selection(sector_view.selected_object())
	if _rows.has(key):
		_rows[key].grab_focus()
	notification_label.visible = false

func _set_selected_notification(kind: String, title: String) -> void:
	# Kept as a compatibility seam for callers; selection is rendered in the
	# right-hand Sector Contents panel, never in the floating notification bar.
	notification_label.visible = false

func _on_art_warp_selected(destination: int, _source: String) -> void:
	var key := "warp:%d" % destination
	_select_warp(key, destination, true)

func _style_all_rows() -> void:
	for key in _rows:
		_style_selected_row(str(key))

func _render_selection(object: Dictionary) -> void:
	for child in context_action_list.get_children():
		child.queue_free()
	context_action_buttons.clear()
	action_button = null
	if object.is_empty():
		selection_title.text = "NO SELECTION"
		selection_detail.text = "Choose an illustrated object or a contents row. Display positions have no gameplay meaning."
		selection_hint.text = "No object selected"
		_add_context_note("Select a sector object to see its available actions.")
		details_button.disabled = true
		return
	var kind := str(object.get("kind", "object"))
	selection_title.text = kind.to_upper()
	selection_detail.text = str(object.get("name", "Unnamed"))
	selection_hint.text = "Selected · %s · %s" % [kind.capitalize(), str(object.get("name", "Unnamed"))]
	details_button.disabled = false
	if kind == "warp":
		selection_detail.text += "\nAdjacent destination. Screen position is decorative."
		action_button = _add_context_button(_warp_action_text(), true, _on_context_action_pressed)
		action_button.disabled = not _can_warp_selected()
	else:
		var fields: Dictionary = object.get("data", {})
		var extras: Array[String] = []
		if fields.has("type"):
			extras.append("Type %s" % str(fields["type"]))
		if fields.has("owner"):
			extras.append("Owner %s" % str(fields["owner"]))
		if kind == "ship":
			var selected_id = fields.get("id", fields.get("ship_id", null))
			var current_id = _snapshot.get("hud", {}).get("ship_id", null)
			if selected_id != null and current_id != null and str(selected_id) == str(current_id):
				var current_ship: Dictionary = _snapshot.get("ship", {})
				var cargo: Array = current_ship.get("cargo", [])
				extras.append("Holds %s · Cargo %s" % [_display(current_ship.get("holds")), _cargo_summary(cargo)])
			else:
				extras.append("Cargo unavailable · sector.info does not return other ships' holds")
		if not extras.is_empty():
			selection_detail.text += "\n" + " · ".join(extras)
		_add_object_actions(kind, object)
	_apply_context_action_availability()
	_refresh_note_editor()

func _cargo_summary(cargo: Array) -> String:
	if cargo.is_empty():
		return "empty"
	var parts: Array[String] = []
	for item in cargo:
		if item is Dictionary:
			parts.append("%s × %s" % [str(item.get("commodity", "?")), str(item.get("quantity", 0))])
	return ", ".join(parts) if not parts.is_empty() else "empty"

func _current_sector_id() -> int:
	var value = _snapshot.get("hud", {}).get("sector_id", _snapshot.get("sector", {}).get("id", 0))
	return int(value) if value != null else 0

func _refresh_note_editor() -> void:
	if not is_instance_valid(_note_input) or _local_notes == null:
		return
	var sector_id := _current_sector_id()
	_note_input.text = _local_notes.get_note(sector_id) if sector_id > 0 else ""
	_note_status.text = "Private · local only" if sector_id > 0 else "No confirmed sector"

func _save_sector_note() -> void:
	var sector_id := _current_sector_id()
	if sector_id <= 0:
		show_notification("Cannot save a note without a confirmed sector ID.")
		return
	_local_notes.set_note(sector_id, _note_input.text)
	show_notification("Private note saved for Sector %d." % sector_id)

func _remove_sector_note() -> void:
	var sector_id := _current_sector_id()
	if sector_id <= 0:
		return
	_local_notes.remove_note(sector_id)
	_note_input.text = ""
	show_notification("Private note removed from Sector %d." % sector_id)

func _add_object_actions(kind: String, object: Dictionary) -> void:
	match kind:
		"port":
			var port_data: Dictionary = object.get("data", {})
			if port_data.has("id") or port_data.has("port_id"):
				_add_context_button("DOCK · OPEN PORT", true, _dispatch_context_action.bind({"label": "Dock · open port screen", "command": "port.info", "fixed": {}} , object))
			else:
				_add_context_unavailable("DOCK · PORT ID UNAVAILABLE", "The sector response did not include this port's ID, so the client cannot safely request port.info.")
		"planet":
			_add_context_button("PLANET INFORMATION", false, _dispatch_context_action.bind({"label": "Planet information", "command": "planet.info", "context_id": "planet_id"}, object))
			_add_context_button("LAND ON PLANET", true, _dispatch_context_action.bind({"label": "Land on planet", "command": "planet.land", "context_id": "planet_id", "mutating": true}, object))
		"ship":
			_add_context_button("ATTACK THIS SHIP", true, _dispatch_context_action.bind({"label": "Attack this ship", "command": "combat.attack", "context_id": "target_ship_id", "mutating": true}, object))
			_add_context_button("CLAIM THIS SHIP", false, _dispatch_context_action.bind({"label": "Claim ship", "command": "ship.claim", "context_id": "ship_id", "mutating": true}, object))
			_add_context_button("TOW THIS SHIP", false, _dispatch_context_action.bind({"label": "Tow ship", "command": "ship.tow", "context_id": "target_ship_id", "mutating": true}, object))
			_add_context_unavailable("INSPECT SHIP DETAILS · unavailable", "ship.inspect requires a ship ID in its schema, but the handler ignores it and returns sector-wide ships.")
			_add_context_unavailable("PRIVATE MESSAGE OWNER · unavailable", "The chat.send schema rejects the recipient fields required by its handler.")

func _add_context_button(label: String, primary: bool, callback: Callable) -> Button:
	var button := Button.new()
	button.text = label
	button.custom_minimum_size.y = 36
	button.focus_mode = Control.FOCUS_ALL
	_apply_button_style(button, primary)
	button.pressed.connect(callback)
	context_action_list.add_child(button)
	context_action_buttons.append(button)
	if not is_instance_valid(action_button):
		action_button = button
	return button

func _add_context_note(message: String) -> void:
	var note := _label(message, 11, Color(0.67, 0.75, 0.77))
	note.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	context_action_list.add_child(note)

func _add_context_unavailable(label: String, reason: String) -> void:
	var button := _add_context_button(label, false, _show_unavailable_context_action.bind(reason))
	button.disabled = true
	button.tooltip_text = reason
	button.set_meta("unavailable_reason", reason)

func _show_unavailable_context_action(reason: String) -> void:
	show_notification(reason)

func _dispatch_context_action(action: Dictionary, object: Dictionary) -> void:
	command_menu.dispatch_context_action(action, object)

func _apply_context_action_availability() -> void:
	var freshness: Dictionary = _snapshot.get("freshness", {})
	var available := bool(_snapshot.get("authenticated", false)) and not bool(_snapshot.get("disconnected", true)) and str(freshness.get("sector", "")) == "fresh"
	for button in context_action_buttons:
		var reason := str(button.get_meta("unavailable_reason", ""))
		button.disabled = not available or not reason.is_empty()
		if not reason.is_empty():
			button.tooltip_text = reason
		elif not available:
			button.tooltip_text = "Reconnect and refresh the current sector before acting."
			
func _selected_descriptor() -> Dictionary:
	if _selected_key.begins_with("warp:"):
		return {"kind": "warp", "name": "Sector %s" % _selected_key.trim_prefix("warp:")}
	return sector_view.selected_object()

func _warp_action_text() -> String:
	return "MOVE TO SECTOR %s" % _selected_key.trim_prefix("warp:") if _selected_key.begins_with("warp:") else "SELECT AN OBJECT"

func _can_warp_selected() -> bool:
	if not _selected_key.begins_with("warp:"):
		return false
	return _can_warp_destination(int(_selected_key.trim_prefix("warp:")))

func _can_warp_destination(_destination: int) -> bool:
	var freshness: Dictionary = _snapshot.get("freshness", {})
	return bool(_snapshot.get("authenticated", false)) and not bool(_snapshot.get("disconnected", true)) and str(freshness.get("sector", "")) == "fresh"

func _on_context_action_pressed() -> void:
	if _can_warp_selected():
		warp_requested.emit(int(_selected_key.trim_prefix("warp:")))

func request_warp_now(destination: int) -> void:
	if destination <= 0 or not _can_warp_destination(destination):
		show_notification("Warp unavailable · refresh the sector or reconnect before moving.")
		return
	_select_warp("warp:%d" % destination, destination, false)
	warp_activated.emit(destination)

func show_command_result(label: String, data: Dictionary) -> void:
	show_information(label, command_menu.format_result(label, data))

func show_information(title: String, content: String) -> void:
	if not is_instance_valid(information_panel):
		return
	information_title.text = title.to_upper()
	information_text.text = content if not content.strip_edges().is_empty() else "No information was returned."
	information_scroll.scroll_vertical = 0

func clear_information() -> void:
	show_information("Information", "No result yet. Read-only command results will appear here and will not interrupt the sector view.")

func _build_information_panel() -> void:
	information_panel = PanelContainer.new()
	information_panel.custom_minimum_size.x = 270
	information_panel.size_flags_vertical = Control.SIZE_EXPAND_FILL
	information_panel.add_theme_stylebox_override("panel", _panel_style(Color(0.012, 0.032, 0.049, 0.97), Color(0.55, 0.42, 0.25, 0.9)))
	gameplay_body.add_child(information_panel)
	var margin := _inner_margin(information_panel, 14, 14)
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 8)
	margin.add_child(column)
	var heading := HBoxContainer.new()
	column.add_child(heading)
	information_title = _label("INFORMATION", 16, Color(0.94, 0.78, 0.51))
	information_title.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	heading.add_child(information_title)
	var clear := Button.new()
	clear.text = "CLEAR"
	clear.tooltip_text = "Clear the current information result"
	clear.focus_mode = Control.FOCUS_ALL
	clear.pressed.connect(clear_information)
	heading.add_child(clear)
	var rule := ColorRect.new()
	rule.custom_minimum_size.y = 1
	rule.color = Color(0.55, 0.42, 0.25, 0.7)
	column.add_child(rule)
	information_scroll = ScrollContainer.new()
	information_scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	column.add_child(information_scroll)
	information_text = _label("No result yet. Read-only command results will appear here and will not interrupt the sector view.", 12, Color(0.84, 0.87, 0.83))
	information_text.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	information_text.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	information_scroll.add_child(information_text)

func show_planned_route(path: Array, destination: int) -> void:
	var ids: Array[String] = []
	for item in path:
		if typeof(item) == TYPE_INT or typeof(item) == TYPE_FLOAT:
			ids.append(str(int(item)))
	_route_label.text = "PLANNED ROUTE · DESTINATION %d · %s · No movement yet" % [destination, " → ".join(ids)]
	_route_panel.visible = not ids.is_empty()

func clear_planned_route() -> void:
	_route_panel.visible = false

func show_shipyard(data: Dictionary) -> void:
	var ship_state: Dictionary = _snapshot.get("ship", {})
	command_menu.show_shipyard(data, str(ship_state.get("name", "")))

func show_deployed_assets(data: Dictionary, asset_kind: String, sector_id: int) -> void:
	command_menu.show_deployed_assets(data, asset_kind, sector_id)

func set_command_busy(value: bool) -> void:
	command_menu.set_busy(value)
	if value:
		for button in context_action_buttons:
			button.disabled = true
	else:
		_apply_context_action_availability()
	port_workflow.set_busy(value)
	planet_workflow.set_busy(value)

func set_activity_history(entries: Array[String]) -> void:
	command_menu.set_activity_history(entries)

func select_object_key(key: String) -> void:
	if key.begins_with("warp:"):
		_select_warp(key, int(key.trim_prefix("warp:")), false)
	else:
		_select(key, false)

func enter_port_workflow(data: Dictionary) -> void:
	var sector_id = _snapshot.get("hud", {}).get("sector_id", null)
	if sector_id == null:
		show_notification("Port information arrived, but the current sector is unavailable.")
		return
	port_workflow.set_player_hold(_snapshot.get("ship", {}))
	port_workflow.show_port(data, int(sector_id))

func enter_planet_workflow(planet_id: int) -> void:
	_planet_mode = true
	sector_title.text = "PLANET SURFACE"
	sector_subtitle.text = "LANDED · CURRENT PLANET"
	planet_workflow.open_planet(planet_id)

func set_planet_information(data: Dictionary) -> void:
	planet_workflow.set_planet_info(data)

func set_planet_colonist_state(data: Dictionary) -> void:
	planet_workflow.set_colonist_state(data)

func leave_planet_workflow() -> void:
	_planet_mode = false
	planet_workflow.launch_confirmed(0)
	_render_snapshot(_snapshot)

func confirm_trade_quote(direction: String, commodity: String, quantity: int, quote: Dictionary) -> void:
	var total_key := "total_buy_price" if direction == "buy" else "total_sell_price"
	var unit_key := "buy_price" if direction == "buy" else "sell_price"
	var total = quote.get(total_key, null)
	var unit = quote.get(unit_key, null)
	trade_quote_dialog.dialog_text = "%s %d × %s\n\nAuthoritative total: %s CR\nUnit price: %s CR\n\nNo cargo or credits change unless you confirm." % [direction.capitalize(), quantity, commodity, _display_money(total), _display_money(unit)]
	trade_quote_dialog.ok_button_text = "CONFIRM %s" % direction.to_upper()
	trade_quote_dialog.cancel_button_text = "CANCEL"
	trade_quote_dialog.popup_centered(Vector2i(500, 250))

func _display_money(value) -> String:
	return "Unavailable" if value == null else str(value)

func _on_view_details() -> void:
	_selected_key = ""
	sector_view.selected_key = ""
	sector_view._update_highlight()
	_style_all_rows()
	_render_selection({})
	sector_view.grab_focus()
	show_notification("Selection cleared.")

func _unhandled_key_input(event: InputEvent) -> void:
	if not (event is InputEventKey) or not event.pressed or event.echo:
		return
	if event.keycode == KEY_ESCAPE:
		_selected_key = ""
		sector_view.selected_key = ""
		sector_view._update_highlight()
		_style_all_rows()
		_render_selection({})
		sector_view.grab_focus()
		get_viewport().set_input_as_handled()
	elif sector_view.has_focus() and event.keycode in [KEY_LEFT, KEY_UP, KEY_RIGHT, KEY_DOWN]:
		var selectable: Array = _rows.keys()
		if selectable.is_empty():
			return
		var current := selectable.find(_selected_key)
		var direction := -1 if event.keycode in [KEY_LEFT, KEY_UP] else 1
		current = (current + direction + selectable.size()) % selectable.size()
		var key := str(selectable[current])
		if key.begins_with("warp:"):
			_select_warp(key, int(key.trim_prefix("warp:")), false)
		else:
			_select(key, false)
		get_viewport().set_input_as_handled()

func _display(value) -> String:
	return "—" if value == null else str(value)

func _format_integer(value) -> String:
	if value == null:
		return "—"
	var digits := str(value)
	var result := ""
	while digits.length() > 3:
		result = "," + digits.substr(digits.length() - 3) + result
		digits = digits.substr(0, digits.length() - 3)
	return digits + result

func _freshness_color(value: String) -> Color:
	if value == "FRESH":
		return Color(0.43, 0.83, 0.7)
	if value in ["STALE", "DISCONNECTED", "PARTIALLY AVAILABLE"]:
		return Color(0.98, 0.69, 0.43)
	return Color(0.68, 0.74, 0.77)

func _label(text: String, font_size: int, color: Color) -> Label:
	var label := Label.new()
	label.text = text
	label.add_theme_font_size_override("font_size", font_size)
	label.add_theme_color_override("font_color", color)
	return label

func _inner_margin(parent: Control, horizontal: int, vertical: int) -> MarginContainer:
	var margin := MarginContainer.new()
	margin.add_theme_constant_override("margin_left", horizontal)
	margin.add_theme_constant_override("margin_right", horizontal)
	margin.add_theme_constant_override("margin_top", vertical)
	margin.add_theme_constant_override("margin_bottom", vertical)
	parent.add_child(margin)
	return margin

func _panel_style(fill: Color, border: Color) -> StyleBoxFlat:
	var style := StyleBoxFlat.new()
	style.bg_color = fill
	style.border_color = border
	style.set_border_width_all(1)
	style.set_corner_radius_all(7)
	style.content_margin_left = 7
	style.content_margin_right = 7
	style.content_margin_top = 5
	style.content_margin_bottom = 5
	return style

func _apply_button_style(button: Button, primary: bool) -> void:
	button.add_theme_color_override("font_color", Color(0.93, 0.94, 0.89))
	button.add_theme_color_override("font_disabled_color", Color(0.51, 0.59, 0.62))
	button.add_theme_stylebox_override("normal", _panel_style(Color(0.035, 0.13, 0.16, 0.97) if primary else Color(0.022, 0.06, 0.078, 0.9), Color(0.32, 0.82, 0.8, 0.86) if primary else Color(0.16, 0.33, 0.38, 0.75)))
	button.add_theme_stylebox_override("hover", _panel_style(Color(0.05, 0.2, 0.21, 0.99), Color(0.53, 0.9, 0.86, 1.0)))

func _update_responsive_layout() -> void:
	if not is_instance_valid(center_split):
		return
	center_split.vertical = size.x < 980
	if center_split.vertical:
		brand_label.visible = true
		brand_label.add_theme_font_size_override("font_size", 14)
		sector_subtitle.visible = false
		top_row.add_theme_constant_override("separation", 10)
		sector_title.add_theme_font_size_override("font_size", 14)
		player_label.add_theme_font_size_override("font_size", 12)
		credits_label.add_theme_font_size_override("font_size", 12)
		credits_label.custom_minimum_size.x = 94
		turns_label.add_theme_font_size_override("font_size", 12)
		turns_label.custom_minimum_size.x = 90
		center_split.split_offset = -60
		sector_view.custom_minimum_size = Vector2(0, 210)
		command_panel.custom_minimum_size = Vector2(0, 340)
		panel_column.move_child(contents_scroll, 3)
		selection_panel.custom_minimum_size.y = 0
		contents_scroll.custom_minimum_size.y = 78
		panel_column.add_theme_constant_override("separation", 7)
		selection_panel.visible = true
		context_action_list.visible = true
		details_button.visible = true
		toast_panel.visible = false
	else:
		brand_label.visible = true
		brand_label.add_theme_font_size_override("font_size", 23)
		sector_subtitle.visible = true
		top_row.add_theme_constant_override("separation", 24)
		sector_title.add_theme_font_size_override("font_size", 17)
		player_label.add_theme_font_size_override("font_size", 15)
		credits_label.add_theme_font_size_override("font_size", 16)
		credits_label.custom_minimum_size.x = 150
		turns_label.add_theme_font_size_override("font_size", 15)
		turns_label.custom_minimum_size.x = 125
		center_split.split_offset = -370
		sector_view.custom_minimum_size = Vector2(520, 0)
		command_panel.custom_minimum_size = Vector2(340, 0)
		panel_column.move_child(selection_panel, 3)
		contents_scroll.custom_minimum_size.y = 0
		panel_column.add_theme_constant_override("separation", 12)
		selection_panel.visible = true
		context_action_list.visible = true
		details_button.visible = true
		toast_panel.visible = true
