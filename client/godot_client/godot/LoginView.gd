extends Control

signal login_requested(host: String, port: int, username: String, password: String)
signal profiles_changed(profiles: Array, selected_index: int)

const BACKDROP := preload("res://assets/sector_backdrop.png")

var host_input: LineEdit
var port_input: LineEdit
var username_input: LineEdit
var password_input: LineEdit
var status_label: Label
var connect_button: Button
var profile_selector: OptionButton
var profile_name_input: LineEdit
var save_profile_button: Button
var remove_profile_button: Button
var _background: TextureRect
var _shade: ColorRect
var _profiles: Array[Dictionary] = []
var _selected_profile_index := -1
var _editing_profile_index := -1
var _loading_profile := false

func _ready() -> void:
	_build()

func set_status(message: String, busy: bool = false) -> void:
	status_label.text = message
	connect_button.disabled = busy
	connect_button.text = "CONNECTING…" if busy else "CONNECT"
	for input in [host_input, port_input, username_input, password_input, profile_name_input]:
		input.editable = not busy
	profile_selector.disabled = busy
	save_profile_button.disabled = busy
	remove_profile_button.disabled = busy or _selected_profile_index < 0

func set_connection_values(host: String, port: int, username: String) -> void:
	_loading_profile = true
	host_input.text = host
	port_input.text = str(port)
	username_input.text = username
	_loading_profile = false
	_sync_selected_profile()

func set_profiles(profiles: Array, selected_index: int = -1) -> void:
	_profiles.clear()
	for candidate in profiles:
		if not candidate is Dictionary:
			continue
		var name := str(candidate.get("name", "")).strip_edges()
		var host := str(candidate.get("host", "")).strip_edges()
		var port := int(candidate.get("port", 0))
		if name.is_empty() or host.is_empty() or port < 1 or port > 65535:
			continue
		_profiles.append({"name": name, "host": host, "port": port, "username": str(candidate.get("username", ""))})
	_refresh_profile_selector()
	if selected_index >= 0 and selected_index < _profiles.size():
		_apply_profile(selected_index)
	else:
		_selected_profile_index = -1
		_editing_profile_index = -1
		profile_selector.select(0)
		remove_profile_button.disabled = true

func get_selected_profile_index() -> int:
	return _selected_profile_index

func set_overlay_mode(enabled: bool) -> void:
	_background.visible = not enabled
	_shade.color = Color(0.012, 0.025, 0.045, 0.76) if enabled else Color(0.015, 0.027, 0.055, 0.58)

