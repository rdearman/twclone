extends Control

signal reconnect_requested
signal warp_requested(destination: int)
signal warp_activated(destination: int)
signal command_requested(command: String, data: Dictionary, label: String, mutating: bool)
signal trade_requested(direction: String, port_id: int, sector_id: int, commodity: String, quantity: int, account: int)
signal trade_confirmation(accepted: bool)
signal tavern_enter_requested
signal route_engage_requested(mode: String)
signal route_cancel_requested

const SectorViewScript = preload("res://SectorView.gd")
const CommandMenuScript = preload("res://CommandMenu.gd")
const DialogLayout = preload("res://DialogLayout.gd")
const PortWorkflowScript = preload("res://PortWorkflow.gd")
const PlanetWorkflowScript = preload("res://PlanetWorkflow.gd")
const PlanetArt = preload("res://PlanetArt.gd")
const LocalSectorNotes = preload("res://LocalSectorNotes.gd")
const WARP_TRANSITION_SHADER := preload("res://WarpTransition.gdshader")
const LOCAL_PREFERENCES_PATH := "user://client_ui_preferences.cfg"

var sector_view
var sector_title: Label
var player_label: Label
var credits_label: Label
var turns_label: Label
var cargo_label: Label
var shields_label: Label
var hull_label: Label
var fighters_label: Label
var warp_links_label: Label
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
var trade_routes_dialog: AcceptDialog
var _trade_route_rows: VBoxContainer
var center_split: SplitContainer
var gameplay_body: BoxContainer
var workspace_stack: VBoxContainer
var workspace_controls: PanelContainer
var workspace_controls_column: VBoxContainer
var _workspace_catalog_open := false
var _has_information_result := false
var _navigation_destination_text := ""
var _navigation_route_plotted := false
var _navigation_action_buttons: Array[Button] = []
var _communications_view_name := "CHAT"
var _viewport_split_ratio := 0.60
var gameplay_scroll: ScrollContainer
var command_panel: PanelContainer
var information_panel: PanelContainer
var information_title: Label
var information_text: TextEdit
var information_scroll: ScrollContainer
var selection_panel: PanelContainer
var panel_column: VBoxContainer
var contents_scroll: ScrollContainer
var toast_panel: PanelContainer
var brand_label: Label
var sector_subtitle: Label
var top_row: BoxContainer
var bottom_row: HBoxContainer
var _snapshot: Dictionary = {}
var _selected_key := ""
var _rows: Dictionary = {}
var _hovered_key := ""
var _planet_mode := false
var _reduced_motion := false
var _hud_visible := {"cargo": true, "shields": true, "hull": true, "fighters": true}
var _hud_settings_dialog: ConfirmationDialog
var _settings_popup: PopupMenu
var _hud_setting_checks: Dictionary = {}
var _route_panel: PanelContainer
var _route_label: Label
var _local_notes
var _note_input: LineEdit
var _note_status: Label
var _note_content: VBoxContainer
var _note_toggle: Button
var primary_tabs: TabBar
var workspace_buttons: Array[Button] = []
var workspace_status: Label
var _holographic_scan_buttons: Array[Button] = []
var _active_workspace := "SECTOR"
const WORKSPACES := ["SECTOR", "NAVIGATION", "TRADING", "COMMUNICATIONS", "COMPUTER"]

func _ready() -> void:
	_local_notes = LocalSectorNotes.new()
	_load_local_preferences()
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
	_sync_holographic_scan_controls()

func set_connection_notice(disconnected: bool, message: String = "") -> void:
	offline_overlay.visible = disconnected
	if disconnected:
		var status: Label = offline_overlay.get_node("Margin/Row/Status")
		status.text = message if not message.is_empty() else "DISCONNECTED · showing last confirmed state"

func show_notification(message: String) -> void:
	notification_label.text = message
	notification_label.visible = not message.is_empty()
	toast_panel.visible = not message.is_empty() and (not is_instance_valid(_route_panel) or not _route_panel.visible)
	if is_instance_valid(port_workflow) and port_workflow.visible and not message.is_empty():
		port_workflow.show_trade_message(message)
	if not message.is_empty():
		var timer := get_tree().create_timer(4.0)
		timer.timeout.connect(func() -> void:
			if is_instance_valid(notification_label) and notification_label.text == message:
				notification_label.visible = false
				toast_panel.visible = false
		)

func show_hover_hint(kind: String, title: String) -> void:
	_hovered_key = "%s:%s" % [kind.to_lower(), title]
	notification_label.text = "PREVIEW · HOVER · %s · %s" % [kind.to_upper(), title]
	notification_label.visible = true
	toast_panel.visible = not is_instance_valid(_route_panel) or not _route_panel.visible

func clear_hover_hint(_key: String) -> void:
	_hovered_key = ""
	if _selected_key.is_empty():
		notification_label.text = "Select an object to see its contextual actions."
		notification_label.visible = true
		toast_panel.visible = not is_instance_valid(_route_panel) or not _route_panel.visible
	else:
		# Selection details live only in the Sector Contents panel. Do not put a
		# second selection summary over the Information panel/artwork boundary.
		notification_label.visible = false
		toast_panel.visible = false

func set_action_pending(pending: bool) -> void:
	if not is_instance_valid(action_button):
		return
	action_button.disabled = pending or not _can_warp_selected()
	action_button.text = "MOVING…" if pending else _warp_action_text()
	details_button.disabled = pending or _selected_key.is_empty()

func play_warp_transition() -> void:
	if _reduced_motion:
		return
	transition_cover.visible = true
	var material := transition_cover.material as ShaderMaterial
	material.set_shader_parameter("phase", 0.0)
	material.set_shader_parameter("strength", 0.0)
	var tween := create_tween()
	tween.tween_method(func(value: float) -> void: material.set_shader_parameter("phase", value), 0.0, 1.0, 0.58).set_trans(Tween.TRANS_SINE).set_ease(Tween.EASE_IN_OUT)
	tween.parallel().tween_method(func(value: float) -> void: material.set_shader_parameter("strength", value), 0.0, 1.0, 0.14)
	await tween.finished
	tween = create_tween()
	tween.tween_method(func(value: float) -> void: material.set_shader_parameter("strength", value), 1.0, 0.0, 0.18)
	await tween.finished
	transition_cover.visible = false

func _load_local_preferences() -> void:
	var config := ConfigFile.new()
	if config.load(LOCAL_PREFERENCES_PATH) == OK:
		_reduced_motion = bool(config.get_value("accessibility", "reduced_motion", false))
		var saved_split_ratio = config.get_value("layout", "viewport_split_ratio", 0.60)
		if (typeof(saved_split_ratio) == TYPE_FLOAT or typeof(saved_split_ratio) == TYPE_INT) and float(saved_split_ratio) >= 0.40 and float(saved_split_ratio) <= 0.80:
			_viewport_split_ratio = float(saved_split_ratio)
		var saved_hud = config.get_value("hud", "visible", {})
		if saved_hud is Dictionary:
			for key in _hud_visible:
				if saved_hud.has(key):
					_hud_visible[key] = bool(saved_hud[key])

func _toggle_reduced_motion() -> void:
	_reduced_motion = not _reduced_motion
	var config := ConfigFile.new()
	config.load(LOCAL_PREFERENCES_PATH)
	config.set_value("accessibility", "reduced_motion", _reduced_motion)
	config.save(LOCAL_PREFERENCES_PATH)
	command_menu.set_reduced_motion_enabled(_reduced_motion)
	show_notification("Reduced motion %s · warp transitions %s." % ["enabled" if _reduced_motion else "disabled", "skipped" if _reduced_motion else "animated"])

func _open_hud_settings() -> void:
	if not is_instance_valid(_hud_settings_dialog):
		_hud_settings_dialog = ConfirmationDialog.new()
		_hud_settings_dialog.title = "HUD status items"
		_hud_settings_dialog.ok_button_text = "SAVE"
		var content := VBoxContainer.new()
		content.add_theme_constant_override("separation", 8)
		var labels := {"cargo": "Cargo and hold warning", "shields": "Shields and low-shield warning", "hull": "Current ship hull", "fighters": "Fighter count"}
		for key in ["cargo", "shields", "hull", "fighters"]:
			var check := CheckBox.new()
			check.text = str(labels[key])
			check.button_pressed = bool(_hud_visible[key])
			_hud_setting_checks[key] = check
			content.add_child(check)
		DialogLayout.attach(_hud_settings_dialog, content)
		_hud_settings_dialog.confirmed.connect(_save_hud_settings)
		add_child(_hud_settings_dialog)
	else:
		for key in _hud_setting_checks:
			(_hud_setting_checks[key] as CheckBox).button_pressed = bool(_hud_visible[key])
	DialogLayout.popup(_hud_settings_dialog, Vector2i(500, 300))

func _open_settings_popup() -> void:
	# Settings live in the Computer/admin workspace rather than in a second
	# floating dialog. This keeps the gear action inside the normal tab layout.
	var computer_index := WORKSPACES.find("COMPUTER")
	if computer_index >= 0:
		_on_workspace_tab_changed(computer_index)
		_workspace_catalog_open = true
		_apply_workspace_layout()
		if is_instance_valid(command_menu):
			command_menu.open_settings_category()

