extends PanelContainer

const TwProtocol = preload("res://Protocol.gd")

signal command_requested(command: String, data: Dictionary, label: String, mutating: bool)

const FIELD_INT := "integer"
const FIELD_TEXT := "text"
const COMMANDS := {
	"COMPUTER": [
		{"label": "Player information", "command": "player.my_info"},
		{"label": "Ship information", "command": "ship.info"},
		{"label": "Rename ship · unavailable", "unavailable_reason": "The ship.rename schema accepts name, but its handler requires ship_id and new_name."},
		{"label": "Scan adjacent sectors", "command": "move.scan"},
		{"label": "Sector density scan", "command": "sector.scan.density", "fixed": {}},
		{"label": "Player rankings", "command": "player.rankings"},
		{"label": "Shipyard catalogue", "command": "shipyard.list", "requires_shipyard_dock": true},
		{"label": "Hardware catalogue", "command": "hardware.list"},
		{"label": "Buy ship hardware", "command": "hardware.buy", "fields": [["code", FIELD_TEXT, "Hardware item code", ""], ["quantity", FIELD_INT, "Quantity", "1"]], "mutating": true, "idempotency": true},
		{"label": "Trade routes", "command": "player.computer.recommend_routes", "tooltip": "The server's two-way filter is mismatched; recommendations use its default (false).", "fields": [["max_hops_between", FIELD_INT, "Maximum hops", "10"], ["limit", FIELD_INT, "Result limit", "10"]]},
	],
	"COMMUNICATIONS": [
		{"label": "Recent server events", "local_activity": true},
		{"label": "Chat history", "command": "chat.history", "fixed": {"limit": 20}},
		{"label": "Broadcast message", "command": "chat.broadcast", "fields": [["message", FIELD_TEXT, "Message", ""]], "mutating": true},
		{"label": "Private message · unavailable", "unavailable_reason": "The chat.send schema rejects the recipient fields required by its handler."},
		{"label": "Notices", "command": "notice.list"},
		{"label": "Mail inbox", "command": "mail.inbox", "fixed": {"limit": 20}},
		{"label": "Online players", "command": "player.list_online"},
	],
	"HELP": [
		{"label": "Controls and command flow", "local_help": true},
	],
	"OPERATIONS": [
		{"label": "Combat status", "command": "combat.status"},
		{"label": "Deploy fighters", "command": "combat.deploy_fighters", "fields": [["amount", FIELD_INT, "Number of fighters", "1"], ["offense", FIELD_INT, "Offense percentage", "50"]], "mutating": true},
		{"label": "Deploy mines", "command": "combat.deploy_mines", "fields": [["amount", FIELD_INT, "Number of mines", "1"], ["offense", FIELD_INT, "Offense mode (1 toll, 2 defend, 3 attack)", "2"]], "mutating": true},
		{"label": "List deployed fighters · recall", "command": "deploy.fighters.list", "context_sector": true},
		{"label": "List deployed mines · recall", "command": "deploy.mines.list", "context_sector": true},
		{"label": "Sweep mines", "command": "combat.sweep_mines", "mutating": true},
		{"label": "Jettison cargo", "command": "trade.jettison", "fields": [["commodity", FIELD_TEXT, "Commodity (ore, organics, equipment, colonists, etc.)", "ore"], ["quantity", FIELD_INT, "Quantity to jettison", "1"]], "mutating": true},
		{"label": "Deploy marker beacon", "command": "sector.set_beacon", "fields": [["text", FIELD_TEXT, "Beacon text (up to 80 characters)", "", 80]], "context_sector": true, "mutating": true},
		{"label": "Create planet with Genesis Torpedo", "command": "planet.genesis_create", "fields": [["name", FIELD_TEXT, "New planet name", ""]], "context_sector": true, "fixed": {"owner_entity_type": "player"}, "mutating": true, "idempotency": true},
	],
	"FINANCES": [
		{"label": "Bank balance", "command": "bank.balance"},
		{"label": "Deposit credits", "command": "bank.deposit", "fields": [["amount", FIELD_INT, "Amount", ""]], "mutating": true},
		{"label": "Withdraw credits", "command": "bank.withdraw", "fields": [["amount", FIELD_INT, "Amount", ""]], "mutating": true},
		{"label": "Transfer credits to player", "command": "bank.transfer", "fields": [["to_player_id", FIELD_INT, "Recipient player ID", ""], ["amount", FIELD_INT, "Amount", ""]], "mutating": true},
		{"label": "Bank history", "command": "bank.history"},
		{"label": "Bank leaderboard", "command": "bank.leaderboard"},
	],
	"NEWS & RECORDS": [
		{"label": "News feed", "command": "news.get_feed"},
		{"label": "Mark news read", "command": "news.mark_feed_read", "mutating": true},
		{"label": "Insurance policy list · server stub", "unavailable_reason": "The current insurance handler returns a hard-coded stub, not authoritative policies."},
		{"label": "Buy insurance · server stub", "unavailable_reason": "The current insurance purchase handler returns a hard-coded stub and does not create a policy."},
		{"label": "File insurance claim · server stub", "unavailable_reason": "The current insurance claim handler returns a hard-coded stub and does not process a claim."},
		{"label": "Subscriptions", "command": "subscribe.list"},
	],
	"CORPORATION": [
		{"label": "Corporation status", "command": "corp.status"},
		{"label": "Member roster", "command": "corp.roster"},
		{"label": "Corporation directory", "command": "corp.list"},
		{"label": "Treasury balance", "command": "corp.balance"},
		{"label": "Treasury statement", "command": "corp.statement"},
		{"label": "Create corporation", "command": "corp.create", "fields": [["name", FIELD_TEXT, "Corporation name", ""], ["tag", FIELD_TEXT, "Corporation tag", ""]], "mutating": true},
		{"label": "Join corporation", "command": "corp.join", "fields": [["corp_id", FIELD_INT, "Corporation ID", ""]], "mutating": true},
		{"label": "Invite player", "command": "corp.invite", "fields": [["target_player_id", FIELD_INT, "Player ID", ""]], "mutating": true},
		{"label": "Kick member", "command": "corp.kick", "fields": [["target_player_id", FIELD_INT, "Player ID", ""]], "mutating": true},
		{"label": "Deposit to treasury", "command": "corp.deposit", "fields": [["amount", FIELD_INT, "Credits", ""]], "mutating": true},
		{"label": "Withdraw from treasury", "command": "corp.withdraw", "fields": [["amount", FIELD_INT, "Credits", ""]], "mutating": true},
		{"label": "Transfer CEO role", "command": "corp.transfer_ceo", "fields": [["target_player_id", FIELD_INT, "New CEO player ID", ""]], "mutating": true},
		{"label": "Leave corporation", "command": "corp.leave", "mutating": true},
		{"label": "Dissolve corporation", "command": "corp.dissolve", "mutating": true},
	],
	"MARKET": [
		{"label": "Exchange listings", "command": "equity.exchange.list"},
		{"label": "My portfolio", "command": "equity.portfolio.list"},
		{"label": "Register IPO", "command": "equity.ipo_register", "fields": [["ticker", FIELD_TEXT, "Ticker (3–5 characters)", ""], ["total_shares", FIELD_INT, "Total shares", ""], ["par_value", FIELD_INT, "Par value in whole credits", ""]], "mutating": true},
		{"label": "Buy shares", "command": "equity.buy", "fields": [["equity_id", FIELD_INT, "Equity ID", ""], ["quantity", FIELD_INT, "Quantity", ""]], "mutating": true},
		{"label": "Set dividend", "command": "equity.dividend_set", "fields": [["amount_per_share", FIELD_INT, "Credits per share", ""]], "mutating": true},
	],
	"TAVERN": [
		{"label": "Price board", "command": "tavern.barcharts.get_prices_summary"},
		{"label": "Lottery status", "command": "tavern.lottery.status"},
		{"label": "Buy lottery ticket", "command": "tavern.lottery.buy_ticket", "fields": [["number", FIELD_INT, "Your lucky number", ""]], "mutating": true},
		{"label": "Place Dead Pool bet", "command": "tavern.deadpool.place_bet", "fields": [["target_player_name", FIELD_TEXT, "Target player name", ""], ["amount", FIELD_INT, "Bet amount", ""]], "mutating": true},
		{"label": "Play bar dice", "command": "tavern.dice.play", "fields": [["amount", FIELD_INT, "Bet amount", ""]], "mutating": true},
		{"label": "Play high stakes", "command": "tavern.highstakes.play", "fields": [["amount", FIELD_INT, "Bet amount", ""], ["rounds", FIELD_INT, "Number of rounds", ""]], "mutating": true},
		{"label": "Buy raffle ticket", "command": "tavern.raffle.buy_ticket", "mutating": true},
		{"label": "Buy a round", "command": "tavern.round.buy", "mutating": true},
		{"label": "Take loan", "command": "tavern.loan.take", "fields": [["amount", FIELD_INT, "Loan amount", ""]], "mutating": true},
		{"label": "Repay loan", "command": "tavern.loan.pay", "fields": [["amount", FIELD_INT, "Payment amount", ""]], "mutating": true},
		{"label": "Buy password", "command": "tavern.trader.buy_password", "mutating": true},
		{"label": "Post tavern graffiti", "command": "tavern.graffiti.post", "fields": [["text", FIELD_TEXT, "Message for the tavern wall", "", 255]], "mutating": true},
		{"label": "Buy a rumour hint · 50 CR", "command": "tavern.rumour.get_hint", "mutating": true},
	],
	"NAVIGATION": [
		{"label": "Warp to sector number", "command": "move.warp", "fields": [["to_sector_id", FIELD_INT, "Destination sector", ""]], "mutating": true},
		{"label": "Bookmarks", "command": "nav.bookmark.list"},
		{"label": "Add current sector bookmark", "command": "nav.bookmark.add", "fields": [["name", FIELD_TEXT, "Bookmark name", ""]], "context_sector": true, "mutating": true},
		{"label": "Remove bookmark", "command": "nav.bookmark.remove", "fields": [["name", FIELD_TEXT, "Bookmark name", ""]], "mutating": true},
		{"label": "Avoid list", "command": "nav.avoid.list"},
		{"label": "Avoid a sector", "command": "nav.avoid.add", "fields": [["sector_id", FIELD_INT, "Sector ID", ""]], "mutating": true},
		{"label": "Remove sector from avoid list", "command": "nav.avoid.remove", "fields": [["sector_id", FIELD_INT, "Sector ID", ""]], "mutating": true},
		{"label": "Autopilot status", "command": "move.autopilot.status"},
		{"label": "Plot route to sector", "command": "move.autopilot.start", "fields": [["to_sector_id", FIELD_INT, "Destination sector", ""]], "requires_fresh_sector": true},
	],
	"SETTINGS & NOTES": [
		{"label": "View settings and preferences", "command": "player.get_settings"},
		{"label": "View preferences", "command": "player.get_prefs"},
		{"label": "Set 24-hour clock preference", "command": "player.set_prefs", "fields": [["value", "boolean", "Use 24-hour clock", "true"]], "preference_key": "ui.clock_24h", "preference_type": "bool", "mutating": true},
		{"label": "Set locale preference", "command": "player.set_prefs", "fields": [["value", FIELD_TEXT, "Locale (for example en-GB)", "en-GB"]], "preference_key": "ui.locale", "preference_type": "string", "mutating": true},
		{"label": "List notes", "command": "notes.list", "fixed": {}},
		{"label": "Save note", "command": "notes.set", "fields": [["scope", FIELD_TEXT, "Note scope", "player"], ["key", FIELD_TEXT, "Note key", ""], ["note", FIELD_TEXT, "Note text", ""]], "mutating": true},
		{"label": "Delete note", "command": "notes.delete", "fields": [["scope", FIELD_TEXT, "Note scope", "player"], ["key", FIELD_TEXT, "Note key", ""]], "mutating": true},
	],
	"MAIL & SUBSCRIPTIONS": [
		{"label": "Mail inbox", "command": "mail.inbox", "fixed": {}},
		{"label": "Read mail by ID", "command": "mail.read", "fields": [["mail_id", FIELD_INT, "Mail ID", ""]], "mutating": true, "duplicate_mail_read_id": true},
		{"label": "Send mail · unavailable", "unavailable_reason": "The mail.send schema requires to_player_name, but the handler reads to or to_id."},
		{"label": "Delete mail · unavailable", "unavailable_reason": "The mail.delete schema requires mail_id, but the handler reads an ids array."},
		{"label": "Subscription catalogue", "command": "subscribe.catalog"},
		{"label": "Add subscription · unavailable", "unavailable_reason": "Protocol docs and handler require topic, but the strict schema requires event_type and rejects topic."},
		{"label": "Remove subscription · unavailable", "unavailable_reason": "Protocol docs and handler require topic, but the strict schema requires event_type and rejects topic."},
	],
	"SESSION": [
		{"label": "Log out to login screen", "local_logout": true},
	],
}