func _build() -> void:
	_background = TextureRect.new()
	_background.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	_background.texture = BACKDROP
	_background.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	_background.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_COVERED
	_background.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(_background)

	_shade = ColorRect.new()
	_shade.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	_shade.color = Color(0.015, 0.027, 0.055, 0.58)
	_shade.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(_shade)

	var center := CenterContainer.new()
	center.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	add_child(center)
	var card := PanelContainer.new()
	card.custom_minimum_size = Vector2(430, 0)
	card.add_theme_stylebox_override("panel", _panel_style(Color(0.025, 0.055, 0.085, 0.94), Color(0.24, 0.71, 0.77, 0.8)))
	center.add_child(card)
	var margin := MarginContainer.new()
	margin.add_theme_constant_override("margin_left", 30)
	margin.add_theme_constant_override("margin_right", 30)
	margin.add_theme_constant_override("margin_top", 27)
	margin.add_theme_constant_override("margin_bottom", 27)
	card.add_child(margin)
	var form := VBoxContainer.new()
	form.add_theme_constant_override("separation", 9)
	margin.add_child(form)
	var title := _label("TRADE WARS", 29, Color(0.94, 0.91, 0.82))
	form.add_child(title)
	form.add_child(_label("CONNECT TO YOUR UNIVERSE", 13, Color(0.37, 0.79, 0.81)))
	form.add_child(_label("SERVER PROFILE", 11, Color(0.68, 0.78, 0.8)))
	var profile_row := HBoxContainer.new()
	profile_row.add_theme_constant_override("separation", 8)
	form.add_child(profile_row)
	profile_selector = OptionButton.new()
	profile_selector.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	profile_selector.custom_minimum_size.y = 38
	profile_selector.add_theme_stylebox_override("normal", _panel_style(Color(0.018, 0.04, 0.063, 0.96), Color(0.19, 0.36, 0.42, 0.8)))
	profile_selector.add_theme_color_override("font_color", Color(0.93, 0.93, 0.89))
	profile_selector.item_selected.connect(_on_profile_selected)
	profile_row.add_child(profile_selector)
	remove_profile_button = _small_button("REMOVE")
	remove_profile_button.pressed.connect(_remove_profile)
	profile_row.add_child(remove_profile_button)
	var save_row := HBoxContainer.new()
	save_row.add_theme_constant_override("separation", 8)
	form.add_child(save_row)
	profile_name_input = _field("e.g. Local development")
	profile_name_input.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	profile_name_input.custom_minimum_size.y = 38
	save_row.add_child(profile_name_input)
	save_profile_button = _small_button("SAVE PROFILE")
	save_profile_button.pressed.connect(_save_profile)
	save_row.add_child(save_profile_button)
	form.add_child(_thin_rule())
	host_input = _field("127.0.0.1")
	port_input = _field("1234")
	username_input = _field("Captain name")
	password_input = _field("Password")
	password_input.secret = true
	form.add_child(_labeled_field("SERVER ADDRESS", host_input))
	form.add_child(_labeled_field("PORT", port_input))
	form.add_child(_labeled_field("USERNAME", username_input))
	form.add_child(_labeled_field("PASSWORD", password_input))
	host_input.text_changed.connect(_on_connection_field_changed)
	port_input.text_changed.connect(_on_connection_field_changed)
	username_input.text_changed.connect(_on_connection_field_changed)
	connect_button = Button.new()
	connect_button.text = "CONNECT"
	connect_button.custom_minimum_size.y = 48
	connect_button.add_theme_color_override("font_color", Color(0.96, 0.94, 0.87))
	connect_button.add_theme_stylebox_override("normal", _panel_style(Color(0.035, 0.19, 0.23, 0.98), Color(0.32, 0.83, 0.84, 0.9)))
	connect_button.add_theme_stylebox_override("hover", _panel_style(Color(0.05, 0.27, 0.3, 1.0), Color(0.55, 0.94, 0.9, 1.0)))
	connect_button.pressed.connect(_submit)
	form.add_child(connect_button)
	form.add_child(_label("Passwords are never saved in server profiles.", 11, Color(0.55, 0.66, 0.7)))
	status_label = _label("Enter your captain credentials to begin.", 13, Color(0.72, 0.78, 0.8))
	status_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	form.add_child(status_label)
	username_input.grab_focus.call_deferred()

func _field(placeholder: String) -> LineEdit:
	var field := LineEdit.new()
	field.placeholder_text = placeholder
	field.custom_minimum_size.y = 42
	field.add_theme_color_override("font_color", Color(0.93, 0.93, 0.89))
	field.add_theme_color_override("font_placeholder_color", Color(0.56, 0.62, 0.67))
	field.add_theme_stylebox_override("normal", _panel_style(Color(0.018, 0.04, 0.063, 0.96), Color(0.19, 0.36, 0.42, 0.8)))
	field.add_theme_stylebox_override("focus", _panel_style(Color(0.018, 0.04, 0.063, 0.98), Color(0.35, 0.81, 0.81, 1.0)))
	field.text_submitted.connect(func(_value: String) -> void: _submit())
	return field

func _labeled_field(caption: String, field: LineEdit) -> Control:
	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", 12)
	var label := _label(caption, 11, Color(0.72, 0.8, 0.81))
	label.custom_minimum_size.x = 112
	label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	row.add_child(label)
	field.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	row.add_child(field)
	return row

func _small_button(caption: String) -> Button:
	var button := Button.new()
	button.text = caption
	button.custom_minimum_size = Vector2(96, 38)
	button.add_theme_font_size_override("font_size", 11)
	button.add_theme_color_override("font_color", Color(0.9, 0.92, 0.88))
	button.add_theme_stylebox_override("normal", _panel_style(Color(0.022, 0.07, 0.09, 0.96), Color(0.2, 0.45, 0.5, 0.8)))
	button.add_theme_stylebox_override("hover", _panel_style(Color(0.035, 0.13, 0.15, 0.98), Color(0.35, 0.72, 0.74, 0.9)))
	return button

func _thin_rule() -> Control:
	var rule := HSeparator.new()
	rule.add_theme_color_override("separator_color", Color(0.24, 0.39, 0.43, 0.65))
	return rule

func _refresh_profile_selector() -> void:
	_loading_profile = true
	profile_selector.clear()
	profile_selector.add_item("Current connection · not saved", -1)
	for index in range(_profiles.size()):
		profile_selector.add_item(str(_profiles[index]["name"]), index)
	profile_selector.select(0)
	_loading_profile = false

