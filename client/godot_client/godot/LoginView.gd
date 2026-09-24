extends Control

signal login_requested(host: String, port: int, username: String, password: String)

const BACKDROP := preload("res://assets/sector_backdrop.png")

var host_input: LineEdit
var port_input: LineEdit
var username_input: LineEdit
var password_input: LineEdit
var status_label: Label
var connect_button: Button
var _background: TextureRect
var _shade: ColorRect

func _ready() -> void:
	_build()

func set_status(message: String, busy: bool = false) -> void:
	status_label.text = message
	connect_button.disabled = busy
	connect_button.text = "CONNECTING…" if busy else "CONNECT"
	for input in [host_input, port_input, username_input, password_input]:
		input.editable = not busy

func set_connection_values(host: String, port: int, username: String) -> void:
	host_input.text = host
	port_input.text = str(port)
	username_input.text = username

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
	form.add_theme_constant_override("separation", 13)
	margin.add_child(form)
	var title := _label("TRADE WARS", 29, Color(0.94, 0.91, 0.82))
	form.add_child(title)
	form.add_child(_label("CONNECT TO YOUR UNIVERSE", 13, Color(0.37, 0.79, 0.81)))
	host_input = _field("Server address", "127.0.0.1")
	port_input = _field("Server port", "1234")
	username_input = _field("Captain name", "")
	password_input = _field("Password", "")
	password_input.secret = true
	form.add_child(host_input)
	form.add_child(port_input)
	form.add_child(username_input)
	form.add_child(password_input)
	connect_button = Button.new()
	connect_button.text = "CONNECT"
	connect_button.custom_minimum_size.y = 48
	connect_button.add_theme_color_override("font_color", Color(0.96, 0.94, 0.87))
	connect_button.add_theme_stylebox_override("normal", _panel_style(Color(0.035, 0.19, 0.23, 0.98), Color(0.32, 0.83, 0.84, 0.9)))
	connect_button.add_theme_stylebox_override("hover", _panel_style(Color(0.05, 0.27, 0.3, 1.0), Color(0.55, 0.94, 0.9, 1.0)))
	connect_button.pressed.connect(_submit)
	form.add_child(connect_button)
	status_label = _label("Enter your captain credentials to begin.", 13, Color(0.72, 0.78, 0.8))
	status_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	form.add_child(status_label)
	username_input.grab_focus.call_deferred()

func _field(caption: String, placeholder: String) -> LineEdit:
	var field := LineEdit.new()
	field.placeholder_text = placeholder
	field.custom_minimum_size.y = 42
	field.add_theme_color_override("font_color", Color(0.93, 0.93, 0.89))
	field.add_theme_color_override("font_placeholder_color", Color(0.56, 0.62, 0.67))
	field.add_theme_stylebox_override("normal", _panel_style(Color(0.018, 0.04, 0.063, 0.96), Color(0.19, 0.36, 0.42, 0.8)))
	field.add_theme_stylebox_override("focus", _panel_style(Color(0.018, 0.04, 0.063, 0.98), Color(0.35, 0.81, 0.81, 1.0)))
	field.tooltip_text = caption
	field.text_submitted.connect(func(_value: String) -> void: _submit())
	return field

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