var _snapshot: Dictionary = {}
var _selection: Dictionary = {}
var _category_buttons: OptionButton
var _action_list: VBoxContainer
var _action_scroll: ScrollContainer
var _title: Label
var _active_category := "COMPUTER"
var _busy := false
var _form_dialog: ConfirmationDialog
var _result_dialog: AcceptDialog
var _result_text: Label
var _logout_dialog: ConfirmationDialog
var _form_fields: Dictionary = {}
var _form_action: Dictionary = {}
var _form_content: VBoxContainer
var _planet_action_buttons: Array[Dictionary] = []
var _shipyard_dialog: AcceptDialog
var _shipyard_rows: VBoxContainer
var _shipyard_name_dialog: ConfirmationDialog
var _shipyard_name_input: LineEdit
var _shipyard_type_id := 0
var _deployment_command := ""
var _deployment_sector := 0
var _activity_history: Array[String] = []
var _tavern_access := false

func _ready() -> void:
	custom_minimum_size.x = 260
	size_flags_vertical = Control.SIZE_EXPAND_FILL
	add_theme_stylebox_override("panel", _panel_style(Color(0.009, 0.021, 0.034, 0.995), Color(0.25, 0.55, 0.61, 0.96)))
	var outer := MarginContainer.new()
	outer.add_theme_constant_override("margin_left", 18)
	outer.add_theme_constant_override("margin_right", 18)
	outer.add_theme_constant_override("margin_top", 14)
	outer.add_theme_constant_override("margin_bottom", 14)
	add_child(outer)
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 9)
	outer.add_child(column)
	_title = Label.new()
	_title.text = "COMMANDS"
	_title.add_theme_font_size_override("font_size", 20)
	_title.add_theme_color_override("font_color", Color(0.55, 0.88, 0.85))
	column.add_child(_title)
	var context := Label.new()
	context.name = "Context"
	context.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	context.add_theme_color_override("font_color", Color(0.78, 0.84, 0.83))
	column.add_child(context)
	_category_buttons = OptionButton.new()
	_style_button(_category_buttons, true)
	_category_buttons.item_selected.connect(_on_category_selected)
	column.add_child(_category_buttons)
	var scroll := ScrollContainer.new()
	scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	scroll.custom_minimum_size.y = 180
	column.add_child(scroll)
	_action_scroll = scroll
	_action_list = VBoxContainer.new()
	_action_list.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_action_list.add_theme_constant_override("separation", 6)
	scroll.add_child(_action_list)
	_form_dialog = ConfirmationDialog.new()
	_form_dialog.title = "Command details"
	_form_dialog.confirmed.connect(_submit_form)
	_form_content = VBoxContainer.new()
	_form_dialog.add_child(_form_content)
	_form_dialog.exclusive = false
	get_parent().add_child(_form_dialog)
	_result_dialog = AcceptDialog.new()
	_result_dialog.title = "Command result"
	_result_dialog.exclusive = false
	var result_margin := MarginContainer.new()
	result_margin.add_theme_constant_override("margin_left", 8)
	result_margin.add_theme_constant_override("margin_right", 8)
	result_margin.add_theme_constant_override("margin_top", 8)
	result_margin.add_theme_constant_override("margin_bottom", 8)
	_result_dialog.add_child(result_margin)
	_result_text = Label.new()
	_result_text.custom_minimum_size = Vector2(470, 160)
	_result_text.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_result_text.text_overrun_behavior = TextServer.OVERRUN_TRIM_ELLIPSIS
	result_margin.add_child(_result_text)
	get_parent().add_child(_result_dialog)
	_logout_dialog = ConfirmationDialog.new()
	_logout_dialog.title = "Log out of Trade Wars?"
	_logout_dialog.dialog_text = "Your server session will be ended. You can log in again without closing the client."
	_logout_dialog.ok_button_text = "LOG OUT"
	_logout_dialog.exclusive = false
	_logout_dialog.confirmed.connect(_confirm_logout)
	get_parent().add_child(_logout_dialog)
	_shipyard_dialog = AcceptDialog.new()
	_shipyard_dialog.title = "Shipyard · available hulls"
	_shipyard_dialog.exclusive = false
	var shipyard_scroll := ScrollContainer.new()
	shipyard_scroll.custom_minimum_size = Vector2(500, 340)
	_shipyard_dialog.add_child(shipyard_scroll)
	_shipyard_rows = VBoxContainer.new()
	_shipyard_rows.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	shipyard_scroll.add_child(_shipyard_rows)
	get_parent().add_child(_shipyard_dialog)
	_shipyard_name_dialog = ConfirmationDialog.new()
	_shipyard_name_dialog.title = "Confirm hull exchange"
	_shipyard_name_dialog.exclusive = false
	_shipyard_name_dialog.ok_button_text = "CONFIRM UPGRADE"
	var shipyard_name_column := VBoxContainer.new()
	_shipyard_name_dialog.add_child(shipyard_name_column)
	var shipyard_note := Label.new()
	shipyard_note.text = "The server will recheck eligibility and final cost before exchanging your hull."
	shipyard_note.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	shipyard_name_column.add_child(shipyard_note)
	_shipyard_name_input = LineEdit.new()
	_shipyard_name_input.placeholder_text = "Name your new ship"
	_shipyard_name_input.max_length = 100
	shipyard_name_column.add_child(_shipyard_name_input)
	_shipyard_name_dialog.confirmed.connect(_submit_shipyard_upgrade)
	get_parent().add_child(_shipyard_name_dialog)
	var context_label: Label = column.get_node("Context")
	set_meta("context_label", context_label)
	_rebuild_categories()

