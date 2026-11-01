extends Control

const COMMAND_RAIL_CLEARANCE := 292.0
const DialogLayout = preload("res://DialogLayout.gd")
const AssetCatalog = preload("res://AssetCatalog.gd")

signal trade_requested(direction: String, port_id: int, sector_id: int, commodity: String, quantity: int, account: int)
signal back_requested
signal tavern_enter_requested
signal tavern_entered
signal tavern_exited

var _port: Dictionary = {}
var _sector_id := 0
var _trade_dialog: ConfirmationDialog
var _quantity: SpinBox
var _trade_account: OptionButton
var _port_title: Label
var _port_artwork: TextureRect
var _commodity_list: VBoxContainer
var _pending_direction := ""
var _pending_commodity := ""
var _trade_buttons: Array[Dictionary] = []
var _back_button: Button
var _tavern_button: Button
var _description: Label
var _commodity_region: VBoxContainer
var _tavern_mode := false
var _hold_label: Label
var _player_ship: Dictionary = {}
var _dim: ColorRect
var _workflow_margin: MarginContainer
var _panel_inner: MarginContainer
var _heading: BoxContainer
var _commodity_headers: Control
var _compact_layout := false

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
	_dim = dim
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
	_workflow_margin = margin
	var panel := PanelContainer.new()
	panel.add_theme_stylebox_override("panel", _panel_style(Color(0.012, 0.033, 0.052, 0.99), Color(0.32, 0.66, 0.69, 0.95)))
	margin.add_child(panel)
	var inner := MarginContainer.new()
	inner.add_theme_constant_override("margin_left", 22)
	inner.add_theme_constant_override("margin_right", 22)
	inner.add_theme_constant_override("margin_top", 17)
	inner.add_theme_constant_override("margin_bottom", 17)
	panel.add_child(inner)
	_panel_inner = inner
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 12)
	inner.add_child(column)
	var heading := BoxContainer.new()
	heading.vertical = false
	heading.name = "Heading"
	column.add_child(heading)
	_heading = heading
	_port_artwork = TextureRect.new()
	_port_artwork.name = "PortArtwork"
	_port_artwork.custom_minimum_size = Vector2(112, 92)
	_port_artwork.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	_port_artwork.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	_port_artwork.tooltip_text = "Port illustration from the shared asset catalogue."
	heading.add_child(_port_artwork)
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
	_hold_label = Label.new()
	_hold_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_hold_label.add_theme_color_override("font_color", Color(0.74, 0.86, 0.82))
	column.add_child(_hold_label)
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
	_commodity_headers = columns
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
	DialogLayout.prepare_label(prompt)
	quantity_box.add_child(prompt)
	_quantity = SpinBox.new()
	_quantity.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_quantity.min_value = 1
	_quantity.max_value = 1000000
	_quantity.step = 1
	quantity_box.add_child(_quantity)
	var account_label := Label.new()
	account_label.text = "Pay from / credit to"
	quantity_box.add_child(account_label)
	_trade_account = OptionButton.new()
	_trade_account.add_item("Server preference", -1)
	_trade_account.add_item("Bank", 1)
	_trade_account.add_item("Petty cash", 0)
	# The server owns the saved account preference.  Default each new trade to
	# the omitted-account sentinel so a bank preference is not silently replaced
	# by a client-side petty-cash choice.
	_trade_account.selected = 0
	_trade_account.tooltip_text = "Choose which player account funds a purchase or receives sale proceeds."
	quantity_box.add_child(_trade_account)
	quantity_box.add_theme_constant_override("separation", 10)
	DialogLayout.attach(_trade_dialog, quantity_box)
	_trade_dialog.ok_button_text = "REQUEST QUOTE"
	add_child(_trade_dialog)
	resized.connect(_update_responsive_layout)
	call_deferred("_update_responsive_layout")