func _on_settings_popup_id_pressed(id: int) -> void:
	match id:
		1:
			_open_hud_settings()
		2:
			_toggle_reduced_motion()
		4:
			_on_workspace_tab_changed(WORKSPACES.find("COMPUTER"))
			_workspace_catalog_open = true
			_apply_workspace_layout()
			command_menu.open_settings_category()
		3:
			if is_instance_valid(command_menu):
				command_menu.invoke_local_action("Log out to login screen")

func _save_hud_settings() -> void:
	for key in _hud_setting_checks:
		_hud_visible[key] = (_hud_setting_checks[key] as CheckBox).button_pressed
	var config := ConfigFile.new()
	config.load(LOCAL_PREFERENCES_PATH)
	config.set_value("hud", "visible", _hud_visible)
	config.save(LOCAL_PREFERENCES_PATH)
	_apply_hud_visibility()
	show_notification("HUD status items saved on this device.")

func _apply_hud_visibility() -> void:
	if is_instance_valid(cargo_label):
		cargo_label.visible = bool(_hud_visible.get("cargo", true))
	if is_instance_valid(shields_label):
		shields_label.visible = bool(_hud_visible.get("shields", true))
	if is_instance_valid(hull_label):
		hull_label.visible = bool(_hud_visible.get("hull", true))
	if is_instance_valid(fighters_label):
		fighters_label.visible = bool(_hud_visible.get("fighters", true))

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
	top_row = BoxContainer.new()
	top_row.vertical = false
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
	var settings_button := Button.new()
	settings_button.text = "⚙"
	settings_button.tooltip_text = "HUD and accessibility settings"
	settings_button.focus_mode = Control.FOCUS_ALL
	settings_button.pressed.connect(_open_settings_popup)
	_apply_button_style(settings_button, false)
	top_row.add_child(settings_button)
	var help_button := Button.new()
	help_button.text = "?"
	help_button.tooltip_text = "Show keyboard and command help"
	help_button.focus_mode = Control.FOCUS_ALL
	help_button.pressed.connect(func() -> void: show_notification("Alt+1–5 switches workspaces · Alt+D docks at the selected port · Alt+S density scan · Alt+H holographic scan when equipped · Esc clears selection · use the command shortcuts configured in Computer."))
	_apply_button_style(help_button, false)
	top_row.add_child(help_button)

	primary_tabs = TabBar.new()
	primary_tabs.custom_minimum_size.y = 42
	primary_tabs.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	for workspace in WORKSPACES:
		primary_tabs.add_tab(workspace.capitalize())
	primary_tabs.tab_changed.connect(_on_workspace_tab_changed)
	primary_tabs.current_tab = 0
	primary_tabs.add_theme_font_size_override("font_size", 14)
	primary_tabs.add_theme_color_override("font_selected_color", Color(0.92, 1.0, 0.98))
	primary_tabs.add_theme_color_override("font_unselected_color", Color(0.64, 0.76, 0.79))
	# Keep the native tab state for keyboard/state compatibility. Godot's
	# dynamically-created TabBar can lose its label rendering when adjacent
	# panels are hidden, so the visible presentation uses ordinary accessible
	# buttons bound to the same workspace state.
	primary_tabs.visible = false
	var workspace_row := HBoxContainer.new()
	workspace_row.custom_minimum_size.y = 42
	workspace_row.add_theme_constant_override("separation", 4)
	layout.add_child(workspace_row)
	var workspace_group := ButtonGroup.new()
	for index in WORKSPACES.size():
		var workspace_button := Button.new()
		workspace_button.text = WORKSPACES[index].capitalize()
		workspace_button.toggle_mode = true
		workspace_button.button_group = workspace_group
		workspace_button.focus_mode = Control.FOCUS_ALL
		workspace_button.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		workspace_button.pressed.connect(_on_workspace_button_pressed.bind(index))
		workspace_row.add_child(workspace_button)
		workspace_buttons.append(workspace_button)
	_sync_workspace_buttons()
	workspace_status = _label("SECTOR WORKSPACE · select an object or scan the current sector", 10, Color(0.48, 0.78, 0.78))
	# Context is retained for accessibility/state updates but no longer consumes
	# a separate row above the flexible viewport.
	workspace_status.visible = false

	gameplay_scroll = ScrollContainer.new()
	gameplay_scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	gameplay_scroll.custom_minimum_size.y = 260
	gameplay_scroll.clip_contents = true
	gameplay_scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	gameplay_scroll.vertical_scroll_mode = ScrollContainer.SCROLL_MODE_AUTO
	layout.add_child(gameplay_scroll)
	gameplay_body = BoxContainer.new()
	# This is the single flexible third row: artwork on the left and the active
	# contents/command surface on the right. Nothing is stacked below it.
	gameplay_body.vertical = false
	gameplay_body.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	gameplay_body.size_flags_vertical = Control.SIZE_EXPAND_FILL
	gameplay_scroll.add_child(gameplay_body)
	center_split = SplitContainer.new()
	center_split.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	center_split.size_flags_vertical = Control.SIZE_EXPAND_FILL
	center_split.split_offset = -370
	center_split.dragger_visibility = SplitContainer.DRAGGER_VISIBLE
	center_split.dragged.connect(_on_viewport_split_dragged)
	gameplay_body.add_child(center_split)
	sector_view = SectorViewScript.new()
	sector_view.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	sector_view.size_flags_vertical = Control.SIZE_EXPAND_FILL
	center_split.add_child(sector_view)
	sector_view.object_selected.connect(_on_art_object_selected)
	sector_view.object_activated.connect(_on_art_object_activated)
	sector_view.warp_selected.connect(_on_art_warp_selected)
	sector_view.warp_activated.connect(request_warp_now)
	workspace_stack = VBoxContainer.new()
	workspace_stack.name = "WorkspaceDrawer"
	workspace_stack.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	workspace_stack.size_flags_vertical = Control.SIZE_EXPAND_FILL
	workspace_stack.add_theme_constant_override("separation", 8)
	center_split.add_child(workspace_stack)

	command_panel = PanelContainer.new()
	command_panel.custom_minimum_size.x = 340
	command_panel.add_theme_stylebox_override("panel", _panel_style(Color(0.014, 0.041, 0.063, 0.96), Color(0.28, 0.61, 0.66, 0.82)))
	workspace_stack.add_child(command_panel)
	var panel_margin := _inner_margin(command_panel, 17, 15)
	panel_column = VBoxContainer.new()
	panel_column.add_theme_constant_override("separation", 12)
	panel_margin.add_child(panel_column)
	var panel_header := HBoxContainer.new()
	panel_column.add_child(panel_header)
	var panel_title := _label("SECTOR CONTENTS", 16, Color(0.48, 0.86, 0.84))
	panel_title.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	panel_header.add_child(panel_title)
	var sector_scan_button := _workspace_button("DENSITY SCAN", func() -> void:
		_emit_workspace_command("sector.scan.density", {}, "Sector density scan"), true)
	sector_scan_button.custom_minimum_size.y = 34
	panel_header.add_child(sector_scan_button)
	var sector_holo_button := _workspace_button("HOLOGRAPHIC SCAN", func() -> void:
		_emit_workspace_command("sector.scan", {"mode": "holographic"}, "Holographic adjacent-sector scan"))
	sector_holo_button.custom_minimum_size.y = 34
	sector_holo_button.visible = false
	_holographic_scan_buttons.append(sector_holo_button)
	panel_header.add_child(sector_holo_button)
	var sector_information_row := HBoxContainer.new()
	sector_information_row.add_theme_constant_override("separation", 6)
	sector_information_row.add_child(_workspace_button("PLAYER INFORMATION", func() -> void:
		_emit_workspace_command("player.my_info", {}, "Player information")))
	sector_information_row.add_child(_workspace_button("SHIP INFORMATION", func() -> void:
		_emit_workspace_command("ship.status", {}, "Ship information")))
	panel_column.add_child(sector_information_row)
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
	_note_toggle = Button.new()
	_note_toggle.text = "PRIVATE SECTOR NOTE  ▸"
	_note_toggle.alignment = HORIZONTAL_ALIGNMENT_LEFT
	_note_toggle.pressed.connect(_toggle_note_section)
	_apply_button_style(_note_toggle, false)
	selection_column.add_child(_note_toggle)
	_note_content = VBoxContainer.new()
	_note_content.add_theme_constant_override("separation", 5)
	_note_content.visible = false
	selection_column.add_child(_note_content)
	_note_input = LineEdit.new()
	_note_input.placeholder_text = "e.g. Batiredigo sells slaves"
	_note_input.tooltip_text = "Stored locally on this client and never sent as a gameplay command."
	_note_input.max_length = 240
	_note_content.add_child(_note_input)
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
	_note_content.add_child(note_row)
	_note_status = _label("Private · local only", 10, Color(0.54, 0.65, 0.67))
	_note_content.add_child(_note_status)
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
	command_menu.reduced_motion_toggle_requested.connect(_toggle_reduced_motion)
	command_menu.hud_settings_requested.connect(_open_hud_settings)
	command_menu.shortcut_binding_prompt.connect(show_notification)
	command_menu.set_reduced_motion_enabled(_reduced_motion)
	workspace_controls = PanelContainer.new()
	workspace_controls.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	workspace_controls.add_theme_stylebox_override("panel", _panel_style(Color(0.014, 0.041, 0.063, 0.96), Color(0.28, 0.61, 0.66, 0.82)))
	workspace_stack.add_child(workspace_controls)
	var controls_margin := _inner_margin(workspace_controls, 14, 12)
	workspace_controls_column = VBoxContainer.new()
	workspace_controls_column.add_theme_constant_override("separation", 8)
	controls_margin.add_child(workspace_controls_column)
	workspace_stack.add_child(command_menu)
	_build_information_panel()
	command_menu.open_for(_snapshot, {})
	_build_workspace_controls()
	_apply_workspace_layout()
	port_workflow = PortWorkflowScript.new()
	port_workflow.trade_requested.connect(func(direction: String, port_id: int, sector_id: int, commodity: String, quantity: int, account: int) -> void:
		trade_requested.emit(direction, port_id, sector_id, commodity, quantity, account)
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
	bottom_row = HBoxContainer.new()
	bottom_row.add_theme_constant_override("separation", 30)
	bottom_margin.add_child(bottom_row)
	cargo_label = _label("CARGO  —/—", 13, Color(0.86, 0.88, 0.83))
	bottom_row.add_child(cargo_label)
	shields_label = _label("SHIELDS  —", 13, Color(0.76, 0.88, 0.86))
	bottom_row.add_child(shields_label)
	hull_label = _label("HULL  —", 13, Color(0.76, 0.88, 0.86))
	bottom_row.add_child(hull_label)
	fighters_label = _label("FIGHTERS  —", 13, Color(0.91, 0.77, 0.57))
	bottom_row.add_child(fighters_label)
	warp_links_label = _label("WARP LINKS: —", 12, Color(0.55, 0.84, 0.84))
	warp_links_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	warp_links_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	bottom_row.add_child(warp_links_label)
	freshness_label = _label("LINK UNKNOWN · STATE WAITING", 9, Color(0.51, 0.59, 0.62))
	freshness_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	bottom_row.add_child(freshness_label)

	toast_panel = PanelContainer.new()
	toast_panel.anchor_left = 0.5
	toast_panel.anchor_right = 0.5
	toast_panel.anchor_top = 1.0
	toast_panel.anchor_bottom = 1.0
	toast_panel.offset_left = -195
	toast_panel.offset_top = -124
	toast_panel.offset_right = 195
	toast_panel.offset_bottom = -81
	toast_panel.grow_vertical = Control.GROW_DIRECTION_BEGIN
	toast_panel.custom_minimum_size = Vector2(390, 43)
	toast_panel.visible = false
	toast_panel.add_theme_stylebox_override("panel", _panel_style(Color(0.012, 0.04, 0.057, 0.94), Color(0.28, 0.72, 0.75, 0.8)))
	add_child(toast_panel)
	var toast_margin := _inner_margin(toast_panel, 13, 8)
	notification_label = _label("Select an object to see its contextual details.", 13, Color(0.91, 0.92, 0.87))
	notification_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	toast_margin.add_child(notification_label)
	_route_panel = PanelContainer.new()
	_route_panel.set_anchors_preset(Control.PRESET_BOTTOM_WIDE)
	_route_panel.offset_left = 18
	_route_panel.offset_top = -62
	_route_panel.offset_right = -18
	_route_panel.offset_bottom = -14
	_route_panel.custom_minimum_size.y = 48
	_route_panel.add_theme_stylebox_override("panel", _panel_style(Color(0.04, 0.10, 0.10, 0.97), Color(0.84, 0.64, 0.3, 0.9)))
	_route_panel.visible = false
	add_child(_route_panel)
	var route_row := HBoxContainer.new()
	_route_panel.add_child(route_row)
	_route_label = _label("ROUTE", 12, Color(0.95, 0.88, 0.7))
	_route_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_route_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	route_row.add_child(_route_label)

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
	transition_cover.color = Color.WHITE
	transition_cover.mouse_filter = Control.MOUSE_FILTER_IGNORE
	transition_cover.z_index = 40
	transition_cover.visible = false
	var transition_material := ShaderMaterial.new()
	transition_material.shader = WARP_TRANSITION_SHADER
	transition_cover.material = transition_material
	sector_view.add_child(transition_cover)

func _render_snapshot(snapshot: Dictionary) -> void:
	if not is_instance_valid(sector_view):
		return
	var sector: Dictionary = snapshot.get("sector", {})
	var hud: Dictionary = snapshot.get("hud", {})
	var freshness: Dictionary = snapshot.get("freshness", {})
	var sector_for_display: Dictionary = _sector_for_display(sector, hud)
	if not _planet_mode:
		sector_title.text = "SECTOR %s" % _display(hud.get("sector_id", sector.get("id")))
		if sector.get("name") != null and not str(sector.get("name")).is_empty():
			sector_title.text += "  ·  " + str(sector["name"]).to_upper()
		sector_subtitle.text = "CURRENT LOCATION"
	player_label.text = _display(hud.get("player_name")).to_upper()
	if hud.get("ship_name") != null:
		player_label.text += "  ·  " + str(hud["ship_name"]).to_upper()
	credits_label.text = "%s CR" % _format_integer(hud.get("credits"))
	turns_label.text = "%s TURNS" % _display(hud.get("turns_remaining"))
	cargo_label.text = "CARGO  %s/%s" % [_display(hud.get("cargo_used")), _display(hud.get("cargo_total"))]
	shields_label.text = "SHIELDS  %s" % _display(hud.get("shields"))
	hull_label.text = "HULL  %s" % _display(hud.get("hull"))
	fighters_label.text = "FIGHTERS  %s" % _display(hud.get("fighters"))
	var cargo_used = hud.get("cargo_used", null)
	var cargo_total = hud.get("cargo_total", null)
	var hold_full := cargo_used != null and cargo_total != null and float(cargo_total) > 0.0 and float(cargo_used) >= float(cargo_total)
	if hold_full:
		cargo_label.text = "FULL HOLD  %s/%s" % [_display(cargo_used), _display(cargo_total)]
		cargo_label.add_theme_color_override("font_color", Color(1.0, 0.67, 0.43))
	else:
		cargo_label.add_theme_color_override("font_color", Color(0.86, 0.88, 0.83))
	var shield_value = hud.get("shields", null)
	var low_shields := shield_value != null and float(shield_value) <= 25.0
	if low_shields:
		shields_label.text = "LOW SHIELDS  %s" % _display(shield_value)
		shields_label.add_theme_color_override("font_color", Color(1.0, 0.58, 0.44))
	else:
		shields_label.add_theme_color_override("font_color", Color(0.76, 0.88, 0.86))
	var overall := str(freshness.get("overall", "unavailable")).to_upper()
	var connected := not bool(snapshot.get("disconnected", false)) and bool(snapshot.get("authenticated", false))
	freshness_label.text = "LINK %s · DATA %s" % ["ONLINE" if connected else "OFFLINE", overall]
	freshness_label.add_theme_color_override("font_color", Color(0.48, 0.59, 0.61) if overall == "FRESH" else _freshness_color(overall))
	var warp_links: Array = []
	for link in sector.get("adjacent_sector_ids", []):
		if typeof(link) == TYPE_INT or (typeof(link) == TYPE_FLOAT and is_finite(link) and link == floor(link)):
			warp_links.append(str(int(link)))
	warp_links_label.text = "WARP LINKS: " + (" · ".join(warp_links) if not warp_links.is_empty() else "—")
	if is_instance_valid(workspace_status):
		workspace_status.text = _workspace_context_text()
	_apply_hud_visibility()
	sector_view.compose(sector_for_display, _selected_key)
	_build_contents(sector_for_display)
	if not _selected_key.is_empty() and not _rows.has(_selected_key):
		_selected_key = ""
		_render_selection({})
	_refresh_note_editor()
	if is_instance_valid(command_menu):
		command_menu.update_snapshot(snapshot)
		_apply_workspace_layout()

func _sector_for_display(sector: Dictionary, hud: Dictionary) -> Dictionary:
	"""Return the sector objects visible in the viewport.

	The server may include the authenticated player's active ship in
	`ships_present`.  That is authoritative state, but it is not a sector target:
	the player is inside that ship and it is already represented by the HUD.
	Only remove the exact active-ship ID; other owned ships and other players'
	ships remain visible and selectable.
	"""
	var result: Dictionary = sector.duplicate(true)
	var active_ship_id := _authoritative_object_id(hud, ["ship_id"])
	if active_ship_id <= 0:
		return result
	for field in ["ships", "ships_present"]:
		if not result.get(field) is Array:
			continue
		var visible: Array = []
		for item in result[field]:
			if not (item is Dictionary):
				visible.append(item)
				continue
			var object_id := _authoritative_object_id(item, ["id", "ship_id"])
			if object_id != active_ship_id:
				visible.append(item)
		result[field] = visible
	return result

func _build_contents(sector: Dictionary) -> void:
	for child in contents_list.get_children():
		child.queue_free()
	_rows.clear()
	var has_any := false
	for item in _ordered_entities(sector.get("ports", []), ["id", "port_id"]):
		if item is Dictionary:
			_add_entity_row("PORT", item, "port", ["id", "port_id"])
			has_any = true
	for item in _ordered_entities(sector.get("planets", []), ["planet_id", "id"]):
		if item is Dictionary:
			_add_entity_row("PLANET", item, "planet", ["planet_id", "id"])
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
	var knowledge: Dictionary = sector.get("knowledge", {})
	for item in [
		{"key": "ports", "label": "PORTS"},
		{"key": "planets", "label": "PLANETS"},
		{"key": "ships", "label": "SHIPS"},
		{"key": "adjacent", "label": "WARP LINKS"},
	]:
		if not bool(knowledge.get(item["key"], true)):
			_add_static_row(item["label"], "Not reported by the server")
			has_any = true
	if not has_any:
		var empty := _label("No ports, planets, ships, hazards, or adjacent sectors were found here.", 13, Color(0.68, 0.75, 0.78))
		empty.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
		contents_list.add_child(empty)

func _add_entity_row(kind_label: String, source: Dictionary, kind: String, id_fields: Array) -> void:
	var name := str(source.get("name", source.get("ship_name", "Unnamed " + kind_label.to_lower())))
	if kind == "planet":
		var planet_class := PlanetArt.class_for_planet(source)
		name += " · Class %s" % (planet_class if not planet_class.is_empty() else "unknown")
	# Row selection must use the same server-authoritative integer identity
	# validation as the action dispatcher.  A non-empty string, bool, or float
	# from a legacy alias is display metadata, not a safe request target.
	var identity := _authoritative_object_id(source, id_fields)
	if identity <= 0:
		_add_static_row(kind_label, name + " · selection unavailable")
		return
	var key := "%s:%d" % [kind, identity]
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
	toast_panel.visible = false

func _on_art_object_selected(key: String, _source: String) -> void:
	_selected_key = key
	_style_all_rows()
	_render_selection(sector_view.selected_object())
	if _rows.has(key):
		_rows[key].grab_focus()
	notification_label.visible = false
	toast_panel.visible = false

func _on_art_object_activated(key: String) -> void:
	_on_art_object_selected(key, "artwork")
	var object: Dictionary = sector_view.selected_object()
	var kind := str(object.get("kind", ""))
	if kind == "planet":
		var planet_data: Dictionary = object.get("data", {})
		var planet_id := _authoritative_object_id(planet_data, ["planet_id", "id"])
		if planet_id <= 0:
			show_notification("Landing is unavailable because the server did not provide a planet ID.")
			return
		_dispatch_context_action({"label": "Land on planet", "command": "planet.land", "context_id": "planet_id", "mutating": true}, object)
	elif kind == "port":
		var port_data: Dictionary = object.get("data", {})
		if _authoritative_object_id(port_data, ["port_id", "id"]) > 0:
			_dispatch_context_action({"label": "Dock · open port screen", "command": "port.info", "context_id": "port_id", "fixed": {}}, object)
		else:
			show_notification("Docking is unavailable because the server did not provide a valid port ID.")

func _set_selected_notification(kind: String, title: String) -> void:
	# Kept as a compatibility seam for callers; selection is rendered in the
	# right-hand Sector Contents panel, never in the floating notification bar.
	notification_label.visible = false
	toast_panel.visible = false

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
		var sector: Dictionary = _snapshot.get("sector", {})
		var sector_id := int(_snapshot.get("hud", {}).get("sector_id", sector.get("sector_id", 0)))
		var ports: Array = sector.get("ports", []) if sector.get("ports", []) is Array else []
		var planets: Array = sector.get("planets", []) if sector.get("planets", []) is Array else []
		var ships: Array = sector.get("ships_present", []) if sector.get("ships_present", []) is Array else []
		var links: Array = sector.get("adjacent_sector_ids", []) if sector.get("adjacent_sector_ids", []) is Array else []
		selection_title.text = "SECTOR %d" % sector_id if sector_id > 0 else "CURRENT SECTOR"
		selection_detail.text = "%d port%s · %d planet%s · %d ship%s · %d warp link%s" % [ports.size(), "" if ports.size() == 1 else "s", planets.size(), "" if planets.size() == 1 else "s", ships.size(), "" if ships.size() == 1 else "s", links.size(), "" if links.size() == 1 else "s"]
		selection_hint.text = "Select an object for available actions"
		_add_context_note("Sector objects and actions appear here when selected.")
		details_button.disabled = true
		return
	var kind := str(object.get("kind", "object"))
	selection_title.text = kind.to_upper()
	selection_detail.text = str(object.get("name", "Unnamed"))
	selection_hint.text = "Selected · %s · %s" % [kind.capitalize(), str(object.get("name", "Unnamed"))]
	details_button.disabled = false
	if kind == "warp":
		selection_detail.text += "\nAdjacent destination"
		action_button = _add_context_button(_warp_action_text(), true, _on_context_action_pressed)
		action_button.disabled = not _can_warp_selected()
	else:
		var fields: Dictionary = object.get("data", {})
		var extras: Array[String] = []
		if kind == "planet":
			var planet_class := PlanetArt.class_for_planet(fields)
			extras.append("Class %s" % (planet_class if not planet_class.is_empty() else "unknown"))
		if kind != "port" and fields.has("type"):
			extras.append("Type %s" % str(fields["type"]))
		if fields.has("owner"):
			extras.append("Owner %s" % str(fields["owner"]))
		if kind == "ship":
			var selected_id := _authoritative_object_id(fields, ["id", "ship_id"])
			var current_id := _authoritative_object_id(_snapshot.get("hud", {}), ["ship_id"])
			if selected_id > 0 and current_id > 0 and selected_id == current_id:
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
	# Local notes are keyed by the server-confirmed sector.  Do not coerce a
	# schema-mismatched string, float, or boolean into a different valid key.
	return int(value) if typeof(value) == TYPE_INT and int(value) > 0 else 0

func _refresh_note_editor() -> void:
	if not is_instance_valid(_note_input) or _local_notes == null:
		return
	var sector_id := _current_sector_id()
	_note_input.text = _local_notes.get_note(sector_id) if sector_id > 0 else ""
	_note_status.text = "Private · local only" if sector_id > 0 else "No confirmed sector"

func _toggle_note_section() -> void:
	if not is_instance_valid(_note_content):
		return
	_note_content.visible = not _note_content.visible
	_note_toggle.text = "PRIVATE SECTOR NOTE  ▾" if _note_content.visible else "PRIVATE SECTOR NOTE  ▸"

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
			if _authoritative_object_id(port_data, ["port_id", "id"]) > 0:
				_add_context_button("DOCK · OPEN PORT", true, _dispatch_context_action.bind({"label": "Dock · open port screen", "command": "port.info", "context_id": "port_id", "fixed": {}} , object))
			else:
				_add_context_unavailable("DOCK · PORT ID UNAVAILABLE", "The sector response did not include a valid port ID, so the client cannot safely request port.info.")
		"planet":
			var planet_data: Dictionary = object.get("data", {})
			var planet_id := _authoritative_object_id(planet_data, ["planet_id", "id"])
			if planet_id > 0:
				_add_context_button("PLANET INFORMATION", false, _dispatch_context_action.bind({"label": "Planet information", "command": "planet.info", "context_id": "planet_id"}, object))
				_add_context_button("LAND ON PLANET", true, _dispatch_context_action.bind({"label": "Land on planet", "command": "planet.land", "context_id": "planet_id", "mutating": true}, object))
			else:
				_add_context_unavailable("PLANET INFORMATION · ID UNAVAILABLE", "The sector response did not include this planet's ID, so planet information is unavailable.")
				_add_context_unavailable("LAND ON PLANET · ID UNAVAILABLE", "The sector response did not include this planet's ID, so landing is unavailable.")
		"ship":
			var ship_data: Dictionary = object.get("data", {})
			var target_ship_id := _authoritative_object_id(ship_data, ["target_ship_id", "ship_id", "id"])
			var active_ship_id := _authoritative_object_id(_snapshot.get("hud", {}), ["ship_id"])
			if target_ship_id > 0 and target_ship_id != active_ship_id:
				_add_context_button("ATTACK THIS SHIP", true, _dispatch_context_action.bind({"label": "Attack this ship", "command": "combat.attack", "context_id": "target_ship_id", "mutating": true}, object))
				_add_context_button("CLAIM THIS SHIP", false, _dispatch_context_action.bind({"label": "Claim ship", "command": "ship.claim", "context_id": "ship_id", "mutating": true}, object))
				_add_context_button("TOW THIS SHIP", false, _dispatch_context_action.bind({"label": "Tow ship", "command": "ship.tow", "context_id": "target_ship_id", "mutating": true}, object))
			elif target_ship_id <= 0:
				_add_context_unavailable("SHIP ACTIONS · ID UNAVAILABLE", "This ship has no server identifier; attack, claim and tow are unavailable.")
			else:
				_add_context_unavailable("ACTIVE SHIP · no target actions", "Your active ship is shown in the HUD and cannot be attacked, claimed or towed as a sector target.")
			if target_ship_id > 0:
				_add_context_button("INSPECT SHIP DETAILS", false, _dispatch_context_action.bind({"label": "Inspect ship details", "command": "ship.inspect", "context_id": "ship_id"}, object))
			else:
				_add_context_unavailable("INSPECT SHIP DETAILS · ID UNAVAILABLE", "The selected ship has no server identifier, so inspection is unavailable.")
			var owner_id := _authoritative_object_id(ship_data, ["owner_player_id", "owner_id", "player_id"])
			var owner_name := str(ship_data.get("owner_name", ship_data.get("owner", ""))).strip_edges()
			var recipient: Dictionary = {}
			if owner_id > 0:
				# The server owns player identity. Prefer its typed ID over a mutable
				# display name when constructing the documented chat.send request.
				recipient = {"to_id": owner_id}
			elif not owner_name.is_empty():
				# Name addressing remains the server-supported fallback for peers that
				# do not expose the owner's ID in the sector row.
				recipient = {"to_player": owner_name}
			if not recipient.is_empty():
				_add_context_button("PRIVATE MESSAGE OWNER", false, _dispatch_context_action.bind({"label": "Private message", "command": "chat.send", "fixed": recipient, "fields": [["message", "text", "Message", ""]], "mutating": true}, object))

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

func _authoritative_object_id(source: Dictionary, keys: Array) -> int:
	for key in keys:
		if not source.has(key):
			continue
		# Godot may decode a JSON integer as an integral float. Accept that wire
		# representation, but never coerce strings, booleans or fractional values.
		var raw: Variant = source[key]
		var valid: bool = (typeof(raw) == TYPE_INT and int(raw) > 0) or (typeof(raw) == TYPE_FLOAT and is_finite(raw) and raw > 0.0 and raw == floor(raw))
		if not valid:
			# An explicit but malformed identity must not fall through to a
			# legacy alias that could identify a different object. This includes
			# null: an explicit null canonical field is malformed metadata, not an
			# invitation to reinterpret another field as the object identity.
			return 0
		var candidate := int(raw)
		if candidate > 0:
			return candidate
	return 0

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
	_has_information_result = true
	# Keep the workspace uncluttered until a result exists, then reveal the
	# existing information surface in whichever viewport menu issued the command
	# (Sector, Navigation, or another workspace).
	workspace_stack.visible = true
	information_panel.visible = true
	information_title.text = title.to_upper()
	information_text.text = content if not content.strip_edges().is_empty() else "No information was returned."
	information_scroll.scroll_vertical = 0

func clear_information() -> void:
	_has_information_result = false
	show_information("Information", "No result yet. Read-only command results will appear here and will not interrupt the sector view.")

func _build_information_panel() -> void:
	information_panel = PanelContainer.new()
	information_panel.custom_minimum_size.y = 0
	information_panel.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	information_panel.size_flags_vertical = Control.SIZE_EXPAND_FILL
	information_panel.add_theme_stylebox_override("panel", _panel_style(Color(0.012, 0.032, 0.049, 0.97), Color(0.55, 0.42, 0.25, 0.9)))
	workspace_stack.add_child(information_panel)
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
	var collapse := Button.new()
	collapse.text = "COLLAPSE"
	collapse.tooltip_text = "Collapse or expand the activity/results drawer"
	collapse.pressed.connect(func() -> void:
		information_scroll.visible = not information_scroll.visible
		collapse.text = "EXPAND" if not information_scroll.visible else "COLLAPSE"
	)
	heading.add_child(collapse)
	var rule := ColorRect.new()
	rule.custom_minimum_size.y = 1
	rule.color = Color(0.55, 0.42, 0.25, 0.7)
	column.add_child(rule)
	information_scroll = ScrollContainer.new()
	information_scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	column.add_child(information_scroll)
	information_text = TextEdit.new()
	information_text.text = "No result yet. Read-only command results will appear here and will not interrupt the sector view."
	information_text.editable = false
	information_text.context_menu_enabled = true
	information_text.shortcut_keys_enabled = true
	information_text.wrap_mode = TextEdit.LINE_WRAPPING_BOUNDARY
	information_text.add_theme_color_override("font_color", Color(0.84, 0.87, 0.83))
	information_text.add_theme_font_size_override("font_size", 12)
	information_text.add_theme_stylebox_override("normal", StyleBoxEmpty.new())
	information_text.add_theme_stylebox_override("focus", StyleBoxEmpty.new())
	information_text.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	information_text.size_flags_vertical = Control.SIZE_EXPAND_FILL
	information_scroll.add_child(information_text)

func show_planned_route(path: Array, destination: int) -> void:
	var ids: Array[String] = []
	for item in path:
		if typeof(item) == TYPE_INT or typeof(item) == TYPE_FLOAT:
			ids.append(str(int(item)))
	_set_navigation_route_plotted(true)
	_route_label.text = "PLANNED ROUTE · DESTINATION %d · %s · Choose Express or Single Step" % [destination, " → ".join(ids)]
	_set_route_display(not ids.is_empty())

func show_restored_autopilot_route(path: Array, destination: int, state: String, mode: String) -> void:
	var ids: Array[String] = []
	for item in path:
		if typeof(item) == TYPE_INT or typeof(item) == TYPE_FLOAT:
			ids.append(str(int(item)))
	_set_navigation_route_plotted(true)
	_route_label.text = "SAVED ROUTE · %s · MODE %s · DESTINATION %d · %s · Review, then engage to move" % [state.to_upper(), mode.to_upper(), destination, " → ".join(ids)]
	_set_route_display(not ids.is_empty())

func set_autonav_status(status: String, stop_pending: bool = false) -> void:
	_route_label.text = status
	_set_route_display(true)

func clear_planned_route() -> void:
	_set_navigation_route_plotted(false)
	_set_route_display(false)

func show_single_step_ready(path: Array, destination: int, current_sector: int) -> void:
	var ids: Array[String] = []
	for item in path:
		if typeof(item) == TYPE_INT or typeof(item) == TYPE_FLOAT:
			ids.append(str(int(item)))
	_set_navigation_route_plotted(true)
	_route_label.text = "SINGLE STEP · Sector %d confirmed · next %s · destination %d" % [current_sector, str(ids[0]) if not ids.is_empty() else "none", destination]
	_set_route_display(not ids.is_empty())

func _set_route_display(active: bool) -> void:
	_route_panel.visible = active
	if not is_instance_valid(bottom_row):
		return
	for child in bottom_row.get_children():
		child.visible = not active
	if active:
		toast_panel.visible = false
	else:
		_apply_hud_visibility()
		if is_instance_valid(notification_label):
			toast_panel.visible = notification_label.visible

func _set_navigation_route_plotted(plotted: bool) -> void:
	_navigation_route_plotted = plotted
	for button in _navigation_action_buttons:
		if is_instance_valid(button):
			button.disabled = not plotted

func show_shipyard(data: Dictionary) -> void:
	var ship_state: Dictionary = _snapshot.get("ship", {})
	command_menu.show_shipyard(data, str(ship_state.get("name", "")))

func show_hardware_catalogue(data: Dictionary) -> void:
	command_menu.show_hardware_catalogue(data)

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
	var raw_sector_id: Variant = _snapshot.get("hud", {}).get("sector_id", null)
	# The trade surface must remain bound to the server-issued current-sector
	# identity. Godot decodes JSON integers as integral floats in some responses;
	# accept that lossless wire representation while still rejecting strings,
	# booleans and fractional values before a trade request crosses the boundary.
	var sector_id := _authoritative_object_id({"id": raw_sector_id}, ["id"])
	if sector_id <= 0:
		show_notification("Port information arrived, but the current sector is unavailable.")
		return
	port_workflow.set_player_hold(_snapshot.get("ship", {}))
	port_workflow.show_port(data, sector_id)

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

func confirm_trade_quote(direction: String, commodity: String, quantity: int, account: int, quote: Dictionary) -> void:
	var total_key := "total_buy_price" if direction == "buy" else "total_sell_price"
	var unit_key := "buy_price" if direction == "buy" else "sell_price"
	var total = quote.get(total_key, null)
	var unit = quote.get(unit_key, null)
	port_workflow.show_trade_quote(direction, commodity, quote)
	# Ordinary row trades commit the server-issued quote immediately. The server
	# may mark an exceptional quote for confirmation without changing the quote
	# or settlement protocol.
	if quote.get("requires_confirmation", false) != true:
		trade_quote_dialog.hide()
		trade_confirmation.emit(true)
		return
	var account_name := "Server preference" if account < 0 else ("Bank" if account == 1 else "Petty cash")
	trade_quote_dialog.dialog_text = "%s %d × %s\n\nQuoted goods value: %s CR\nUnit price: %s CR\nAccount: %s\n\nFees are not included. Prices may change before the trade is processed. No cargo or credits change unless you confirm." % [direction.capitalize(), quantity, commodity, _display_money(total), _display_money(unit), account_name]
	trade_quote_dialog.ok_button_text = "CONFIRM %s" % direction.to_upper()
	trade_quote_dialog.cancel_button_text = "CANCEL"
	DialogLayout.popup(trade_quote_dialog, Vector2i(520, 280))

func dismiss_trade_quote() -> void:
	trade_quote_dialog.hide()

func show_trade_routes(data: Dictionary) -> void:
	if not is_instance_valid(trade_routes_dialog):
		trade_routes_dialog = AcceptDialog.new()
		trade_routes_dialog.title = "Known Trade Routes"
		trade_routes_dialog.dialog_autowrap = true
		var scroll := ScrollContainer.new()
		scroll.custom_minimum_size = Vector2(620, 300)
		scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
		scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
		_trade_route_rows = VBoxContainer.new()
		_trade_route_rows.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		_trade_route_rows.add_theme_constant_override("separation", 8)
		scroll.add_child(_trade_route_rows)
		DialogLayout.attach(trade_routes_dialog, scroll)
		add_child(trade_routes_dialog)
	for child in _trade_route_rows.get_children():
		child.queue_free()
	var routes: Array = data.get("routes", []) if data.get("routes", []) is Array else []
	if routes.is_empty():
		var empty := Label.new()
		empty.text = "No matching known port pairs were found within those hop limits. Visit and inspect more ports to expand your personal trade data."
		DialogLayout.prepare_label(empty)
		_trade_route_rows.add_child(empty)
	else:
		for item in routes:
			if not (item is Dictionary):
				continue
			var route: Dictionary = item
			var approach_sector := int(route.get("approach_sector_id", route.get("sector_a_id", 0)))
			var commodity := str(route.get("commodity", ""))
			if commodity.is_empty():
				commodity = "Known commodity profile"
			var a_to_b := bool(route.get("a_to_b", false))
			var b_to_a := bool(route.get("b_to_a", false))
			var profit_ab := int(route.get("estimated_profit_a_to_b", 0))
			var profit_ba := int(route.get("estimated_profit_b_to_a", 0))
			var has_direction_data := route.has("a_to_b") or route.has("b_to_a")
			var has_price_data := route.has("estimated_profit_a_to_b") or route.has("estimated_profit_b_to_a")
			var directions: Array[String] = []
			if a_to_b:
				directions.append("A→B %s%d CR/unit gross" % ["+" if profit_ab >= 0 else "", profit_ab])
			if b_to_a:
				directions.append("B→A %s%d CR/unit gross" % ["+" if profit_ba >= 0 else "", profit_ba])
			if not has_direction_data:
				directions.append("trade in both directions" if bool(route.get("is_two_way", false)) else "one-way trade profile")
			var row := PanelContainer.new()
			row.add_theme_stylebox_override("panel", _panel_style(Color(0.016, 0.045, 0.061, 0.98), Color(0.20, 0.45, 0.51, 0.85)))
			var inner := MarginContainer.new()
			inner.add_theme_constant_override("margin_left", 10)
			inner.add_theme_constant_override("margin_right", 10)
			inner.add_theme_constant_override("margin_top", 8)
			inner.add_theme_constant_override("margin_bottom", 8)
			row.add_child(inner)
			var line := HBoxContainer.new()
			line.add_theme_constant_override("separation", 10)
			inner.add_child(line)
			var detail := Label.new()
			detail.size_flags_horizontal = Control.SIZE_EXPAND_FILL
			detail.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
			var sector_a := int(route.get("sector_a_id", 0))
			var sector_b := int(route.get("sector_b_id", 0))
			var name_a := str(route.get("port_a_name", "Port %d" % int(route.get("port_a_id", 0))))
			var name_b := str(route.get("port_b_name", "Port %d" % int(route.get("port_b_id", 0))))
			var direction_text := " · ".join(directions) if not directions.is_empty() else "No current matching direction"
			var price_note := "Gross price estimate only; excludes fees and can change before arrival." if has_price_data else "Price estimate unavailable from this server version."
			detail.text = "%s · %s (Sector %d) ↔ %s (Sector %d)\n%s · %d hops between · %d hops from you\n%s" % [commodity, name_a, sector_a, name_b, sector_b, direction_text, int(route.get("hops_between", 0)), int(route.get("hops_from_player", 0)), price_note]
			line.add_child(detail)
			var plot := Button.new()
			plot.text = "PLOT TO %d" % approach_sector
			plot.disabled = approach_sector <= 0
			plot.pressed.connect(func() -> void:
				trade_routes_dialog.hide()
				command_requested.emit("move.autopilot.start", {"to_sector_id": approach_sector}, "Plot route to recommended port", false)
			)
			line.add_child(plot)
			_trade_route_rows.add_child(row)
	var count := routes.size()
	trade_routes_dialog.dialog_text = "%d known trade candidate%s · choose a port to plot a route. Plotting does not move the ship." % [count, "" if count == 1 else "s"]
	DialogLayout.popup(trade_routes_dialog, Vector2i(820, 540))

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
	if event.alt_pressed and event.keycode >= KEY_1 and event.keycode <= KEY_5:
		primary_tabs.current_tab = event.keycode - KEY_1
		get_viewport().set_input_as_handled()
		return
	if command_menu.capture_shortcut(event):
		get_viewport().set_input_as_handled()
		return
	var focused := get_viewport().gui_get_focus_owner()
	if event.alt_pressed and event.keycode == KEY_D:
		if focused is LineEdit or focused is TextEdit:
			return
		if is_instance_valid(port_workflow) and port_workflow.visible:
			show_notification("Already docked at the current port.")
		else:
			var selected := _selected_descriptor()
			var port_data: Dictionary = selected.get("data", {}) if selected is Dictionary and str(selected.get("kind", "")) == "port" else {}
			var port_id := _authoritative_object_id(port_data, ["port_id", "id"])
			if port_id > 0:
				_emit_workspace_command("port.info", {"port_id": port_id}, "Dock · open port screen")
			else:
				show_notification("Select a port before docking.")
		get_viewport().set_input_as_handled()
		return
	if event.alt_pressed and event.keycode in [KEY_S, KEY_H]:
		if focused is LineEdit or focused is TextEdit:
			return
		if event.keycode == KEY_H:
			if _has_holographic_scanner():
				_emit_workspace_command("sector.scan", {"mode": "holographic"}, "Holographic adjacent-sector scan")
			else:
				show_notification("Holographic scan unavailable · install a long-range scanner.")
		else:
			_emit_workspace_command("sector.scan.density", {}, "Sector density scan")
		get_viewport().set_input_as_handled()
		return
	if focused is LineEdit or focused is TextEdit or event.ctrl_pressed or event.alt_pressed or event.meta_pressed:
		return
	if command_menu.dispatch_shortcut(event.keycode):
		get_viewport().set_input_as_handled()
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

func _on_workspace_tab_changed(index: int) -> void:
	if index < 0 or index >= WORKSPACES.size():
		return
	if is_instance_valid(primary_tabs) and primary_tabs.current_tab != index:
		primary_tabs.current_tab = index
	_active_workspace = WORKSPACES[index]
	# Computer and Communications open directly into their integrated command
	# surface. Trading and Navigation use their purpose-built controls and do
	# not expose a second generic command catalogue by default.
	_workspace_catalog_open = _active_workspace == "COMPUTER"
	if is_instance_valid(command_menu):
		command_menu.select_category(_active_workspace)
	_build_workspace_controls()
	_apply_workspace_layout()

func _apply_workspace_layout() -> void:
	if not is_instance_valid(command_menu) or not is_instance_valid(information_panel) or not is_instance_valid(workspace_stack) or not is_instance_valid(workspace_controls):
		return
	# Only Sector and Navigation are spatial workspaces. Trading, Communications,
	# and Computer are full-width terminal surfaces with no artwork column.
	var viewport_workspace := _active_workspace in ["SECTOR", "NAVIGATION"]
	var sector_workspace := _active_workspace == "SECTOR"
	center_split.visible = true
	sector_view.visible = viewport_workspace
	if viewport_workspace and not center_split.vertical:
		_set_viewport_split_from_ratio()
	else:
		center_split.split_offset = 0
	workspace_stack.visible = true
	var integrated_catalog_workspace := _active_workspace == "COMPUTER"
	command_panel.visible = sector_workspace
	workspace_controls.visible = not sector_workspace and not integrated_catalog_workspace
	command_menu.visible = not sector_workspace and _workspace_catalog_open
	information_panel.visible = not sector_workspace and (_workspace_catalog_open or _has_information_result)
	if viewport_workspace:
		gameplay_body.size_flags_vertical = Control.SIZE_EXPAND_FILL
		workspace_stack.size_flags_vertical = Control.SIZE_EXPAND_FILL
		workspace_controls.size_flags_vertical = Control.SIZE_EXPAND_FILL
	else:
		gameplay_body.size_flags_vertical = Control.SIZE_EXPAND_FILL

func _set_viewport_split_from_ratio() -> void:
	if not is_instance_valid(center_split) or center_split.vertical or center_split.size.x <= 0:
		return
	var offset := int(round(float(center_split.size.x) * _viewport_split_ratio))
	var minimum_left := int(sector_view.custom_minimum_size.x)
	var minimum_right := int(command_panel.custom_minimum_size.x)
	offset = clampi(offset, minimum_left, maxi(minimum_left, int(center_split.size.x) - minimum_right))
	center_split.split_offset = offset

func _on_viewport_split_dragged(offset: int) -> void:
	if not is_instance_valid(center_split) or center_split.vertical or center_split.size.x <= 0:
		return
	_viewport_split_ratio = clampf(float(offset) / float(center_split.size.x), 0.40, 0.80)
	var config := ConfigFile.new()
	config.load(LOCAL_PREFERENCES_PATH)
	config.set_value("layout", "viewport_split_ratio", _viewport_split_ratio)
	config.save(LOCAL_PREFERENCES_PATH)
	if is_instance_valid(information_panel):
		information_panel.size_flags_vertical = Control.SIZE_EXPAND_FILL
	if is_instance_valid(workspace_status):
		workspace_status.text = _workspace_context_text()
	_sync_workspace_buttons()

func _workspace_context_text() -> String:
	# JSON numeric fields may arrive as either integer, float, or string values.
	# Use the engine's String conversion here so workspace rendering cannot throw
	# during the first authoritative refresh and prevent the remaining HUD from
	# being reconciled.
	var sector_id := str(_snapshot.get("hud", {}).get("sector_id", 0)).to_int()
	match _active_workspace:
		"SECTOR":
			return "SECTOR %d · select an object or scan the current sector" % sector_id if sector_id > 0 else "SECTOR · waiting for confirmed location"
		"NAVIGATION":
			return "ROUTE READY · choose a destination and execution mode" if not is_instance_valid(_route_panel) or not _route_panel.visible else "ROUTE ACTIVE · follow confirmed movement state"
		"TRADING":
			var selected: Dictionary = sector_view.selected_object() if is_instance_valid(sector_view) else {}
			var port_name := str(selected.get("name", "no port selected")) if selected is Dictionary and str(selected.get("kind", "")) == "port" else "no port selected"
			return "PORT · %s" % port_name
		"COMMUNICATIONS":
			return "COMMUNICATIONS · review messages and send through server-backed forms"
		"COMPUTER":
			return "COMPUTER · information and scans for sector %d" % sector_id if sector_id > 0 else "COMPUTER · information and scans"
	return _active_workspace

func _workspace_button(label: String, callback: Callable, emphasized: bool = false) -> Button:
	var button := Button.new()
	button.text = label
	button.custom_minimum_size.y = 34
	button.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	button.focus_mode = Control.FOCUS_ALL
	button.pressed.connect(callback)
	_apply_button_style(button, emphasized)
	return button

func _workspace_heading(title: String, description: String) -> void:
	var heading := HBoxContainer.new()
	var title_label := _label(title, 15, Color(0.48, 0.86, 0.84))
	title_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	heading.add_child(title_label)
	workspace_controls_column.add_child(heading)
	var description_label := _label(description, 11, Color(0.65, 0.75, 0.77))
	description_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	workspace_controls_column.add_child(description_label)

func _emit_workspace_command(command: String, data: Dictionary = {}, label: String = "Command", mutating: bool = false) -> void:
	command_requested.emit(command, data, label, mutating)

func _has_holographic_scanner() -> bool:
	var ship_value: Variant = _snapshot.get("ship", {})
	return ship_value is Dictionary and ship_value.get("has_long_range_scanner", false) == true

func _sync_holographic_scan_controls() -> void:
	var available := _has_holographic_scanner()
	for button in _holographic_scan_buttons:
		if is_instance_valid(button):
			button.visible = available
	# Array.filter() returns an untyped Array in Godot 4. Assigning that result
	# to Array[Button] raises during every authoritative refresh.
	var live_buttons: Array[Button] = []
	for button in _holographic_scan_buttons:
		if is_instance_valid(button):
			live_buttons.append(button)
	_holographic_scan_buttons = live_buttons

func _build_workspace_controls() -> void:
	if not is_instance_valid(workspace_controls_column):
		return
	var existing_destination := workspace_controls_column.find_child("NavigationDestination", true, false)
	if existing_destination is LineEdit:
		_navigation_destination_text = (existing_destination as LineEdit).text
	for child in workspace_controls_column.get_children():
		child.queue_free()
	match _active_workspace:
		"NAVIGATION":
			_workspace_heading("NAVIGATION", "Plot a destination, then choose Express or Single Step. Every hop remains server-confirmed.")
			workspace_controls_column.add_child(_workspace_button("DENSITY SCAN", func() -> void:
				_emit_workspace_command("sector.scan.density", {}, "Sector density scan"), true))
			var navigation_holo_button := _workspace_button("HOLOGRAPHIC SCAN", func() -> void:
				_emit_workspace_command("sector.scan", {"mode": "holographic"}, "Holographic adjacent-sector scan"))
			navigation_holo_button.visible = false
			_holographic_scan_buttons.append(navigation_holo_button)
			workspace_controls_column.add_child(navigation_holo_button)
			var route_row := HBoxContainer.new()
			var destination := LineEdit.new()
			destination.name = "NavigationDestination"
			destination.placeholder_text = "ENTER DESTINATION SECTOR #"
			destination.tooltip_text = "Enter a valid sector number"
			destination.size_flags_horizontal = Control.SIZE_EXPAND_FILL
			destination.custom_minimum_size.y = 38
			destination.text = _navigation_destination_text
			destination.add_theme_color_override("font_color", Color(0.96, 0.9, 0.68))
			destination.add_theme_color_override("font_placeholder_color", Color(0.95, 0.72, 0.36, 0.95))
			destination.add_theme_font_size_override("font_size", 14)
			destination.add_theme_stylebox_override("normal", _panel_style(Color(0.08, 0.065, 0.025, 0.98), Color(0.95, 0.68, 0.25, 0.95)))
			destination.add_theme_stylebox_override("focus", _panel_style(Color(0.10, 0.075, 0.025, 1.0), Color(1.0, 0.84, 0.38, 1.0)))
			route_row.add_child(destination)
			var plot := _workspace_button("PLOT ROUTE", func() -> void:
				var value := int(destination.text.strip_edges()) if destination.text.strip_edges().is_valid_int() else 0
				if value <= 0:
					show_notification("Enter a valid destination sector.")
					return
				_emit_workspace_command("move.autopilot.start", {"to_sector_id": value}, "Plot route to sector", true)
			, true)
			route_row.add_child(plot)
			workspace_controls_column.add_child(route_row)
			var modes := HBoxContainer.new()
			_navigation_action_buttons.clear()
			var express := _workspace_button("EXPRESS", func() -> void: route_engage_requested.emit("express"), true)
			var single_step := _workspace_button("SINGLE STEP", func() -> void: route_engage_requested.emit("single_step"))
			var pause := _workspace_button("PAUSE", func() -> void: _emit_workspace_command("move.autopilot.control", {"action": "stop_at_next"}, "Pause autopilot", true))
			var resume := _workspace_button("RESUME", func() -> void: _emit_workspace_command("move.autopilot.control", {"action": "continue"}, "Resume autopilot", true))
			var cancel := _workspace_button("CANCEL", func() -> void: route_cancel_requested.emit())
			for button in [express, single_step, pause, resume, cancel]:
				modes.add_child(button)
				_navigation_action_buttons.append(button)
			workspace_controls_column.add_child(modes)
			var autopilot_status := _workspace_button("AUTOPILOT STATUS", func() -> void: _emit_workspace_command("move.autopilot.status", {}, "Autopilot status"))
			workspace_controls_column.add_child(autopilot_status)
			_navigation_action_buttons.append(autopilot_status)
			var update_navigation_state := func(value: String) -> void:
				var changed := value != _navigation_destination_text
				_navigation_destination_text = value
				if changed:
					_navigation_route_plotted = false
				var valid := value.strip_edges().is_valid_int() and int(value.strip_edges()) > 0
				destination.add_theme_color_override("font_color", Color(0.9, 0.98, 0.88) if valid else Color(1.0, 0.48, 0.36))
				plot.disabled = not valid
				for button in _navigation_action_buttons:
					button.disabled = not valid or not _navigation_route_plotted
			destination.text_changed.connect(update_navigation_state)
			update_navigation_state.call(destination.text)
		"TRADING":
			_workspace_heading("TRADING", "Select a port in the Sector workspace, then open its authoritative trading interface.")
			var selected: Dictionary = sector_view.selected_object() if is_instance_valid(sector_view) else {}
			var port_id := _authoritative_object_id(selected.get("data", {}), ["port_id", "id"]) if selected is Dictionary and str(selected.get("kind", "")) == "port" else 0
			var port_name := str(selected.get("name", "No port selected")) if selected is Dictionary and str(selected.get("kind", "")) == "port" else "No port selected"
			var port_label := _label("SELECTED PORT · %s" % port_name, 13, Color(0.9, 0.86, 0.72))
			workspace_controls_column.add_child(port_label)
			var dock := _workspace_button("DOCK · OPEN PORT", func() -> void:
				if port_id <= 0:
					show_notification("Select a port with an authoritative ID first.")
					return
				_emit_workspace_command("port.info", {"port_id": port_id}, "Dock · open port screen")
			, true)
			dock.disabled = port_id <= 0
			workspace_controls_column.add_child(dock)
			workspace_controls_column.add_child(_workspace_button("PORT INFORMATION", func() -> void:
				if port_id > 0:
					_emit_workspace_command("port.info", {"port_id": port_id}, "Port information")
				else:
					show_notification("Select a port with an authoritative ID first.")))
			workspace_controls_column.add_child(_workspace_button("BANK BALANCE", func() -> void: _emit_workspace_command("bank.balance", {}, "Bank balance")))
		"COMMUNICATIONS":
			_build_communications_workspace(_communications_view_name)
		"COMPUTER":
			_workspace_heading("COMPUTER", "Frequently used information, scans and records. The full command catalogue remains available below.")
			var computer := HBoxContainer.new()
			computer.add_child(_workspace_button("PLAYER INFO", func() -> void: _emit_workspace_command("player.my_info", {}, "Player information")))
			computer.add_child(_workspace_button("SHIP STATUS", func() -> void: _emit_workspace_command("ship.status", {}, "Ship information")))
			computer.add_child(_workspace_button("SECTOR INFO", func() -> void: _emit_workspace_command("sector.info", {}, "Current sector information")))
			computer.add_child(_workspace_button("DETAILED SCAN", func() -> void: _emit_workspace_command("sector.scan", {}, "Detailed current-sector scan")))
			computer.add_child(_workspace_button("HOLOGRAPHIC SCAN", func() -> void: _emit_workspace_command("sector.scan", {"mode": "holographic"}, "Holographic adjacent-sector scan")))
			workspace_controls_column.add_child(computer)
			workspace_controls_column.add_child(_workspace_button("OPEN FULL COMMAND CATALOGUE", func() -> void:
				_workspace_catalog_open = not _workspace_catalog_open
				_apply_workspace_layout(), true))

func _build_communications_workspace(view_name: String) -> void:
	_workspace_heading("COMMUNICATIONS", "Server-backed messages, mail, notices and player activity.")
	var navigation := HBoxContainer.new()
	for name in ["CHAT", "MAIL", "BROADCASTS", "NOTICES", "OFFERS", "BOUNTIES", "PLAYERS"]:
		var tab := _workspace_button(name, func() -> void:
			_communications_view_name = name
			_build_workspace_controls())
		tab.button_pressed = name == view_name
		navigation.add_child(tab)
	workspace_controls_column.add_child(navigation)
	var content := VBoxContainer.new()
	content.add_theme_constant_override("separation", 8)
	workspace_controls_column.add_child(content)
	match view_name:
		"CHAT":
			content.add_child(_label("CHAT HISTORY", 14, Color(0.9, 0.86, 0.72)))
			content.add_child(_comm_button("REFRESH CHAT", func() -> void: _emit_workspace_command("chat.history", {"limit": 50}, "Chat history")))
			content.add_child(_label("Compose a private message or broadcast below.", 11, Color(0.65, 0.75, 0.77)))
			var recipient := _comm_input("Recipient player name (private message)")
			var message := _comm_input("Message")
			content.add_child(recipient)
			content.add_child(message)
			var send_row := HBoxContainer.new()
			send_row.add_child(_comm_button("SEND PRIVATE", func() -> void:
				if recipient.text.strip_edges().is_empty() or message.text.strip_edges().is_empty():
					show_notification("Recipient and message are required.")
					return
				_emit_workspace_command("chat.send", {"to_player": recipient.text.strip_edges(), "message": message.text}, "Private message", true)))
			send_row.add_child(_comm_button("BROADCAST", func() -> void:
				if message.text.strip_edges().is_empty():
					show_notification("Message is required.")
					return
				_emit_workspace_command("chat.broadcast", {"message": message.text}, "Broadcast message", true)))
			content.add_child(send_row)
		"MAIL":
			content.add_child(_label("MAIL", 14, Color(0.9, 0.86, 0.72)))
			content.add_child(_comm_button("REFRESH INBOX", func() -> void: _emit_workspace_command("mail.inbox", {}, "Mail inbox")))
			var to := _comm_input("Recipient")
			var subject := _comm_input("Subject")
			var body := _comm_input("Message body")
			content.add_child(to); content.add_child(subject); content.add_child(body)
			content.add_child(_comm_button("SEND MAIL", func() -> void:
				if to.text.strip_edges().is_empty() or subject.text.strip_edges().is_empty() or body.text.strip_edges().is_empty():
					show_notification("Recipient, subject and message are required.")
					return
				_emit_workspace_command("mail.send", {"to": to.text.strip_edges(), "subject": subject.text.strip_edges(), "body": body.text}, "Send mail", true)))
		"BROADCASTS":
			content.add_child(_label("BROADCASTS", 14, Color(0.9, 0.86, 0.72)))
			content.add_child(_comm_button("REFRESH HISTORY", func() -> void: _emit_workspace_command("chat.history", {"limit": 50}, "Broadcast history")))
			var broadcast := _comm_input("Broadcast message")
			content.add_child(broadcast)
			content.add_child(_comm_button("SEND BROADCAST", func() -> void:
				if broadcast.text.strip_edges().is_empty():
					show_notification("Message is required.")
					return
				_emit_workspace_command("chat.broadcast", {"message": broadcast.text}, "Broadcast message", true)))
		"NOTICES":
			content.add_child(_label("NOTICES", 14, Color(0.9, 0.86, 0.72)))
			content.add_child(_comm_button("REFRESH NOTICES", func() -> void: _emit_workspace_command("notice.list", {}, "Notices")))
			var notice_id := _comm_input("Select a notice ID to acknowledge")
			content.add_child(notice_id)
			content.add_child(_comm_button("ACKNOWLEDGE SELECTED NOTICE", func() -> void:
				if not notice_id.text.is_valid_int() or int(notice_id.text) <= 0:
					show_notification("Choose a valid notice from the server results.")
					return
				_emit_workspace_command("notice.ack", {"notice_id": int(notice_id.text)}, "Acknowledge notice", true)))
		"OFFERS":
			content.add_child(_label("PLAYER OFFERS", 14, Color(0.9, 0.86, 0.72)))
			content.add_child(_label("Offer rows and IDs are displayed only from server responses.", 11, Color(0.65, 0.75, 0.77)))
			content.add_child(_comm_button("REFRESH OFFERS", func() -> void: _emit_workspace_command("trade.offers", {}, "Player offers")))
			var offer_id := _comm_input("Selected offer ID from server list")
			content.add_child(offer_id)
			var offer_actions := HBoxContainer.new()
			offer_actions.add_child(_comm_button("ACCEPT", func() -> void: _emit_workspace_command("trade.accept", {"offer_id": int(offer_id.text)}, "Accept player offer", true)))
			offer_actions.add_child(_comm_button("CANCEL", func() -> void: _emit_workspace_command("trade.cancel", {"offer_id": int(offer_id.text)}, "Cancel player offer", true)))
			content.add_child(offer_actions)
		"BOUNTIES":
			content.add_child(_label("BOUNTIES", 14, Color(0.9, 0.86, 0.72)))
			content.add_child(_comm_button("REFRESH BOUNTIES", func() -> void: _emit_workspace_command("bounty.list", {}, "Active bounties")))
		"PLAYERS":
			content.add_child(_label("ONLINE PLAYERS", 14, Color(0.9, 0.86, 0.72)))
			content.add_child(_comm_button("REFRESH PLAYERS", func() -> void: _emit_workspace_command("player.list_online", {}, "Online players")))

func _comm_input(placeholder: String) -> LineEdit:
	var input := LineEdit.new()
	input.placeholder_text = placeholder
	input.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	input.custom_minimum_size.y = 34
	return input

func _comm_button(label: String, callback: Callable) -> Button:
	return _workspace_button(label, callback, false)


func _on_workspace_button_pressed(index: int) -> void:
	if is_instance_valid(primary_tabs):
		primary_tabs.current_tab = index
	else:
		_on_workspace_tab_changed(index)

func _sync_workspace_buttons() -> void:
	for index in workspace_buttons.size():
		var button := workspace_buttons[index]
		button.button_pressed = index < WORKSPACES.size() and WORKSPACES[index] == _active_workspace
		_apply_button_style(button, button.button_pressed)

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
	bottom_row.add_theme_constant_override("separation", 8 if size.x < 600 else 30)
	if size.x < 600:
		var freshness: Dictionary = _snapshot.get("freshness", {})
		var overall := str(freshness.get("overall", "unavailable")).to_upper()
		var connected := not bool(_snapshot.get("disconnected", false)) and bool(_snapshot.get("authenticated", false))
		freshness_label.text = "%s · %s" % ["ONLINE" if connected else "OFFLINE", overall]
		freshness_label.add_theme_font_size_override("font_size", 8)
	else:
		var freshness: Dictionary = _snapshot.get("freshness", {})
		var overall := str(freshness.get("overall", "unavailable")).to_upper()
		var connected := not bool(_snapshot.get("disconnected", false)) and bool(_snapshot.get("authenticated", false))
		freshness_label.text = "LINK %s · DATA %s" % ["ONLINE" if connected else "OFFLINE", overall]
		freshness_label.add_theme_font_size_override("font_size", 9)
	if center_split.vertical:
		gameplay_body.vertical = false
		gameplay_body.size_flags_vertical = Control.SIZE_EXPAND_FILL
		gameplay_scroll.vertical_scroll_mode = ScrollContainer.SCROLL_MODE_AUTO
		gameplay_body.move_child(center_split, 0)
		top_row.vertical = true
		brand_label.visible = false
		sector_subtitle.visible = false
		top_row.add_theme_constant_override("separation", 3)
		sector_title.add_theme_font_size_override("font_size", 14)
		player_label.add_theme_font_size_override("font_size", 12)
		credits_label.add_theme_font_size_override("font_size", 12)
		credits_label.custom_minimum_size.x = 94
		turns_label.add_theme_font_size_override("font_size", 12)
		turns_label.custom_minimum_size.x = 90
		center_split.split_offset = -60
		sector_view.custom_minimum_size = Vector2(0, 210)
		command_panel.custom_minimum_size = Vector2(0, 300)
		panel_column.move_child(contents_scroll, 3)
		selection_panel.custom_minimum_size.y = 0
		contents_scroll.custom_minimum_size.y = 78
		panel_column.add_theme_constant_override("separation", 7)
		selection_panel.visible = true
		context_action_list.visible = true
		details_button.visible = true
		toast_panel.visible = false
	else:
		gameplay_body.vertical = false
		gameplay_body.size_flags_vertical = Control.SIZE_EXPAND_FILL
		gameplay_body.move_child(center_split, 0)
		top_row.vertical = false
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
		if _active_workspace in ["SECTOR", "NAVIGATION"]:
			_set_viewport_split_from_ratio()
		else:
			center_split.split_offset = 0
		sector_view.custom_minimum_size = Vector2(520, 0)
		command_panel.custom_minimum_size = Vector2(340, 0)
		panel_column.move_child(selection_panel, 3)
		contents_scroll.custom_minimum_size.y = 0
		panel_column.add_theme_constant_override("separation", 12)
		selection_panel.visible = true
		context_action_list.visible = true
		details_button.visible = true
		toast_panel.visible = notification_label.visible and not _route_panel.visible