func open_for(snapshot: Dictionary, selection: Dictionary) -> void:
	_snapshot = snapshot.duplicate(true)
	_selection = selection.duplicate(true)
	var context_label: Label = get_meta("context_label")
	context_label.text = _context_description()
	if not _selection.is_empty():
		if str(_selection.get("kind", "")) == "planet":
			_active_category = "PLANET"
		elif str(_selection.get("kind", "")) == "port":
			_active_category = "PORT"
		elif str(_selection.get("kind", "")) == "ship":
			_active_category = "SHIP"
	_rebuild_categories()
	_render_actions()
	visible = true

func update_snapshot(snapshot: Dictionary) -> void:
	_snapshot = snapshot.duplicate(true)
	_selection.clear()
	(get_meta("context_label") as Label).text = _context_description()
	if _active_category in ["PLANET", "PORT", "SHIP"]:
		_active_category = "COMPUTER"
	_rebuild_categories()
	_render_actions()
	visible = true

func set_tavern_access(available: bool) -> void:
	_tavern_access = available
	if not available and _active_category == "TAVERN":
		_active_category = "COMPUTER"
	if available:
		_active_category = "TAVERN"
	(get_meta("context_label") as Label).text = _context_description()
	_rebuild_categories()

func _context_description() -> String:
	if _tavern_access:
		return "StarDock Tavern · access confirmed by the server."
	if _selection.is_empty():
		return "General shipboard, communication and service commands."
	return "Selected %s · %s" % [str(_selection.get("kind", "object")).capitalize(), str(_selection.get("name", ""))]