func show_port(payload: Dictionary, sector_id: Variant) -> void:
	var port_data = payload.get("port", {})
	# A port screen is actionable only when it is tied to the confirmed current
	# sector. Do not coerce a malformed JSON number/string/bool into a sector ID
	# and then let a quote request cross the protocol boundary.
	if (not (port_data is Dictionary) or port_data.is_empty()
			or not _is_positive_integral(sector_id)):
		close_for_location_change()
		return
	# The server's port.info payload also carries the port's resolved sector.
	# A delayed response for a previous location must not replace the current
	# dock with a valid-looking port ID. Keep compatibility with older payloads
	# that omit the optional field, but reject it when present and malformed or
	# inconsistent with the authoritative current-sector identity.
	if port_data.has("sector"):
		var port_sector: Variant = port_data["sector"]
		if (not _is_positive_integral(port_sector)
				or int(port_sector) != int(sector_id)):
			close_for_location_change()
			return
	if _tavern_mode:
		_tavern_mode = false
		tavern_exited.emit()
	# A refreshed port snapshot supersedes any open quantity dialog.  Do not let
	# a delayed confirmation submit the previous port row after stock, fixed
	# direction, or commodity identity has changed.
	_clear_pending_trade()
	_port = port_data.duplicate(true)
	_sector_id = sector_id
	_port_artwork.texture = AssetCatalog.texture_for_object("port", _port, "large")
	var art_id := AssetCatalog.asset_id_for_object("port", _port)
	_port_artwork.tooltip_text = "ART · " + (art_id if not art_id.is_empty() else "UNMAPPED")
	_port_title.text = str(_port.get("name", "PORT")).to_upper()
	_description.text = "Authoritative port inventory · choose a commodity and request a quote before committing a trade."
	_update_hold_label()
	_back_button.text = "BACK TO SECTOR"
	_tavern_button.visible = _is_stardock()
	_commodity_region.visible = true
	_rebuild_commodity_rows()
	visible = true
	_trade_dialog.hide()
	_update_responsive_layout()

func _rebuild_commodity_rows() -> void:
	var list: VBoxContainer = _commodity_list
	_trade_buttons.clear()
	for child in list.get_children():
		# Rebuilds happen immediately after an authoritative port refresh.  A
		# deferred queue_free leaves stale rows in the container until the next
		# frame, which can expose old commodities and duplicate controls while the
		# refreshed port is being acted on.
		child.free()
	var commodities = _port.get("commodities", [])
	if not commodities is Array or commodities.is_empty():
		var empty := Label.new()
		empty.text = "No commodity inventory was reported by the port."
		list.add_child(empty)
	else:
		for item in commodities:
			if item is Dictionary and not _authoritative_commodity_code(item).is_empty():
				_add_commodity_row(list, item)

func set_player_hold(ship: Dictionary) -> void:
	_player_ship = ship.duplicate(true) if ship is Dictionary else {}
	_update_hold_label()

func _update_hold_label() -> void:
	if not is_instance_valid(_hold_label):
		return
	var capacity = _player_ship.get("holds", null)
	var cargo: Array = _player_ship.get("cargo", [])
	var used = _player_ship.get("cargo_used", null)
	if used == null:
		used = 0
		for item in cargo:
			if item is Dictionary and (item.get("quantity", 0) is int or item.get("quantity", 0) is float):
				used += int(item.get("quantity", 0))
	var contents := "empty"
	if not cargo.is_empty():
		var parts: Array[String] = []
		for item in cargo:
			if item is Dictionary:
				parts.append("%s × %s" % [str(item.get("commodity", "?")), str(item.get("quantity", 0))])
		contents = ", ".join(parts)
	_hold_label.text = "YOUR HOLD · %s/%s used · Available space: %s\n%s" % [str(used), "—" if capacity == null else str(capacity), "—" if capacity == null else str(maxi(0, int(capacity) - int(used))), contents]

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
	_clear_pending_trade()
	_port.clear()
	_sector_id = 0
	_tavern_button.visible = false

func _is_stardock() -> bool:
	# Port type is server-owned service identity.  Do not coerce malformed JSON
	# values into the Stardock discriminator and unlock tavern actions locally.
	var value: Variant = _port.get("type", null)
	return typeof(value) == TYPE_INT and int(value) == 9

