extends Control

const COMMAND_RAIL_CLEARANCE := 292.0

signal trade_requested(direction: String, port_id: int, sector_id: int, commodity: String, quantity: int)
signal back_requested
signal tavern_enter_requested
signal tavern_entered
signal tavern_exited

var _port: Dictionary = {}
var _sector_id := 0
var _trade_dialog: ConfirmationDialog
var _quantity: SpinBox
var _port_title: Label
var _commodity_list: VBoxContainer
var _pending_direction := ""
var _pending_commodity := ""
var _trade_buttons: Array[Dictionary] = []
var _back_button: Button
var _tavern_button: Button
var _description: Label
var _commodity_region: VBoxContainer
var _tavern_mode := false

func _ready() -> void:
	set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	z_index = 50
	mouse_filter = Control.MOUSE_FILTER_IGNORE
	visible = false
	var dim := ColorRect.new()
	dim.name = "WorkflowDim"
	dim.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	dim.offset_left = COMMAND_RAIL_CLEARANCE
	dim.offset_top = 88
	dim.offset_bottom = -62
	dim.color = Color(0.006, 0.014, 0.025, 0.985)
	dim.mouse_filter = Control.MOUSE_FILTER_STOP
	add_child(dim)
	var margin := MarginContainer.new()
	margin.name = "WorkflowMargin"
	margin.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	margin.offset_left = COMMAND_RAIL_CLEARANCE + 10.0
	margin.offset_top = 88
	margin.offset_bottom = -62
	margin.add_theme_constant_override("margin_left", 28)
	margin.add_theme_constant_override("margin_right", 28)
	margin.add_theme_constant_override("margin_top", 22)
	margin.add_theme_constant_override("margin_bottom", 22)
	add_child(margin)
	var panel := PanelContainer.new()
	panel.add_theme_stylebox_override("panel", _panel_style(Color(0.012, 0.033, 0.052, 0.99), Color(0.32, 0.66, 0.69, 0.95)))
	margin.add_child(panel)
	var inner := MarginContainer.new()
	inner.add_theme_constant_override("margin_left", 22)
	inner.add_theme_constant_override("margin_right", 22)
	inner.add_theme_constant_override("margin_top", 17)
	inner.add_theme_constant_override("margin_bottom", 17)
	panel.add_child(inner)
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 12)
	inner.add_child(column)
	var heading := HBoxContainer.new()
	heading.name = "Heading"
	column.add_child(heading)
	var title := Label.new()
	title.name = "Title"
	title.text = "PORT"
	title.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	title.add_theme_font_size_override("font_size", 23)
	title.add_theme_color_override("font_color", Color(0.95, 0.86, 0.66))
	heading.add_child(title)
	_port_title = title
	var back := Button.new()
	back.text = "BACK TO SECTOR"
	back.pressed.connect(func() -> void:
		if _tavern_mode:
			exit_tavern()
		else:
			visible = false
			back_requested.emit()
	)
	heading.add_child(back)
	_back_button = back
	_tavern_button = Button.new()
	_tavern_button.text = "ENTER TAVERN"
	_tavern_button.visible = false
	_tavern_button.pressed.connect(enter_tavern)
	heading.add_child(_tavern_button)
	var description := Label.new()
	description.name = "Description"
	description.text = "Authoritative port inventory · choose a commodity and request a quote before committing a trade."
	description.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	column.add_child(description)
	_description = description
	_commodity_region = VBoxContainer.new()
	_commodity_region.size_flags_vertical = Control.SIZE_EXPAND_FILL
	column.add_child(_commodity_region)
	var columns := HBoxContainer.new()
	columns.add_theme_constant_override("separation", 12)
	columns.add_child(_column_heading("COMMODITY", 180))
	columns.add_child(_column_heading("PORT STOCK / CAPACITY", 250))
	columns.add_child(_column_heading("INDICATIVE PRICE", 190))
	columns.add_child(_column_heading("TRADE", 150))
	_commodity_region.add_child(columns)
	var scroll := ScrollContainer.new()
	scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_commodity_region.add_child(scroll)
	var list := VBoxContainer.new()
	list.name = "Commodities"
	list.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	list.add_theme_constant_override("separation", 7)
	scroll.add_child(list)
	_commodity_list = list
	_trade_dialog = ConfirmationDialog.new()
	_trade_dialog.title = "Trade quantity"
	_trade_dialog.confirmed.connect(_submit_trade)
	var quantity_box := VBoxContainer.new()
	var prompt := Label.new()
	prompt.text = "Choose a positive quantity. The server quote is required before a trade can be confirmed."
	prompt.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	quantity_box.add_child(prompt)
	_quantity = SpinBox.new()
	_quantity.min_value = 1
	_quantity.max_value = 1000000
	_quantity.step = 1
	quantity_box.add_child(_quantity)
	_trade_dialog.add_child(quantity_box)
	_trade_dialog.ok_button_text = "REQUEST QUOTE"
	add_child(_trade_dialog)

func show_port(payload: Dictionary, sector_id: int) -> void:
	var port_data = payload.get("port", {})
	if not (port_data is Dictionary) or port_data.is_empty():
		return
	if _tavern_mode:
		_tavern_mode = false
		tavern_exited.emit()
	_port = port_data.duplicate(true)
	_sector_id = sector_id
	_port_title.text = str(_port.get("name", "PORT")).to_upper()
	_description.text = "Authoritative port inventory · choose a commodity and request a quote before committing a trade."
	_back_button.text = "BACK TO SECTOR"
	_tavern_button.visible = _is_stardock()
	_commodity_region.visible = true
	var list: VBoxContainer = _commodity_list
	_trade_buttons.clear()
	for child in list.get_children():
		child.queue_free()
	var commodities = _port.get("commodities", [])
	if not commodities is Array or commodities.is_empty():
		var empty := Label.new()
		empty.text = "No commodity inventory was reported by the port."
		list.add_child(empty)
	else:
		for item in commodities:
			if not item is Dictionary:
				continue
			_add_commodity_row(list, item)
	visible = true
	_trade_dialog.hide()