func dispatch_context_action(action: Dictionary, selection: Dictionary) -> void:
	var previous_selection := _selection
	_selection = selection.duplicate(true)
	_choose_action(action)
	_selection = previous_selection

func show_result(label: String, payload: Dictionary) -> void:
	_result_dialog.title = label
	_result_text.text = _format_result(payload)
	_result_dialog.popup_centered(Vector2i(560, 380))

func set_activity_history(entries: Array[String]) -> void:
	_activity_history = entries.duplicate()

func show_activity_history() -> void:
	_result_dialog.title = "Recent server events"
	_result_text.text = "No server events have arrived during this session." if _activity_history.is_empty() else "\n\n".join(_activity_history)
	_result_dialog.popup_centered(Vector2i(620, 420))

func show_help() -> void:
	_result_dialog.title = "Trade Wars · Field Guide"
	_result_text.text = "SELECT\nClick an illustrated object or its contents row. Keyboard focus and selection stay synchronized. Use Tab to move between controls, arrow keys to move through focused lists, Enter to activate, and Escape/Back to close the current dialog.\n\nCOMMAND\nChoose an available action from the command drawer. Actions are discrete requests; the server confirms or refuses them, and the display refreshes from authoritative state. A pending action cannot be sent twice.\n\nMOVE\nSelect an adjacent warp destination, then confirm Move. The screen position of a marker is decorative; it does not represent distance or a flight path.\n\nPORT AND PLANET\nDock opens the port view after port information is confirmed. Land is a server command; the planet surface opens only after success. Trading requires a server quote and your confirmation.\n\nCONNECTION\nAfter a disconnect, the last confirmed scene is marked stale. Reconnect to refresh it before acting. Zero values are distinct from unavailable values."
	_result_dialog.popup_centered(Vector2i(620, 520))