func _add_commodity_row(list: VBoxContainer, item: Dictionary) -> void:
	var code := _authoritative_commodity_code(item)
	if code.is_empty():
		return
	var card := PanelContainer.new()
	var compact := _compact_layout
	card.custom_minimum_size.y = 94 if compact else 58
	card.add_theme_stylebox_override("panel", _panel_style(Color(0.02, 0.055, 0.078, 0.98), Color(0.19, 0.38, 0.43, 0.8)))
	list.add_child(card)
	var stock = item.get("quantity", item.get("available", item.get("stock", null)))
	# The authoritative trade.port_info schema publishes `capacity`. Keep the
	# legacy display alias only for already-cached snapshots; trade quantity
	# validation remains server-owned.
	var capacity = item.get("capacity", item.get("max_quantity", null))
	var price: Variant = _authoritative_price_label(item)
	var name_label := Label.new()
	name_label.text = code
	name_label.add_theme_font_size_override("font_size", 16)
	var stock_label := Label.new()
	stock_label.text = "%s / %s" % ["—" if stock == null else _format_amount(stock), "—" if capacity == null else _format_amount(capacity)]
	var price_label := Label.new()
	price_label.text = price
	var buy := Button.new()
	buy.text = "BUY"
	# `sells` means the port sells this commodity to the player.  When the
	# authoritative payload supplies the fixed direction, it must override any
	# stock-based hint. Older port.info responses omit these flags; unknown
	# direction is left to the authoritative quote rather than inferred here.
	var buy_available: bool = _direction_available(item, "sells")
	buy.disabled = not buy_available
	buy.pressed.connect(_choose_trade.bind("buy", code, item))
	var sell := Button.new()
	sell.text = "SELL"
	# `buys` means the port buys this commodity from the player.
	var sell_available: bool = _direction_available(item, "buys")
	sell.disabled = not sell_available
	sell.pressed.connect(_choose_trade.bind("sell", code, item))
	if compact:
		var content := VBoxContainer.new()
		content.add_theme_constant_override("separation", 4)
		card.add_child(content)
		var title_row := HBoxContainer.new()
		name_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		title_row.add_child(name_label)
		title_row.add_child(price_label)
		content.add_child(title_row)
		stock_label.text = "PORT STOCK / CAPACITY  ·  " + stock_label.text
		content.add_child(stock_label)
		var actions := HBoxContainer.new()
		actions.add_child(buy)
		actions.add_child(sell)
		content.add_child(actions)
	else:
		var row := HBoxContainer.new()
		row.add_theme_constant_override("separation", 12)
		card.add_child(row)
		name_label.custom_minimum_size.x = 180
		row.add_child(name_label)
		stock_label.custom_minimum_size.x = 250
		row.add_child(stock_label)
		price_label.custom_minimum_size.x = 190
		row.add_child(price_label)
		row.add_child(buy)
		row.add_child(sell)
	_trade_buttons.append({"button": buy, "available": buy_available})
	_trade_buttons.append({"button": sell, "available": sell_available})

func _update_responsive_layout() -> void:
	var compact := size.x < 760.0
	if compact != _compact_layout:
		_compact_layout = compact
		if not _port.is_empty() and is_instance_valid(_commodity_list):
			_rebuild_commodity_rows()
	_dim.offset_left = 0.0 if compact else COMMAND_RAIL_CLEARANCE
	_dim.offset_top = 128.0 if compact else 88.0
	_workflow_margin.offset_left = 0.0 if compact else COMMAND_RAIL_CLEARANCE + 10.0
	_workflow_margin.offset_top = 128.0 if compact else 88.0
	_workflow_margin.add_theme_constant_override("margin_left", 12 if compact else 28)
	_workflow_margin.add_theme_constant_override("margin_right", 12 if compact else 28)
	_workflow_margin.add_theme_constant_override("margin_top", 12 if compact else 22)
	_panel_inner.add_theme_constant_override("margin_left", 12 if compact else 22)
	_panel_inner.add_theme_constant_override("margin_right", 12 if compact else 22)
	_panel_inner.add_theme_constant_override("margin_top", 12 if compact else 17)
	_panel_inner.add_theme_constant_override("margin_bottom", 12 if compact else 17)
	_heading.vertical = compact
	_port_artwork.custom_minimum_size = Vector2(90, 72) if compact else Vector2(112, 92)
	_commodity_headers.visible = not compact

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
	var port_id := _authoritative_port_id()
	if port_id <= 0 or _sector_id <= 0:
		return
	if not ["buy", "sell"].has(direction):
		return
	var authoritative_code := _authoritative_commodity_code(item)
	if authoritative_code.is_empty() or authoritative_code != commodity:
		return
	# The buttons are disabled from the same metadata, but keep the protocol
	# boundary safe when a stale signal or test harness invokes this handler
	# directly.  An explicit false or malformed fixed-mode flag is a server
	# refusal, not a client-side hint that may open a quote dialog.
	var mode_key := "sells" if direction == "buy" else "buys"
	if not _direction_available(item, mode_key):
		return
	_pending_direction = direction
	_pending_commodity = commodity
	_trade_dialog.title = "%s %s" % [direction.capitalize(), commodity]
	_quantity.value = 1
	# Use the authoritative server preference by default; explicit petty-cash or
	# bank selection remains available in the dialog.
	_trade_account.select(0)
	# Cached stock/capacity is display context, not a client-side trade rule.
	# The quote and settlement commands own quantity availability; keeping the
	# input open prevents stale port data from blocking a server-valid request.
	_quantity.max_value = 1000000
	DialogLayout.popup(_trade_dialog, Vector2i(460, 250))

func _submit_trade() -> void:
	var port_id := _authoritative_port_id()
	if port_id <= 0 or _sector_id <= 0 or _quantity.value < 1 or not _pending_trade_is_current():
		_clear_pending_trade()
		return
	trade_requested.emit(_pending_direction, port_id, _sector_id, _pending_commodity, int(_quantity.value), _selected_trade_account())
	_clear_pending_trade()

func _clear_pending_trade() -> void:
	_pending_direction = ""
	_pending_commodity = ""
	if is_instance_valid(_trade_dialog):
		_trade_dialog.hide()