func _on_profile_selected(item_index: int) -> void:
	if _loading_profile:
		return
	var profile_index := profile_selector.get_item_id(item_index)
	if profile_index < 0 or profile_index >= _profiles.size():
		_selected_profile_index = -1
		_editing_profile_index = -1
		profile_name_input.clear()
		remove_profile_button.disabled = true
		profiles_changed.emit(_profiles.duplicate(true), -1)
		return
	_apply_profile(profile_index)
	profiles_changed.emit(_profiles.duplicate(true), _selected_profile_index)

func _apply_profile(index: int) -> void:
	if index < 0 or index >= _profiles.size():
		return
	_selected_profile_index = index
	_editing_profile_index = index
	var profile: Dictionary = _profiles[index]
	_loading_profile = true
	profile_selector.select(index + 1)
	profile_name_input.text = str(profile.get("name", ""))
	host_input.text = str(profile.get("host", ""))
	port_input.text = str(profile.get("port", 1234))
	username_input.text = str(profile.get("username", ""))
	password_input.clear()
	_loading_profile = false
	remove_profile_button.disabled = false
	set_status("Loaded server profile · password required.")

func _sync_selected_profile() -> void:
	if _selected_profile_index < 0 or _selected_profile_index >= _profiles.size():
		return
	var profile: Dictionary = _profiles[_selected_profile_index]
	if str(profile.get("host", "")) != host_input.text or int(profile.get("port", 0)) != int(port_input.text) or str(profile.get("username", "")) != username_input.text:
		_selected_profile_index = -1
		profile_selector.select(0)
		remove_profile_button.disabled = true

func _on_connection_field_changed(_value: String = "") -> void:
	if _loading_profile:
		return
	_sync_selected_profile()

func _save_profile() -> void:
	var name := profile_name_input.text.strip_edges()
	var host := host_input.text.strip_edges()
	var port := int(port_input.text)
	if name.is_empty() or host.is_empty() or port < 1 or port > 65535:
		set_status("Enter a profile name, server address, and valid port before saving.")
		return
	var target_index := -1
	if _editing_profile_index >= 0 and _editing_profile_index < _profiles.size() and str(_profiles[_editing_profile_index].get("name", "")).to_lower() == name.to_lower():
		target_index = _editing_profile_index
	else:
		for index in range(_profiles.size()):
			if str(_profiles[index].get("name", "")).to_lower() == name.to_lower():
				set_status("That profile name already exists. Select it to edit, or choose another name.")
				return
	var record := {"name": name, "host": host, "port": port, "username": username_input.text.strip_edges()}
	if target_index < 0:
		_profiles.append(record)
		target_index = _profiles.size() - 1
	else:
		_profiles[target_index] = record
	_selected_profile_index = target_index
	_editing_profile_index = target_index
	_refresh_profile_selector()
	_apply_profile(target_index)
	profiles_changed.emit(_profiles.duplicate(true), _selected_profile_index)
	set_status("Server profile saved. Passwords are not stored.")

func _remove_profile() -> void:
	if _selected_profile_index < 0 or _selected_profile_index >= _profiles.size():
		return
	_profiles.remove_at(_selected_profile_index)
	_selected_profile_index = -1
	_editing_profile_index = -1
	_loading_profile = true
	profile_selector.select(0)
	profile_name_input.clear()
	_loading_profile = false
	remove_profile_button.disabled = true
	profiles_changed.emit(_profiles.duplicate(true), -1)
	set_status("Server profile removed. Current connection details were left in place.")

func _submit() -> void:
	var port_number := int(port_input.text)
	if host_input.text.strip_edges().is_empty() or port_number < 1 or port_number > 65535 or username_input.text.strip_edges().is_empty() or password_input.text.is_empty():
		set_status("Enter a server, valid port, captain name, and password.")
		return
	login_requested.emit(host_input.text.strip_edges(), port_number, username_input.text.strip_edges(), password_input.text)

func _label(text: String, size: int, color: Color) -> Label:
	var label := Label.new()
	label.text = text
	label.add_theme_font_size_override("font_size", size)
	label.add_theme_color_override("font_color", color)
	return label

func _panel_style(fill: Color, border: Color) -> StyleBoxFlat:
	var style := StyleBoxFlat.new()
	style.bg_color = fill
	style.border_color = border
	style.set_border_width_all(1)
	style.set_corner_radius_all(8)
	style.content_margin_left = 11
	style.content_margin_right = 11
	style.content_margin_top = 7
	style.content_margin_bottom = 7
	return style