func show_shipyard(data: Dictionary, current_ship_name: String = "") -> void:
	for child in _shipyard_rows.get_children():
		child.queue_free()
	var available: Variant = data.get("available", [])
	if not (available is Array) or available.is_empty():
		var empty := Label.new()
		empty.text = "No hulls were returned by this shipyard."
		_shipyard_rows.add_child(empty)
	else:
		for hull in available:
			if not (hull is Dictionary):
				continue
			var eligible := bool(hull.get("eligible", false))
			var reasons: Array = hull.get("reasons", [])
			var hull_label := "%s · net %s CR" % [str(hull.get("name", "Unknown hull")), str(hull.get("net_cost", "—"))]
			if not eligible and not reasons.is_empty():
				hull_label += " · " + ", ".join(PackedStringArray(reasons))
			var button := Button.new()
			button.text = hull_label
			button.alignment = HORIZONTAL_ALIGNMENT_LEFT
			button.disabled = not eligible or not hull.has("type_id")
			button.pressed.connect(_choose_shipyard_hull.bind(int(hull.get("type_id", 0)), current_ship_name))
			_shipyard_rows.add_child(button)
	_shipyard_dialog.popup_centered(Vector2i(580, 450))

func _choose_shipyard_hull(type_id: int, current_ship_name: String) -> void:
	if type_id <= 0:
		return
	_shipyard_dialog.hide()
	_shipyard_type_id = type_id
	_shipyard_name_input.text = current_ship_name
	_shipyard_name_dialog.popup_centered(Vector2i(500, 190))

func _submit_shipyard_upgrade() -> void:
	var new_name := _shipyard_name_input.text.strip_edges()
	if _shipyard_type_id <= 0 or new_name.is_empty():
		_add_unavailable("Choose a hull and enter a ship name before confirming.")
		return
	command_requested.emit("shipyard.upgrade", {"new_type_id": _shipyard_type_id, "new_ship_name": new_name}, "Ship hull upgrade", true)
	_shipyard_name_dialog.hide()
	_shipyard_dialog.hide()