func _pending_trade_is_current() -> bool:
	if not ["buy", "sell"].has(_pending_direction) or _pending_commodity.is_empty():
		return false
	var mode_key := "sells" if _pending_direction == "buy" else "buys"
	var commodities: Variant = _port.get("commodities", null)
	if not commodities is Array:
		return false
	for item in commodities:
		if not item is Dictionary or _authoritative_commodity_code(item) != _pending_commodity:
			continue
		return _direction_available(item, mode_key)
	return false

func _selected_trade_account() -> int:
	# OptionButton reserves an item ID of -1 as "use the item index", so the
	# saved-preference choice cannot be represented by get_selected_id(). Keep
	# the UI order local and emit the protocol value expected by Main.gd:
	# -1=omitted/server preference, 0=petty cash, 1=bank.
	match _trade_account.selected:
		0:
			return -1
		1:
			return 1
		2:
			return 0
		_:
			return -1

func _authoritative_port_id() -> int:
	# Port identity comes from the server's JSON schema. Do not coerce bools,
	# floats, strings, or non-positive values into a trade target.
	# An explicit canonical `id` takes precedence over the compatibility alias.
	# If it is present but malformed, reject the record instead of allowing a
	# contradictory `port_id` alias to cross the trade command boundary.
	if _port.has("id"):
		var canonical_value: Variant = _port["id"]
		return int(canonical_value) if _is_positive_integral(canonical_value) else 0
	if _port.has("port_id"):
		var alias_value: Variant = _port["port_id"]
		return int(alias_value) if _is_positive_integral(alias_value) else 0
	return 0

func _is_positive_integral(value: Variant) -> bool:
	return (typeof(value) == TYPE_INT and int(value) > 0) or (typeof(value) == TYPE_FLOAT and is_finite(value) and value > 0.0 and value == floor(value))

func _authoritative_commodity_code(item: Dictionary) -> String:
	# Commodity identity is a server-owned string. Do not stringify booleans,
	# numbers, empty values, or whitespace-padded values into a command target.
	# The quote response supplies the canonical representation later; it must
	# not be used to repair a malformed port-info identity.
	var value: Variant = item.get("code", item.get("commodity", null))
	if typeof(value) != TYPE_STRING:
		return ""
	var code := str(value)
	return code if not code.is_empty() and code == code.strip_edges() else ""

func _direction_available(item: Dictionary, key: String) -> bool:
	# The authoritative trade.port_info commodity schema reports stock and
	# prices, while fixed buy/sell mode is enforced by the trade quote/settle
	# commands.  An omitted mode is therefore not a refusal: keep the action
	# available and let the server's quote be authoritative.  An explicit false
	# is a canonical refusal and malformed metadata is unsafe to act on.
	if not item.has(key):
		return true
	# An explicitly present but schema-invalid capability is not permission to
	# send a trade.  Refuse locally until the port is refreshed with a valid
	# authoritative boolean.
	return item[key] if typeof(item[key]) == TYPE_BOOL else false

func _authoritative_price(item: Dictionary) -> Variant:
	# Port-info prices are server-owned indicative integer credits.  They are
	# display-only here; malformed values must not look like a usable quote.
	# The authoritative trade.port_info payload names the directional values
	# `buy_price` and `sell_price`; retain the older aliases only as a
	# compatibility read for already-cached payloads.  The workflow does not
	# derive direction or settlement from this display value.
	var value: Variant = item.get("buy_price", item.get("sell_price", item.get("price", item.get("base_price", null))))
	# The server's price fields are integer-valued credits, but JSON encoding
	# may publish them as a real (the trade.quote contract does this). Keep
	# integral reals visible as display data without using them as a local
	# settlement rule.
	if typeof(value) == TYPE_INT and int(value) >= 0:
		return value
	if typeof(value) == TYPE_FLOAT and is_finite(value) and value >= 0.0 and value == floor(value):
		return int(value)
	return null

func _authoritative_price_label(item: Dictionary) -> String:
	# Port rows expose both server-owned directional indicators.  Do not show
	# the buy value for a sell action: that makes a valid sale look as though it
	# will settle at the wrong authoritative price.  These values are display
	# only; trade.quote remains the source of settlement truth.
	var buy: Variant = _authoritative_price_field(item, "buy_price", "price")
	var sell: Variant = _authoritative_price_field(item, "sell_price", "price")
	if buy == null and sell == null:
		return "—"
	return "BUY %s · SELL %s" % ["—" if buy == null else str(buy), "—" if sell == null else str(sell)]

func _authoritative_price_field(item: Dictionary, field: String, fallback: String) -> Variant:
	var value: Variant = item.get(field, item.get(fallback, null))
	if typeof(value) == TYPE_INT and int(value) >= 0:
		return value
	if typeof(value) == TYPE_FLOAT and is_finite(value) and value >= 0.0:
		return value
	return null

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