func enter_tavern() -> void:
	if not visible or not _is_stardock() or _tavern_mode:
		return
	tavern_enter_requested.emit()

func confirm_tavern_entered() -> void:
	if not visible or not _is_stardock() or _tavern_mode:
		return
	_tavern_mode = true
	_port_title.text = str(_port.get("name", "STARDOCK")).to_upper() + " · TAVERN"
	_description.text = "STARDOCK TAVERN · Local services and games are available while you are in this room."
	_back_button.text = "BACK TO PORT"
	_tavern_button.visible = false
	_commodity_region.visible = false
	tavern_entered.emit()

func exit_tavern() -> void:
	if not _tavern_mode:
		return
	_tavern_mode = false
	_port_title.text = str(_port.get("name", "PORT")).to_upper()
	_description.text = "Authoritative port inventory · choose a commodity and request a quote before committing a trade."
	_back_button.text = "BACK TO SECTOR"
	_tavern_button.visible = _is_stardock()
	_commodity_region.visible = true
	tavern_exited.emit()

func close_for_location_change() -> void:
	_tavern_mode = false
	visible = false
	_trade_dialog.hide()
	_port.clear()
	_sector_id = 0
	_tavern_button.visible = false

func _is_stardock() -> bool:
	return int(_port.get("type", -1)) == 9

func _add_commodity_row(list: VBoxContainer, item: Dictionary) -> void:
	var card := PanelContainer.new()
	card.custom_minimum_size.y = 58
	card.add_theme_stylebox_override("panel", _panel_style(Color(0.02, 0.055, 0.078, 0.98), Color(0.19, 0.38, 0.43, 0.8)))
	list.add_child(card)
	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", 12)
	card.add_child(row)
	var code := str(item.get("code", item.get("commodity", "UNKNOWN"))).to_upper()
	var stock = item.get("quantity", item.get("available", item.get("stock", null)))
	var capacity = item.get("max_quantity", null)
	var price: Variant = item.get("price", item.get("base_price", null))
	var name_label := Label.new()
	name_label.text = code
	name_label.custom_minimum_size.x = 180
	name_label.add_theme_font_size_override("font_size", 16)
	row.add_child(name_label)
	var stock_label := Label.new()
	stock_label.text = "%s / %s" % ["—" if stock == null else _format_amount(stock), "—" if capacity == null else _format_amount(capacity)]
	stock_label.custom_minimum_size.x = 250
	row.add_child(stock_label)
	var price_label := Label.new()
	price_label.text = "— CR" if price == null else "%s CR" % str(price)
	price_label.custom_minimum_size.x = 190
	row.add_child(price_label)
	var buy := Button.new()
	buy.text = "BUY"
	var buy_available: bool = stock != null and int(stock) > 0
	buy.disabled = not buy_available
	buy.pressed.connect(_choose_trade.bind("buy", code, item))
	row.add_child(buy)
	var sell := Button.new()
	sell.text = "SELL"
	var sell_available: bool = stock != null and capacity != null and int(stock) < int(capacity)
	sell.disabled = not sell_available
	sell.pressed.connect(_choose_trade.bind("sell", code, item))
	row.add_child(sell)
	_trade_buttons.append({"button": buy, "available": buy_available})
	_trade_buttons.append({"button": sell, "available": sell_available})

func set_busy(busy: bool) -> void:
	_tavern_button.disabled = busy or not _is_stardock() or _tavern_mode
	for entry in _trade_buttons:
		var button: Button = entry["button"]
		if is_instance_valid(button):
			button.disabled = busy or not bool(entry["available"])

func _column_heading(text: String, width: float) -> Label:
	var label := Label.new()
	label.text = text
	label.custom_minimum_size.x = width
	label.add_theme_font_size_override("font_size", 11)
	label.add_theme_color_override("font_color", Color(0.44, 0.77, 0.77))
	return label

func _choose_trade(direction: String, commodity: String, item: Dictionary) -> void:
	var port_id = _port.get("id", _port.get("port_id", null))
	if port_id == null or _sector_id <= 0:
		return
	_pending_direction = direction
	_pending_commodity = commodity
	_trade_dialog.title = "%s %s" % [direction.capitalize(), commodity]
	_quantity.value = 1
	var stock = item.get("quantity", item.get("available", item.get("stock", null)))
	var capacity = item.get("max_quantity", null)
	if direction == "buy" and stock != null and int(stock) > 0:
		_quantity.max_value = int(stock)
	elif direction == "sell" and stock != null and capacity != null and int(capacity) > int(stock):
		_quantity.max_value = int(capacity) - int(stock)
	else:
		_quantity.max_value = 1000000
	_trade_dialog.popup_centered(Vector2i(440, 220))

func _submit_trade() -> void:
	var port_id = _port.get("id", _port.get("port_id", null))
	if port_id == null or _sector_id <= 0 or _quantity.value < 1:
		return
	trade_requested.emit(_pending_direction, int(port_id), _sector_id, _pending_commodity, int(_quantity.value))

func _panel_style(fill: Color, border: Color) -> StyleBoxFlat:
	var style := StyleBoxFlat.new()
	style.bg_color = fill
	style.border_color = border
	style.set_border_width_all(1)
	style.set_corner_radius_all(8)
	return style

func _format_amount(value: Variant) -> String:
	if typeof(value) == TYPE_INT or (typeof(value) == TYPE_FLOAT and is_finite(value) and value == floor(value)):
		return str(int(value))
	return str(value)