func show_deployed_assets(data: Dictionary, asset_kind: String, sector_id: int) -> void:
	_shipyard_dialog.title = "Deployed %s · current sector" % asset_kind
	_deployment_sector = sector_id
	_deployment_command = "fighters.recall" if asset_kind == "fighters" else "mines.recall"
	for child in _shipyard_rows.get_children():
		child.queue_free()
	var entries: Variant = data.get("entries", [])
	if not (entries is Array) or entries.is_empty():
		var empty := Label.new()
		empty.text = "No deployed %s were reported in this sector." % asset_kind
		_shipyard_rows.add_child(empty)
	else:
		for entry in entries:
			if not (entry is Dictionary):
				continue
			var asset_id = entry.get("asset_id", null)
			var button := Button.new()
			button.text = "Asset %s · %s units · %s" % [str(asset_id if asset_id != null else "unknown"), str(entry.get("count", "—")), str(entry.get("offense_mode", "mode unknown"))]
			button.alignment = HORIZONTAL_ALIGNMENT_LEFT
			button.disabled = asset_id == null or sector_id <= 0
			if asset_id != null:
				button.pressed.connect(_recall_asset.bind(int(asset_id)))
			_shipyard_rows.add_child(button)
	_shipyard_dialog.popup_centered(Vector2i(580, 450))

func _recall_asset(asset_id: int) -> void:
	if asset_id <= 0 or _deployment_sector <= 0 or _deployment_command.is_empty():
		return
	_shipyard_dialog.hide()
	command_requested.emit(_deployment_command, {"sector_id": _deployment_sector, "asset_id": asset_id}, "Recall deployed asset", true)

func _format_result(payload: Dictionary) -> String:
	var safe: Dictionary = TwProtocol.redact(payload)
	var lines: Array[String] = []
	_append_result(safe, "", 0, lines)
	return "No additional details were returned." if lines.is_empty() else "\n".join(lines).substr(0, 5000)

func _append_result(value: Variant, key: String, depth: int, lines: Array[String]) -> void:
	if depth > 3 or lines.size() > 35:
		return
	var prefix := "  ".repeat(depth)
	if value is Dictionary:
		if not key.is_empty():
			lines.append("%s%s" % [prefix, key.replace("_", " ").capitalize()])
		for child_key in value:
			if str(value[child_key]) == TwProtocol.REDACTED:
				continue
			_append_result(value[child_key], str(child_key), depth + (1 if not key.is_empty() else 0), lines)
	elif value is Array:
		lines.append("%s%s · %d entries" % [prefix, key.replace("_", " ").capitalize(), value.size()])
		for index in mini(value.size(), 8):
			_append_result(value[index], "Entry %d" % (index + 1), depth + 1, lines)
	else:
		lines.append("%s%s: %s" % [prefix, key.replace("_", " ").capitalize(), str(value)])

func _categories() -> Array[String]:
	var result: Array[String] = []
	if not _selection.is_empty():
		var kind := str(_selection.get("kind", ""))
		if kind in ["planet", "port", "ship"]:
			result.append(kind.to_upper())
	for category in COMMANDS:
		if category == "TAVERN" and not _tavern_access:
			continue
		result.append(str(category))
	return result

func _rebuild_categories() -> void:
	_category_buttons.clear()
	var categories := _categories()
	var selected_index := 0
	for index in categories.size():
		_category_buttons.add_item(categories[index])
		if categories[index] == _active_category:
			selected_index = index
	_category_buttons.select(selected_index)
	if not categories.is_empty():
		_active_category = categories[selected_index]
	_render_actions()

func _on_category_selected(index: int) -> void:
	var categories := _categories()
	if index < 0 or index >= categories.size():
		return
	_active_category = categories[index]
	_render_actions()

func _render_actions() -> void:
	for child in _action_list.get_children():
		child.queue_free()
	var actions: Array = []
	if _active_category == "PLANET":
		actions = [{"label": "Planet information", "command": "planet.info", "context_id": "planet_id"}]
		var ship_state: Dictionary = _snapshot.get("ship", {})
		if int(ship_state.get("onplanet", 0)) > 0:
			actions.append({"label": "Launch from planet", "command": "planet.launch", "fixed": {}, "mutating": true})
		else:
			actions.append({"label": "Land on planet", "command": "planet.land", "context_id": "planet_id", "mutating": true})
	elif _active_category == "PORT":
		actions = [
			{"label": "Dock · open port screen", "command": "port.info", "fixed": {}},
			{"label": "Request commodity quote", "command": "trade.quote", "context_id": "port_id", "fields": [["commodity", FIELD_TEXT, "Commodity code or name", ""], ["quantity", FIELD_INT, "Quantity", "1"]]},
			{"label": "Shipyard · choose eligible hull", "command": "shipyard.list", "requires_shipyard_dock": true},
		]
	elif _active_category == "SHIP":
		var ship_data: Dictionary = _selection.get("data", {})
		var selected_ship_id := int(ship_data.get("ship_id", ship_data.get("id", 0)))
		var active_ship_id := int(_snapshot.get("hud", {}).get("ship_id", 0))
		if selected_ship_id > 0 and selected_ship_id == active_ship_id:
			_add_unavailable("Your active ship is represented in the HUD; target actions are only available for other vessels.")
		else:
			actions = [
				{"label": "Attack this ship", "command": "combat.attack", "context_id": "target_ship_id", "mutating": true},
				{"label": "Claim this ship · server verifies eligibility", "command": "ship.claim", "context_id": "ship_id", "mutating": true},
				{"label": "Tow this ship · requires owned, unpiloted vessel", "command": "ship.tow", "context_id": "target_ship_id", "mutating": true},
			]
		_add_unavailable("Private messages need a server handler/schema fix: chat.send currently has no compatible recipient field.")
		_add_unavailable("Inspection details are unavailable: the current ship.inspect handler ignores the selected ship ID and returns a sector list.")
	else:
		actions = COMMANDS.get(_active_category, [])
		if _active_category == "COMMUNICATIONS":
			_add_unavailable("Private chat is temporarily unavailable because the server's chat.send schema does not accept the recipient field its handler requires.")
		elif _active_category == "MAIL & SUBSCRIPTIONS":
			_add_unavailable("Mail sending/deletion are unavailable until their protocol schemas match the implemented handlers.")
	if actions.is_empty():
		_add_unavailable("No commands are available in this category.")
		return
	for action in actions:
		var button := Button.new()
		button.text = str(action.get("label", action.get("command", "Command")))
		button.alignment = HORIZONTAL_ALIGNMENT_LEFT
		button.custom_minimum_size.y = 42
		_style_button(button, false)
		var connected := bool(_snapshot.get("authenticated", false)) and not bool(_snapshot.get("disconnected", true))
		var freshness: Dictionary = _snapshot.get("freshness", {})
		var context_stale: bool = (action.has("context_id") or bool(action.get("requires_fresh_sector", false))) and str(freshness.get("sector", "")) != "fresh"
		var unavailable_reason := str(action.get("unavailable_reason", ""))
		var missing_shipyard_context := bool(action.get("requires_shipyard_dock", false)) and not _is_docked_at_shipyard()
		button.disabled = not unavailable_reason.is_empty() or _busy or not connected or context_stale or missing_shipyard_context
		button.tooltip_text = unavailable_reason if not unavailable_reason.is_empty() else ("Dock at a supported shipyard port in this sector first." if missing_shipyard_context else (str(action.get("tooltip", "")) if action.has("tooltip") else ("Unavailable while disconnected." if not connected else ("Refresh the sector before using this selected object." if context_stale else ("A command is already waiting for the server." if _busy else "")))))
		button.pressed.connect(_choose_action.bind(action))
		_action_list.add_child(button)

func _is_docked_at_shipyard() -> bool:
	var ship: Dictionary = _snapshot.get("ship", {})
	var docked_port_id := int(ship.get("ported", 0))
	if docked_port_id <= 0:
		return false
	var sector: Dictionary = _snapshot.get("sector", {})
	for port in sector.get("ports", []):
		if not (port is Dictionary):
			continue
		var port_id := int(port.get("id", port.get("port_id", 0)))
		var port_type := int(port.get("type", -1))
		if port_id == docked_port_id:
			return port_type in [9, 10]
	return false

func set_busy(value: bool) -> void:
	_busy = value
	_render_actions()

func _add_unavailable(message: String) -> void:
	var label := Label.new()
	label.text = message
	label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	label.add_theme_color_override("font_color", Color(0.82, 0.7, 0.58))
	_action_list.add_child(label)

func _choose_action(action: Dictionary) -> void:
	if action.has("unavailable_reason"):
		_add_unavailable(str(action["unavailable_reason"]))
		return
	if bool(action.get("local_logout", false)):
		if bool(_snapshot.get("authenticated", false)) and not bool(_snapshot.get("disconnected", true)):
			_logout_dialog.popup_centered(Vector2i(460, 180))
		else:
			_add_unavailable("There is no active server session to log out of.")
		return
	if str(action.get("command", "")).begins_with("tavern.") and not _tavern_access:
		_add_unavailable("Tavern services are available only while inside a StarDock tavern.")
		return
	if bool(action.get("local_activity", false)):
		show_activity_history()
		return
	if bool(action.get("local_help", false)):
		show_help()
		return
	if bool(action.get("requires_fresh_sector", false)) and str(_snapshot.get("freshness", {}).get("sector", "")) != "fresh":
		_add_unavailable("Refresh the current sector before planning from its location.")
		return
	var data: Dictionary = action.get("fixed", {}).duplicate(true)
	if bool(action.get("context_sector", false)):
		var current_sector = _snapshot.get("hud", {}).get("sector_id", null)
		if current_sector == null:
			_add_unavailable("The current sector is not available in the confirmed state.")
			return
		if str(action.get("command", "")) == "sector.set_beacon" and int(current_sector) >= 1 and int(current_sector) <= 10:
			_add_unavailable("The server does not permit marker beacons in FedSpace sectors 1–10.")
			return
		data["sector_id"] = int(current_sector)
	if action.has("context_id"):
		var source: Dictionary = _selection.get("data", {})
		var value = source.get("id", source.get("planet_id", source.get("port_id", source.get("ship_id", null))))
		if value == null:
			_add_unavailable("This object has no server identifier, so the command cannot be sent safely.")
			return
		data[str(action["context_id"])] = int(value)
	var fields: Array = action.get("fields", [])
	if fields.is_empty():
		if action.get("mutating", false):
			_form_action = action
			_form_fields.clear()
			_form_dialog.dialog_text = "Send %s? The server remains authoritative." % str(action.get("label", "this command"))
			_form_dialog.ok_button_text = "CONFIRM"
			_form_dialog.popup_centered(Vector2i(440, 180))
			set_meta("pending_data", data)
		else:
			command_requested.emit(str(action["command"]), data, str(action["label"]), false)
		return
	if bool(action.get("idempotency", false)):
		data["idempotency_key"] = str(Time.get_ticks_usec())
	_form_action = action
	_form_fields.clear()
	for child in _form_content.get_children():
		child.queue_free()
	_form_content.add_theme_constant_override("separation", 8)
	for field in fields:
		var row := VBoxContainer.new()
		var label := Label.new()
		label.text = str(field[2])
		row.add_child(label)
		if str(field[1]) == "boolean":
			var check := CheckBox.new()
			check.button_pressed = str(field[3]) == "true"
			row.add_child(check)
			_form_fields[str(field[0])] = check
		else:
			var input := LineEdit.new()
			input.text = str(field[3])
			input.placeholder_text = str(field[2])
			if field.size() > 4:
				input.max_length = int(field[4])
			row.add_child(input)
			_form_fields[str(field[0])] = input
		_form_content.add_child(row)
	_form_dialog.dialog_text = "Enter the requested details."
	_form_dialog.ok_button_text = "SEND"
	_form_dialog.popup_centered(Vector2i(480, 330))
	set_meta("pending_data", data)

func _confirm_logout() -> void:
	command_requested.emit("auth.logout", {}, "Log out", true)

func _submit_form() -> void:
	var data: Dictionary = get_meta("pending_data", {}).duplicate(true)
	for key in _form_fields:
		var control: Control = _form_fields[key]
		if control is CheckBox:
			data[key] = (control as CheckBox).button_pressed
		elif control is LineEdit:
			var text: String = (control as LineEdit).text.strip_edges()
			if text.is_empty():
				_add_unavailable("Every visible field must be completed before sending.")
				return
			if str(_field_type(key)) == FIELD_INT:
				if not text.is_valid_int() or int(text) <= 0:
					_add_unavailable("Enter a positive whole number for %s." % key)
					return
				data[key] = int(text)
			else:
				data[key] = text
	if bool(_form_action.get("duplicate_mail_read_id", false)):
		data["id"] = data.get("mail_id", 0)
	if _form_action.has("preference_key"):
		data = {"items": [{"key": str(_form_action["preference_key"]), "type": str(_form_action.get("preference_type", "string")), "value": data.get("value")}]} 
	command_requested.emit(str(_form_action["command"]), data, str(_form_action.get("label", "Command")), bool(_form_action.get("mutating", false)))

func _field_type(key: String) -> String:
	for field in _form_action.get("fields", []):
		if str(field[0]) == key:
			return str(field[1])
	return FIELD_TEXT

func _style_button(button: Button, primary: bool) -> void:
	button.add_theme_color_override("font_color", Color(0.92, 0.94, 0.9))
	button.add_theme_color_override("font_hover_color", Color(0.98, 0.97, 0.88))
	button.add_theme_color_override("font_disabled_color", Color(0.48, 0.56, 0.59))
	button.add_theme_stylebox_override("normal", _panel_style(Color(0.026, 0.072, 0.09, 0.98) if primary else Color(0.018, 0.042, 0.058, 0.98), Color(0.24, 0.58, 0.61, 0.85) if primary else Color(0.16, 0.34, 0.39, 0.76)))
	button.add_theme_stylebox_override("hover", _panel_style(Color(0.04, 0.13, 0.15, 1.0), Color(0.45, 0.79, 0.76, 0.95)))
	button.add_theme_stylebox_override("pressed", _panel_style(Color(0.05, 0.18, 0.19, 1.0), Color(0.55, 0.83, 0.74, 0.98)))
	button.add_theme_stylebox_override("disabled", _panel_style(Color(0.018, 0.034, 0.044, 0.92), Color(0.13, 0.24, 0.28, 0.55)))
	button.add_theme_stylebox_override("focus", _panel_style(Color(0.028, 0.09, 0.1, 1.0), Color(0.93, 0.69, 0.34, 1.0)))

func _panel_style(fill: Color, border: Color) -> StyleBoxFlat:
	var style := StyleBoxFlat.new()
	style.bg_color = fill
	style.border_color = border
	style.set_border_width_all(1)
	style.set_corner_radius_all(7)
	style.content_margin_left = 10
	style.content_margin_right = 10
	style.content_margin_top = 5
	style.content_margin_bottom = 5
	return style
