extends PanelContainer

const TwProtocol = preload("res://Protocol.gd")
const DialogLayout = preload("res://DialogLayout.gd")
const PlanetCombatPresenter = preload("res://PlanetCombatPresenter.gd")
const FerengiPresenter = preload("res://FerengiPresenter.gd")

signal command_requested(command: String, data: Dictionary, label: String, mutating: bool)
signal reduced_motion_toggle_requested
signal hud_settings_requested
signal shortcut_binding_prompt(message: String)

const LOCAL_PREFERENCES_PATH := "user://client_ui_preferences.cfg"

const FIELD_INT := "integer"
const FIELD_NONNEGATIVE_INT := "nonnegative_integer"
const FIELD_TEXT := "text"
const TAB_CATEGORIES := ["NAVIGATION", "SECTOR", "TRADING", "COMPUTER", "COMMUNICATIONS"]
const COMMANDS := {
	"COMPUTER": [
		{"label": "Help · controls and command flow", "local_help": true},
		{"label": "Player information", "command": "player.my_info"},
		{"label": "Ship information", "command": "ship.info"},
		{"label": "Owned ship roster", "command": "ship.list", "fixed": {}},
		{"label": "Transfer owned ship by roster ID", "command": "ship.transfer", "fields": [["ship_id", FIELD_INT, "Ship ID from owned ship roster", ""], ["target_player_id", FIELD_INT, "Recipient player ID", ""]], "mutating": true, "tooltip": "Use a ship_id returned by Owned ship roster; the server validates ownership and performs the atomic transfer."},
		{"label": "Current sector information", "command": "sector.info", "fixed": {}},
		{"label": "Detailed current-sector scan", "command": "sector.scan", "fixed": {}, "requires_fresh_sector": true},
		{"label": "Holographic adjacent-sector scan", "command": "sector.scan", "fixed": {"mode": "holographic"}, "requires_fresh_sector": true},
		{"label": "Ship towing status", "command": "ship.tow.status", "fixed": {}},
		{"label": "Release current tow beam", "command": "ship.tow", "fixed": {}, "mutating": true, "tooltip": "Releases the active tow relationship if your ship is towing another vessel."},
		{"label": "Repair ship at shipyard", "command": "ship.repair", "mutating": true, "requires_shipyard_dock": true, "tooltip": "Repair restores hull to 100; the server checks the final cost and your credits."},
		{"label": "Rename ship", "command": "ship.rename", "fields": [["name", FIELD_TEXT, "New ship name", ""]], "mutating": true, "tooltip": "Renames your active ship; the server validates ownership and length."},
		{"label": "Scan adjacent sectors", "command": "sector.scan.density", "fixed": {}, "context_sector": true, "requires_fresh_sector": true},
		{"label": "Sector density scan", "command": "sector.scan.density", "fixed": {}, "context_sector": true, "requires_fresh_sector": true},
		{"label": "Trade history", "command": "trade.history", "fixed": {"limit": 20}},
		{"label": "Outstanding fines", "command": "fine.list", "fixed": {}},
		{"label": "Pay a fine", "command": "fine.pay", "fields": [["fine_id", FIELD_INT, "Fine ID from outstanding fines", ""]], "mutating": true, "tooltip": "The server verifies fine ownership and the available credit balance."},
		{"label": "Police status", "command": "police.status", "fixed": {}},
		{"label": "Offer a bribe", "command": "police.bribe", "fields": [["amount", FIELD_INT, "Bribe amount in credits", ""]], "mutating": true, "tooltip": "The server decides whether the offer is accepted and applies any consequences."},
		{"label": "Surrender to authorities", "command": "police.surrender", "fixed": {}, "mutating": true},
		{"label": "Player rankings", "command": "player.rankings", "fixed": {"by": "net_worth", "limit": 10}},
		{"label": "Shipyard catalogue", "command": "shipyard.list", "requires_shipyard_dock": true},
		{"label": "Sell current ship · unavailable", "unavailable_reason": "The authoritative shipyard sale handler is not implemented yet; no request will be sent."},
		{"label": "Hardware catalogue", "command": "hardware.list", "requires_supported_dock": true, "tooltip": "Dock at a supported hardware port first."},
		{"label": "Buy ship hardware", "command": "hardware.buy", "requires_supported_dock": true, "fields": [["code", FIELD_TEXT, "Hardware code (from catalogue)", ""], ["quantity", FIELD_INT, "Quantity", "1"]], "mutating": true, "idempotency": true, "tooltip": "Dock first; the server catalogue supplies valid hardware codes."},
		{"label": "Ship equipment upgrade · unavailable", "unavailable_reason": "The server exposes no authoritative upgrade catalogue. Use Hardware catalogue or Shipyard catalogue so eligibility and prices come from the server."},
		{"label": "Ferengi traders and open offers", "command": "ferengi.traders", "fixed": {}},
		{"label": "Accept Ferengi offer", "command": "ferengi.deal.accept", "fields": [["deal_id", FIELD_INT, "Deal ID from trader listing", ""]], "mutating": true},
		{"label": "Reject Ferengi offer", "command": "ferengi.deal.reject", "fields": [["deal_id", FIELD_INT, "Deal ID from trader listing", ""]], "mutating": true},
		{"label": "Trade routes", "command": "player.computer.recommend_routes", "tooltip": "Use known port profiles and current estimated price spreads. Prices can change before you arrive.", "fields": [["max_hops_between", FIELD_INT, "Maximum hops between ports", "10"], ["require_two_way", "boolean", "Only show pairs with trade in both directions", "false"], ["limit", FIELD_INT, "Result limit", "10"]]},
	],
	"COMMUNICATIONS": [
		{"label": "Recent server events", "local_activity": true},
		{"label": "Create sell offer", "command": "trade.offer", "fixed": {"mode": "sell"}, "fields": [["to_player_id", FIELD_INT, "Recipient player ID", ""], ["commodity", FIELD_TEXT, "Three-character commodity code", "ORE"], ["quantity", FIELD_INT, "Quantity", "1"], ["price", FIELD_NONNEGATIVE_INT, "Price per unit (0 or more)", "0"]], "mutating": true, "idempotency": true, "tooltip": "Offers do not reserve cargo or credits; acceptance rechecks both players."},
		{"label": "Create buy offer", "command": "trade.offer", "fixed": {"mode": "buy"}, "fields": [["to_player_id", FIELD_INT, "Recipient player ID", ""], ["commodity", FIELD_TEXT, "Three-character commodity code", "ORE"], ["quantity", FIELD_INT, "Quantity", "1"], ["price", FIELD_NONNEGATIVE_INT, "Price per unit (0 or more)", "0"]], "mutating": true, "idempotency": true, "tooltip": "Offers do not reserve cargo or credits; acceptance rechecks both players."},
		{"label": "Accept player offer by ID", "command": "trade.accept", "fields": [["offer_id", FIELD_INT, "Offer ID supplied by its sender", ""]], "mutating": true},
		{"label": "Cancel player offer by ID", "command": "trade.cancel", "fields": [["offer_id", FIELD_INT, "Your offer ID", ""]], "mutating": true},
		{"label": "Chat history", "command": "chat.history", "fixed": {"limit": 20}},
		{"label": "Broadcast message", "command": "chat.broadcast", "fields": [["message", FIELD_TEXT, "Message", ""]], "mutating": true},
		{"label": "Private message", "command": "chat.send", "fields": [["to_player", FIELD_TEXT, "Recipient player name", ""], ["message", FIELD_TEXT, "Message", ""]], "mutating": true},
		{"label": "Active bounties", "command": "bounty.list", "fixed": {}},
		{"label": "Notices", "command": "notice.list"},
		{"label": "Acknowledge notice", "command": "notice.ack", "fields": [["notice_id", FIELD_INT, "Unread notice ID", ""]], "mutating": true},
		{"label": "Mail inbox", "command": "mail.inbox", "fixed": {}},
		{"label": "Online players", "command": "player.list_online"},
	],
	"HELP": [
		{"label": "Controls and command flow", "local_help": true},
	],
	"OPERATIONS": [
		{"label": "Combat status", "command": "combat.status"},
		{"label": "Deploy fighters", "command": "combat.deploy_fighters", "fields": [["amount", FIELD_INT, "Number of fighters", "1"], ["offense", FIELD_INT, "Offense percentage", "50"]], "mutating": true},
		{"label": "Deploy mines", "command": "combat.deploy_mines", "fields": [["amount", FIELD_INT, "Number of mines", "1"], ["offense", FIELD_INT, "Offense mode (1 toll, 2 defend, 3 attack)", "2"]], "mutating": true},
		# The list schemas are empty objects; the server scopes results to the
		# authenticated player. Main.gd supplies confirmed HUD sector context
		# only when rendering returned assets for recall.
		{"label": "List deployed fighters · recall", "command": "deploy.fighters.list"},
		{"label": "List deployed mines · recall", "command": "deploy.mines.list"},
		{"label": "Sweep mines", "command": "combat.sweep_mines", "context_sector": true, "requires_fresh_sector": true, "mutating": true, "tooltip": "Sweep mines in the confirmed current sector."},
		{"label": "Jettison cargo", "command": "trade.jettison", "fields": [["commodity", FIELD_TEXT, "Commodity (ore, organics, equipment, colonists, etc.)", "ore"], ["quantity", FIELD_INT, "Quantity to jettison", "1"]], "form_notice": "Permanently destroys this cargo and consumes one turn. The server will validate the amount.", "mutating": true},
		{"label": "Deploy marker beacon", "command": "sector.set_beacon", "fields": [["text", FIELD_TEXT, "Beacon text (up to 80 characters)", "", 80]], "context_sector": true, "requires_fresh_sector": true, "mutating": true},
		{"label": "Create planet with Genesis Torpedo", "command": "planet.genesis_create", "fields": [["name", FIELD_TEXT, "New planet name", ""]], "context_sector": true, "requires_fresh_sector": true, "fixed": {"owner_entity_type": "player"}, "mutating": true, "idempotency": true},
	],
	"FINANCES": [
		{"label": "Bank balance", "command": "bank.balance"},
		{"label": "Bank statement", "command": "bank.statement", "fields": [["limit", FIELD_INT, "Number of entries", "20"]]},
		{"label": "Deposit credits", "command": "bank.deposit", "fields": [["amount", FIELD_INT, "Amount", ""]], "mutating": true},
		{"label": "Withdraw credits", "command": "bank.withdraw", "fields": [["amount", FIELD_INT, "Amount", ""]], "mutating": true},
		{"label": "Transfer credits to player", "command": "bank.transfer", "fields": [["to_player_id", FIELD_INT, "Recipient player ID", ""], ["amount", FIELD_INT, "Amount", ""]], "mutating": true},
		{"label": "Bank history", "command": "bank.history"},
		{"label": "Bank leaderboard", "command": "bank.leaderboard"},
		{"label": "Standing orders", "command": "bank.orders.list"},
		{"label": "Create standing order", "command": "bank.orders.create", "fields": [["recipient_id", FIELD_INT, "Recipient player ID", ""], ["amount", FIELD_INT, "Amount", ""], ["frequency", FIELD_TEXT, "Payment frequency", "daily"]], "mutating": true},
		{"label": "Cancel standing order", "command": "bank.orders.cancel", "fields": [["order_id", FIELD_INT, "Standing order ID", ""]], "mutating": true},
	],
	"NEWS & RECORDS": [
		{"label": "News feed", "command": "news.get_feed"},
		{"label": "Mark news read", "command": "news.mark_feed_read", "mutating": true},
		{"label": "Insurance policies · server stub", "unavailable_reason": "Insurance persistence is not available from the authoritative server handler yet; no request will be sent."},
		{"label": "Buy insurance · server stub", "unavailable_reason": "Insurance persistence is not available from the authoritative server handler yet; no request will be sent."},
		{"label": "File insurance claim · server stub", "unavailable_reason": "Insurance persistence is not available from the authoritative server handler yet; no request will be sent."},
		{"label": "Subscriptions", "command": "subscribe.list"},
	],
	"CORPORATION": [
		{"label": "Corporation status", "command": "corp.status", "requires_corporation": true},
		{"label": "Member roster", "command": "corp.roster", "context_player_corp_id": true},
		{"label": "Corporation directory", "command": "corp.list"},
		{"label": "Treasury balance", "command": "corp.balance", "requires_corporation": true},
		{"label": "Treasury statement", "command": "corp.statement", "requires_corporation": true},
		{"label": "Create corporation", "command": "corp.create", "fields": [["name", FIELD_TEXT, "Corporation name", ""]], "mutating": true},
		{"label": "Join corporation", "command": "corp.join", "fields": [["corp_id", FIELD_INT, "Corporation ID", ""]], "mutating": true},
		{"label": "Invite player", "command": "corp.invite", "fields": [["target_player_id", FIELD_INT, "Player ID", ""]], "mutating": true, "requires_corporation": true},
		{"label": "Kick member", "command": "corp.kick", "fields": [["target_player_id", FIELD_INT, "Player ID", ""]], "mutating": true, "requires_corporation": true},
		{"label": "Deposit to treasury", "command": "corp.deposit", "fields": [["amount", FIELD_INT, "Credits", ""]], "mutating": true, "requires_corporation": true},
		{"label": "Withdraw from treasury", "command": "corp.withdraw", "fields": [["amount", FIELD_INT, "Credits", ""]], "mutating": true, "requires_corporation": true},
		{"label": "Transfer CEO role", "command": "corp.transfer_ceo", "fields": [["target_player_id", FIELD_INT, "New CEO player ID", ""]], "mutating": true, "requires_corporation": true},
		{"label": "Leave corporation", "command": "corp.leave", "mutating": true, "requires_corporation": true},
		{"label": "Dissolve corporation", "command": "corp.dissolve", "mutating": true, "requires_corporation": true},
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
		{"label": "Transwarp to sector", "command": "move.transwarp", "fields": [["to_sector_id", FIELD_INT, "Destination sector", ""]], "requires_fresh_sector": true, "mutating": true, "tooltip": "Refresh the confirmed sector first; the server checks transwarp equipment, destination and turn cost."},
		{"label": "Pathfind to sector", "command": "move.pathfind", "fields": [["to_sector_id", FIELD_INT, "Destination sector", ""]], "requires_fresh_sector": true, "tooltip": "Ask the server for a route without moving the ship."},
		{"label": "Bookmarks", "command": "nav.bookmark.list"},
		{"label": "Add current sector bookmark", "command": "nav.bookmark.add", "fields": [["name", FIELD_TEXT, "Bookmark name", ""]], "context_sector": true, "mutating": true},
		{"label": "Remove bookmark", "command": "nav.bookmark.remove", "fields": [["name", FIELD_TEXT, "Bookmark name", ""]], "mutating": true},
		{"label": "Avoid list", "command": "nav.avoid.list"},
		{"label": "Avoid a sector", "command": "nav.avoid.add", "fields": [["sector_id", FIELD_INT, "Sector ID", ""]], "mutating": true},
		{"label": "Remove sector from avoid list", "command": "nav.avoid.remove", "fields": [["sector_id", FIELD_INT, "Sector ID", ""]], "mutating": true},
		{"label": "Autopilot status", "command": "move.autopilot.status"},
		{"label": "Plot route to sector", "command": "move.autopilot.start", "fields": [["to_sector_id", FIELD_INT, "Destination sector", ""]], "requires_fresh_sector": true, "mutating": true, "tooltip": "Saves this server route and replaces any existing saved route; no movement begins until you engage Autonav."},
		{"label": "Autopilot · stop at next sector", "command": "move.autopilot.control", "fixed": {"action": "stop_at_next"}, "mutating": true},
		{"label": "Autopilot · continue route", "command": "move.autopilot.control", "fixed": {"action": "continue"}, "mutating": true},
		{"label": "Autopilot · express mode", "command": "move.autopilot.control", "fixed": {"action": "express"}, "mutating": true, "tooltip": "Express keeps each hop server-validated; it does not skip per-hop checks."},
		{"label": "Autopilot · stop saved route now", "command": "move.autopilot.stop", "fixed": {}, "mutating": true},
	],
	"SETTINGS & NOTES": [
		{"label": "Reduced motion · OFF", "local_reduced_motion": true},
		{"label": "Choose HUD status items", "local_hud_settings": true},
		{"label": "Change password", "command": "auth.change_password", "fields": [["old_password", "password", "Current password", ""], ["new_password", "password", "New password", ""], ["confirm_password", "password", "Confirm new password", ""]], "mutating": true},
		{"label": "View settings and preferences", "command": "player.get_settings"},
		{"label": "View preferences", "command": "player.get_prefs"},
		{"label": "Use petty cash for trades by default", "command": "player.set_trade_account_preference", "fixed": {"preference": 0}, "mutating": true, "tooltip": "The server saves petty cash as the default trade account."},
		{"label": "Use bank for trades by default", "command": "player.set_trade_account_preference", "fixed": {"preference": 1}, "mutating": true, "tooltip": "The server saves bank as the default trade account."},
		{"label": "Set 24-hour clock preference", "command": "player.set_prefs", "fields": [["value", "boolean", "Use 24-hour clock", "true"]], "preference_key": "ui.clock_24h", "preference_type": "bool", "mutating": true},
		{"label": "Set locale preference", "command": "player.set_prefs", "fields": [["value", FIELD_TEXT, "Locale (for example en-GB)", "en-GB"]], "preference_key": "ui.locale", "preference_type": "string", "mutating": true},
		{"label": "List notes", "command": "notes.list", "fixed": {}},
		{"label": "Save note", "command": "notes.set", "fields": [["scope", FIELD_TEXT, "Note scope", "player"], ["key", FIELD_TEXT, "Note key", ""], ["note", FIELD_TEXT, "Note text", ""]], "mutating": true},
		{"label": "Delete note", "command": "notes.delete", "fields": [["scope", FIELD_TEXT, "Note scope", "player"], ["key", FIELD_TEXT, "Note key", ""]], "mutating": true},
	],
	"MAIL & SUBSCRIPTIONS": [
		{"label": "Mail inbox", "command": "mail.inbox", "fixed": {}},
		{"label": "Read mail by ID", "command": "mail.read", "fields": [["mail_id", FIELD_INT, "Mail ID", ""]], "mutating": true},
		{"label": "Send mail", "command": "mail.send", "fields": [["to", FIELD_TEXT, "Recipient name", ""], ["subject", FIELD_TEXT, "Subject", ""], ["body", FIELD_TEXT, "Message body", ""]], "mutating": true, "idempotency": true},
		{"label": "Delete mail", "command": "mail.delete", "fields": [["mail_id", FIELD_INT, "Mail ID", ""]], "mutating": true},
		{"label": "Subscription catalogue", "command": "subscribe.catalog"},
		{"label": "Add subscription", "command": "subscribe.add", "fields": [["topic", FIELD_TEXT, "Topic pattern (see subscription catalogue)", ""]], "mutating": true},
		{"label": "Remove subscription", "command": "subscribe.remove", "fields": [["topic", FIELD_TEXT, "Topic to remove (see subscription list)", ""]], "mutating": true},
	],
	"SESSION": [
		{"label": "Log out to login screen", "local_logout": true},
	],
}

var _snapshot: Dictionary = {}
var _selection: Dictionary = {}
var _category_buttons: OptionButton
var _search_input: LineEdit
var _action_list: Container
var _action_scroll: ScrollContainer
var _title: Label
var _active_category := "COMPUTER"
var _settings_mode := false
var _busy := false
var _form_dialog: ConfirmationDialog
var _result_dialog: AcceptDialog
var _result_text: Label
var _logout_dialog: ConfirmationDialog
var _form_fields: Dictionary = {}
var _form_action: Dictionary = {}
var _form_content: VBoxContainer
var _form_scroll: ScrollContainer
var _inline_form_panel: PanelContainer
var _inline_form_content: VBoxContainer
var _inline_submit: Button
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
var _ferengi_offer_available := false

func _action_without_labels(actions: Array, hidden_labels: Array[String]) -> Array:
	var result: Array = []
	for action in actions:
		if str(action.get("label", "")) not in hidden_labels:
			result.append(action)
	return result

func _player_has_corporation() -> bool:
	var player: Variant = _snapshot.get("player", {})
	if not player is Dictionary:
		return false
	var corp_id: Variant = player.get("corp_id", 0)
	return (typeof(corp_id) == TYPE_INT or typeof(corp_id) == TYPE_FLOAT) and int(corp_id) > 0

func set_ferengi_offer_available(available: bool) -> void:
	_ferengi_offer_available = available
	_render_actions()

func _actions_for_category(category: String) -> Array:
	# The server-facing command definitions remain grouped by implementation
	# legacy for compatibility. The visible catalogue is grouped by the
	# player's intent instead.
	var actions: Array = []
	match category:
		"NAVIGATION":
			actions.append_array(COMMANDS.get("NAVIGATION", []))
			for action in COMMANDS.get("COMPUTER", []):
				var label := str(action.get("label", ""))
				if label in ["Scan adjacent sectors", "Sector density scan", "Holographic adjacent-sector scan"]:
					var scanner: Dictionary = action.duplicate(true)
					if label == "Sector density scan":
						scanner["label"] = "Density scan"
					elif label == "Scan adjacent sectors":
						scanner["label"] = "Scan adjacent sectors"
					actions.append(scanner)
		"SECTOR":
			actions.append_array(COMMANDS.get("OPERATIONS", []))
		"TRADING":
			actions.append_array(COMMANDS.get("FINANCES", []))
			actions.append_array(COMMANDS.get("MARKET", []))
			for action in COMMANDS.get("COMPUTER", []):
				var label := str(action.get("label", ""))
				if label in ["Trade history", "Trade routes"] or (_ferengi_offer_available and label in ["Ferengi traders and open offers", "Accept Ferengi offer", "Reject Ferengi offer"]):
					actions.append(action)
			for action in COMMANDS.get("NEWS & RECORDS", []):
				if str(action.get("label", "")).contains("Insurance"):
					actions.append(action)
		"COMPUTER":
			var hidden: Array[String] = [
				"Help · controls and command flow", "Detailed current-sector scan",
				"Holographic adjacent-sector scan", "Scan adjacent sectors",
				"Sector density scan", "Trade history", "Shipyard catalogue", "Sell current ship · unavailable",
				"Hardware catalogue", "Buy ship hardware",
				"Ship equipment upgrade · unavailable", "Trade routes",
				"Ferengi traders and open offers", "Accept Ferengi offer", "Reject Ferengi offer"
			]
			actions.append_array(_action_without_labels(COMMANDS.get("COMPUTER", []), hidden))
			for action in COMMANDS.get("CORPORATION", []):
				var label := str(action.get("label", ""))
				if not _player_has_corporation() and label in ["Create corporation", "Join corporation"]:
					actions.append(action)
			actions.append_array(COMMANDS.get("TAVERN", []) if _tavern_access else [])
		"CORPORATION":
			if _player_has_corporation():
				actions.append_array(COMMANDS.get("CORPORATION", []))
		"COMMUNICATIONS":
			actions.append_array(COMMANDS.get("COMMUNICATIONS", []))
			actions.append_array(COMMANDS.get("NEWS & RECORDS", []).slice(0, 2))
			actions.append_array(COMMANDS.get("MAIL & SUBSCRIPTIONS", []))
		"SETTINGS & NOTES":
			actions.append_array(COMMANDS.get("SETTINGS & NOTES", []))
			actions.append_array(COMMANDS.get("HELP", []))
			actions.append_array(COMMANDS.get("SESSION", []))
		_:
			actions.append_array(COMMANDS.get(category, []))
	return actions
var _reduced_motion_enabled := false
var _pinned_actions: Array[Dictionary] = []
var _key_bindings: Dictionary = {}
var _awaiting_binding_command := ""
var _owned_ship_ids: Dictionary = {}

func set_owned_ship_roster(data: Dictionary) -> void:
	# Remember only IDs from the latest validated ship.list.response.
	_owned_ship_ids.clear()
	var rows: Variant = data.get("ships", null)
	if not (rows is Array):
		return
	for row in rows:
		if not (row is Dictionary):
			_owned_ship_ids.clear()
			return
		var ship_id: Variant = row.get("ship_id", null)
		# Keep the cache boundary identical to Main.gd's published
		# ship.list.response contract.  An ID without a typed active flag is not
		# enough to establish authoritative lifecycle state for a transfer form.
		if typeof(ship_id) != TYPE_INT or int(ship_id) <= 0 \
			or typeof(row.get("active", null)) != TYPE_BOOL \
			or _owned_ship_ids.has(int(ship_id)):
			_owned_ship_ids.clear()
			return
		_owned_ship_ids[int(ship_id)] = true

func _ready() -> void:
	_load_local_command_preferences()
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
	_search_input = LineEdit.new()
	_search_input.name = "CommandSearch"
	_search_input.placeholder_text = "Search commands"
	_search_input.clear_button_enabled = true
	_search_input.text_changed.connect(func(_value: String) -> void: _render_actions())
	column.add_child(_search_input)
	var scroll := ScrollContainer.new()
	scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	scroll.custom_minimum_size.y = 180
	column.add_child(scroll)
	_action_scroll = scroll
	# Keep the authoritative command registry and dispatch path, but present
	# Computer/other workspace actions as a compact two-column command grid.
	# The old full-width vertical command rail made the Computer tab consume
	# the entire lower half of the game view.
	_action_list = GridContainer.new()
	_action_list.columns = 2
	_action_list.add_theme_constant_override("h_separation", 8)
	_action_list.add_theme_constant_override("v_separation", 6)
	_action_list.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_action_list.add_theme_constant_override("separation", 6)
	scroll.add_child(_action_list)
	resized.connect(_update_command_grid_columns)
	_inline_form_panel = PanelContainer.new()
	_inline_form_panel.name = "InlineCommandForm"
	_inline_form_panel.visible = false
	_inline_form_panel.add_theme_stylebox_override("panel", _panel_style(Color(0.026, 0.071, 0.093, 0.96), Color(0.48, 0.78, 0.7, 0.8)))
	var inline_margin := MarginContainer.new()
	inline_margin.add_theme_constant_override("margin_left", 12)
	inline_margin.add_theme_constant_override("margin_right", 12)
	inline_margin.add_theme_constant_override("margin_top", 10)
	inline_margin.add_theme_constant_override("margin_bottom", 10)
	_inline_form_panel.add_child(inline_margin)
	_inline_form_content = VBoxContainer.new()
	_inline_form_content.add_theme_constant_override("separation", 8)
	inline_margin.add_child(_inline_form_content)
	column.add_child(_inline_form_panel)
	_form_dialog = ConfirmationDialog.new()
	_form_dialog.title = "Command details"
	_form_dialog.confirmed.connect(_submit_form)
	_form_content = VBoxContainer.new()
	_form_content.add_theme_constant_override("separation", 10)
	_form_content.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_form_scroll = ScrollContainer.new()
	_form_scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	_form_scroll.vertical_scroll_mode = ScrollContainer.SCROLL_MODE_AUTO
	_form_scroll.custom_minimum_size = Vector2(0, 220)
	_form_scroll.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_form_scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_form_scroll.add_child(_form_content)
	DialogLayout.attach(_form_dialog, _form_scroll)
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
	DialogLayout.attach(_result_dialog, result_margin)
	_result_text = Label.new()
	_result_text.custom_minimum_size = Vector2(0, 160)
	_result_text.size_flags_horizontal = Control.SIZE_EXPAND_FILL
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
	shipyard_scroll.custom_minimum_size = Vector2(0, 340)
	DialogLayout.attach(_shipyard_dialog, shipyard_scroll)
	_shipyard_rows = VBoxContainer.new()
	_shipyard_rows.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	shipyard_scroll.add_child(_shipyard_rows)
	get_parent().add_child(_shipyard_dialog)
	_shipyard_name_dialog = ConfirmationDialog.new()
	_shipyard_name_dialog.title = "Confirm hull exchange"
	_shipyard_name_dialog.exclusive = false
	_shipyard_name_dialog.ok_button_text = "CONFIRM UPGRADE"
	var shipyard_name_column := VBoxContainer.new()
	shipyard_name_column.add_theme_constant_override("separation", 10)
	DialogLayout.attach(_shipyard_name_dialog, shipyard_name_column)
	var shipyard_note := Label.new()
	shipyard_note.text = "The server will recheck eligibility and final cost before exchanging your hull."
	DialogLayout.prepare_label(shipyard_note)
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
	_update_command_grid_columns()

func _update_command_grid_columns() -> void:
	if not is_instance_valid(_action_list) or not (_action_list is GridContainer):
		return
	# The category selector is the compact command menu. Within a category,
	# use the available width rather than forcing a tall two-column rail; this
	# reaches roughly six readable columns on a 1664px workspace while remaining
	# usable at narrow resolutions.
	var available := maxf(size.x - 36.0, 260.0)
	_action_list.columns = maxi(1, mini(6, int(available / 190.0)))

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
	visible = true

func set_tavern_access(available: bool) -> void:
	_tavern_access = available
	if not available and _active_category == "TAVERN":
		_active_category = "COMPUTER"
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
	var parent := get_parent()
	if parent and parent.has_method("show_information"):
		parent.show_information(label, format_result(label, payload))
	else:
		_result_dialog.title = label
		_result_text.text = format_result(label, payload)
		DialogLayout.popup(_result_dialog, Vector2i(560, 380))

func format_result(label: String, payload: Dictionary) -> String:
	var normalized_label := label.to_lower()
	if normalized_label.contains("chat history"):
		return _format_chat_history_result(payload)
	if normalized_label.contains("mail inbox"):
		return _format_mail_inbox_result(payload)
	if normalized_label.contains("player rankings"):
		return _format_rankings_result(payload)
	if normalized_label.contains("owned ship roster"):
		return _format_ship_roster_result(payload)
	if normalized_label.contains("read mail"):
		return _format_mail_read_result(payload)
	if label.to_lower().contains("ferengi trader"):
		return FerengiPresenter.traders(payload.get("data", payload) if payload.get("data", payload) is Dictionary else {})
	if label.to_lower().contains("bount"):
		return _format_bounty_result(payload)
	if label.to_lower().contains("police") or label.to_lower().contains("surrender") or label.to_lower().contains("bribe"):
		return _format_police_result(payload)
	if label.to_lower().contains("density"):
		return _format_density_result(payload)
	if payload.get("scanner", "") == "holographic":
		return _format_holographic_result(payload)
	if label.to_lower().contains("scan"):
		return _format_scan_result(payload)
	if label.to_lower().contains("news"):
		return _format_news_result(payload)
	if label.to_lower().contains("notice"):
		return _format_notice_result(payload)
	return _format_result(payload)

func _format_chat_history_result(payload: Dictionary) -> String:
	var messages: Variant = payload.get("messages", [])
	if not (messages is Array):
		return "Chat history is unavailable: the server returned no readable messages."
	var lines: Array[String] = []
	for message in messages:
		if not (message is Dictionary):
			continue
		var text := str(message.get("message", "")).strip_edges()
		if text.is_empty():
			continue
		var sender := str(message.get("sender_name", "")).strip_edges()
		if sender.is_empty():
			sender = "Player %s" % str(message.get("sender_id", "unknown"))
		var recipient := str(message.get("recipient_name", "")).strip_edges()
		var route := " -> %s" % recipient if not recipient.is_empty() else ""
		var sent_at := str(message.get("sent_at", "")).strip_edges()
		var timestamp := " [%s]" % sent_at if not sent_at.is_empty() else ""
		lines.append("%s%s%s: %s" % [timestamp, sender, route, text])
	if lines.is_empty():
		return "No chat history yet."
	return "\n".join(lines).substr(0, 5000)

func _format_mail_inbox_result(payload: Dictionary) -> String:
	var items: Variant = payload.get("items", [])
	if not (items is Array):
		return "Mail inbox is unavailable: the server returned no readable messages."
	var lines: Array[String] = []
	for item in items:
		if not (item is Dictionary) or typeof(item.get("id", null)) != TYPE_INT or int(item.get("id", 0)) <= 0:
			continue
		var sender := str(item.get("sender_name", "")).strip_edges()
		if sender.is_empty():
			sender = "Player %s" % str(item.get("sender_id", "unknown"))
		var subject := str(item.get("subject", "")).strip_edges()
		if subject.is_empty():
			subject = "(no subject)"
		var state := "NEW" if item.get("read_at", null) == null else "READ"
		lines.append("[%s] #%d · From: %s · %s" % [state, int(item["id"]), sender, subject])
	if lines.is_empty():
		return "Inbox is empty."
	return "\n".join(lines).substr(0, 5000)

func _format_mail_read_result(payload: Dictionary) -> String:
	if typeof(payload.get("id", null)) != TYPE_INT or int(payload.get("id", 0)) <= 0:
		return "Mail could not be displayed: the server returned no authoritative message ID."
	var sender := str(payload.get("sender_name", "")).strip_edges()
	if sender.is_empty():
		sender = "Player %s" % str(payload.get("sender_id", "unknown"))
	var subject := str(payload.get("subject", "")).strip_edges()
	if subject.is_empty():
		subject = "(no subject)"
	var body := str(payload.get("body", "")).strip_edges()
	if body.is_empty():
		body = "(empty message)"
	return "Mail #%d\nFrom: %s\nSubject: %s\n\n%s" % [int(payload["id"]), sender, subject, body]

func _format_rankings_result(payload: Dictionary) -> String:
	var rankings: Variant = payload.get("rankings", null)
	if not rankings is Array:
		return "Player rankings are unavailable: the server returned no readable rankings."
	if rankings.is_empty():
		return "No player rankings are available yet."
	var lines: Array[String] = []
	for row in rankings:
		if not row is Dictionary:
			continue
		var player_id: Variant = row.get("player_id", null)
		var name: Variant = row.get("name", null)
		var experience: Variant = row.get("experience", null)
		var alignment: Variant = row.get("alignment", null)
		if typeof(player_id) != TYPE_INT or int(player_id) <= 0 or typeof(name) != TYPE_STRING or str(name).strip_edges().is_empty():
			continue
		if typeof(experience) != TYPE_INT or typeof(alignment) != TYPE_INT:
			continue
		lines.append("%d. %s · XP %d · alignment %d" % [lines.size() + 1, str(name), int(experience), int(alignment)])
	if lines.is_empty():
		return "No readable player rankings were returned."
	return "\n".join(lines).substr(0, 5000)

func _format_ship_roster_result(payload: Dictionary) -> String:
	var ships: Variant = payload.get("ships", null)
	if not ships is Array:
		return "Owned ship roster is unavailable: the server returned no readable ships."
	if ships.is_empty():
		return "No ships are currently owned."
	var lines: Array[String] = ["Owned ships (use the server ship ID for transfer):"]
	var seen_ids := {}
	for ship in ships:
		if not ship is Dictionary:
			return "Owned ship roster is unavailable: the server returned an invalid ship row."
		var ship_id: Variant = ship.get("ship_id", null)
		var active: Variant = ship.get("active", null)
		if typeof(ship_id) != TYPE_INT or int(ship_id) <= 0 or typeof(active) != TYPE_BOOL:
			return "Owned ship roster is unavailable: the server returned ambiguous ship identity or active state."
		if seen_ids.has(int(ship_id)):
			return "Owned ship roster is unavailable: the server returned duplicate ship identity."
		seen_ids[int(ship_id)] = true
		var name := str(ship.get("name", "Unnamed")).strip_edges()
		if name.is_empty():
			name = "Unnamed"
		var sector: Variant = ship.get("sector_id", "unknown")
		var state := "ACTIVE" if active else "inactive"
		lines.append("Ship %d · %s · sector %s · %s" % [int(ship_id), name, str(sector), state])
	return "\n".join(lines).substr(0, 5000)

func _format_police_result(payload: Dictionary) -> String:
	if str(payload.get("status", "")) != "ok" and payload.has("error"):
		return _format_result(payload)
	var data: Variant = payload.get("data", payload)
	if not (data is Dictionary):
		return "The server returned an unreadable police response."
	if data.has("message"):
		var lines: Array[String] = [str(data.get("message", ""))]
		if data.has("suspicion_after"):
			lines.append("Suspicion: %s" % str(data.get("suspicion_after")))
		if data.has("wanted_level_after"):
			lines.append("Wanted level: %s" % str(data.get("wanted_level_after")))
		return "\n".join(lines)
	var jurisdiction := "none" if data.get("cluster_id") == null else str(data.get("cluster_id"))
	return "Police status\nJurisdiction cluster: %s\nSuspicion: %s\nWanted level: %s\nBanned: %s" % [
		jurisdiction,
		str(data.get("suspicion", "unreported")),
		str(data.get("wanted_level", "unreported")),
		str(data.get("banned", "unreported")),
	]

func _format_bounty_result(payload: Dictionary) -> String:
	var data: Variant = payload.get("data", payload)
	if not (data is Dictionary):
		return "The server returned an unreadable bounty list."
	var bounties: Variant = data.get("bounties", [])
	if not (bounties is Array):
		return "The server returned an unreadable bounty list."
	if bounties.is_empty():
		return "There are no active bounties."
	var lines: Array[String] = []
	for bounty in bounties:
		if not (bounty is Dictionary):
			continue
		var bounty_id: Variant = bounty.get("bounty_id")
		var target_id: Variant = bounty.get("target_id")
		var reward: Variant = bounty.get("reward")
		if not (bounty_id is int and target_id is int and reward is int):
			continue
		var target_name := str(bounty.get("target_name", "")).strip_edges()
		if target_name.is_empty():
			target_name = "Player %s" % target_id
		lines.append("#%s · %s · %s CR" % [bounty_id, target_name, str(reward)])
	if lines.is_empty():
		return "No readable bounty entries were returned."
	return "\n".join(lines).substr(0, 5000)

func _format_news_result(payload: Dictionary) -> String:
	var data: Variant = payload.get("data", payload)
	if not (data is Dictionary):
		return "The server returned an unreadable news response."
	var articles: Variant = data.get("articles", [])
	if not (articles is Array) or articles.is_empty():
		return "No news articles are available."
	var lines: Array[String] = []
	for article in articles:
		if not (article is Dictionary):
			continue
		var headline := str(article.get("headline", "News")).strip_edges()
		var category := str(article.get("category", "General")).strip_edges()
		var timestamp := str(article.get("timestamp", article.get("created_at", "Time unavailable"))).strip_edges()
		var body := str(article.get("body", "")).strip_edges()
		lines.append("%s · %s · %s" % [headline if not headline.is_empty() else "News", category, timestamp])
		if not body.is_empty():
			lines.append(body)
	if lines.is_empty():
		return "No readable news articles were returned."
	return "\n\n".join(lines).substr(0, 5000)

func _format_notice_result(payload: Dictionary) -> String:
	var data: Variant = payload.get("data", payload)
	if not (data is Dictionary):
		return "The server returned an unreadable notice response."
	var items: Variant = data.get("items", data.get("notices", []))
	if not (items is Array) or items.is_empty():
		return "No current notices."
	var lines: Array[String] = []
	for notice in items:
		if not (notice is Dictionary):
			continue
		var title := str(notice.get("title", "Notice")).strip_edges()
		var scope := str(notice.get("scope", "global")).strip_edges()
		var read_state := "READ" if notice.has("seen_at") and notice.get("seen_at") != null else "NEW"
		var body := str(notice.get("body", "")).strip_edges()
		lines.append("[%s] %s · %s" % [read_state, title if not title.is_empty() else "Notice", scope])
		if not body.is_empty():
			lines.append(body)
	if lines.is_empty():
		return "No readable notices were returned."
	return "\n\n".join(lines).substr(0, 5000)

func _format_scan_result(payload: Dictionary) -> String:
	if str(payload.get("status", "ok")) != "ok":
		return _format_visibility_refusal(payload, "Scan")
	var data = payload.get("data", payload)
	if not (data is Dictionary):
		return "No scan data was returned by the server."
	# An explicit schema identity is authoritative even when malformed. Do not
	# fall through to a legacy alias: a contradictory payload must not become a
	# displayable scan merely because its secondary field looks valid.
	var sector_id = data["sector_id"] if data.has("sector_id") else data.get("id", null)
	# A scan is only displayable when it identifies the server-owned sector.
	# Keep the presenter safe even when called outside Main.gd's command gate.
	if not _authoritative_density_id(sector_id):
		return "No scan data was returned by the server (authoritative sector ID unavailable)."
	var confirmed_sector_id := _authoritative_positive_id(_snapshot.get("hud", {}).get("sector_id", null))
	if confirmed_sector_id > 0 and int(sector_id) != confirmed_sector_id:
		# A delayed or misrouted success response is not visibility evidence for
		# the player's confirmed location. Do not disclose it from this presenter.
		return "No scan data was returned by the server (sector does not match the confirmed location)."
	# Visibility names are server-provided display data. Do not stringify a
	# schema-mismatched object/number into the scan card; that would present
	# untrusted payload text as authoritative visibility information.
	var raw_name: Variant = data.get("name", null)
	var name: String = raw_name.strip_edges() if raw_name is String and not raw_name.strip_edges().is_empty() else "Unknown"
	var adjacent_known: bool = data.has("adjacent") or data.has("adjacent_sectors")
	var adjacent = data.get("adjacent", data.get("adjacent_sectors", []))
	var raw_counts = data.get("counts", {})
	var counts: Dictionary = raw_counts if raw_counts is Dictionary else {}
	var ports_are_objects := data.get("ports") is Array and _authoritative_port_rows(data["ports"])
	var port_known: bool = ports_are_objects or (
		data.get("port") is Dictionary and typeof(data["port"].get("present")) == TYPE_BOOL)
	var port_present := false
	if ports_are_objects:
		port_present = not data["ports"].is_empty()
	elif data.get("port") is Dictionary and typeof(data["port"].get("present")) == TYPE_BOOL:
		# Do not coerce schema-mismatched strings/numbers into visibility.
		port_present = data["port"]["present"]
	var port_status := ("present" if port_present else "none") if port_known else "not reported"
	var lines: Array[String] = ["Sector %s · %s" % [str(sector_id), name], "", "Adjacent: %s" % (_scan_list(adjacent) if adjacent_known else "not reported"), "Port: %s (class/details not disclosed by scan)" % port_status, "Ships: %s" % _scan_count(data, counts, "ships", ["ship_count", "ships_count"]), "Planets: %s" % _scan_count(data, counts, "planets", ["planet_count", "planets_count"]), "Mines: %s" % _scan_count(data, counts, "mines"), "Fighters: %s" % _scan_count(data, counts, "fighters")]
	# Beacon text is server-owned visibility data. Do not stringify a malformed
	# object/number into the scan card: that would present schema-mismatched
	# payload text as canonical information. An absent or invalid beacon is
	# deliberately rendered as unavailable, not guessed from another field.
	var raw_beacon: Variant = data.get("beacon", null)
	var beacon: String = raw_beacon.strip_edges() if raw_beacon is String else ""
	lines.append("Beacon: %s" % (beacon if not beacon.is_empty() else "unavailable"))
	return "\n".join(lines)

func _format_holographic_result(payload: Dictionary) -> String:
	var data: Variant = payload.get("data", payload)
	if not (data is Dictionary) or data.get("scanner", "") != "holographic":
		return "No holographic scan data was returned by the server."
	var sectors: Variant = data.get("sectors", [])
	if not (sectors is Array):
		return "No holographic scan data was returned by the server."
	var lines: Array[String] = ["HOLOGRAPHIC SCAN", ""]
	for sector in sectors:
		if not (sector is Dictionary) or not _authoritative_density_id(sector.get("sector_id", null)):
			return "Holographic scan returned malformed sector data."
		var sector_title := "Sector %s" % str(int(sector["sector_id"]))
		var raw_sector_name: Variant = sector.get("name", null)
		if raw_sector_name is String and not raw_sector_name.strip_edges().is_empty():
			sector_title += " · " + raw_sector_name.strip_edges()
		lines.append(sector_title)
		var ports: Variant = sector.get("ports", [])
		var ships: Variant = sector.get("ships_present", [])
		var planets: Variant = sector.get("celestial_objects", [])
		lines.append("  Ports: %s" % (str(ports.size()) if ports is Array else "unknown"))
		lines.append("  Ships: %s" % (str(ships.size()) if ships is Array else "unknown"))
		lines.append("  Planets: %s" % (str(planets.size()) if planets is Array else "unknown"))
		var raw_beacon: Variant = sector.get("beacon", null)
		if raw_beacon is String and not raw_beacon.strip_edges().is_empty():
			lines.append("  Beacon: " + raw_beacon.strip_edges())
	return "\n".join(lines) if sectors.size() > 0 else "No adjacent sectors were returned by the holographic scan."

func _authoritative_port_rows(value: Variant) -> bool:
	# A non-empty array is not visibility evidence when it contains malformed
	# rows. Fail closed instead of turning an untrusted payload into "Port:
	# present" in the scan presenter.
	if not (value is Array):
		return false
	for row in value:
		if not (row is Dictionary):
			return false
	return true

func _scan_count(data: Dictionary, counts: Dictionary, key: String, aliases: Array = []) -> String:
	for field in [key] + aliases:
		var value = counts.get(field, data.get(field, null))
		# Scan measurements are authoritative JSON integers. Do not coerce
		# booleans, strings, or floating-point values into displayable counts.
		if _is_integral_number(value) and int(value) >= 0:
			return str(int(value))
	return "unknown"

func _scan_list(value: Variant) -> String:
	if not (value is Array) or value.is_empty():
		return "none"
	var values: Array[String] = []
	for item in value:
		var sector_id: Variant = null
		if item is Dictionary:
			sector_id = item.get("sector_id", item.get("to_sector", item.get("id", null)))
		else:
			sector_id = item
		# Adjacent-sector identifiers are server-owned JSON integers. Ignore
		# malformed rows rather than turning arbitrary payload text into a
		# seemingly actionable location.
		if _is_integral_number(sector_id) and int(sector_id) > 0:
			values.append(str(int(sector_id)))
	if values.is_empty():
		return "unknown"
	return ", ".join(values)

func _format_density_result(payload: Dictionary) -> String:
	if str(payload.get("status", "ok")) != "ok":
		return _format_visibility_refusal(payload, "Density scan")
	var data = payload.get("sectors", payload.get("data", payload))
	if data is Dictionary and data.has("sectors"):
		data = data["sectors"]
	var rows: Array[String] = ["SECTOR DENSITY", ""]
	if data is Dictionary and data.has("density"):
		var single_id = data["sector_id"] if data.has("sector_id") else data.get("id", null)
		var single_density = data.get("density", null)
		var confirmed_sector_id := _authoritative_positive_id(_snapshot.get("hud", {}).get("sector_id", null))
		if (_authoritative_density_id(single_id)
				and (confirmed_sector_id == 0 or int(single_id) == confirmed_sector_id)
				and _authoritative_density_value(single_density)):
			rows.append("Sector %s · Density: %s" % [_sector_identifier(single_id), _density_value(single_density)])
	elif data is Array:
		if not data.is_empty() and data[0] is Dictionary:
			# A density response is authoritative only when every returned row
			# carries a valid server-owned ID and integer density.  Do not render
			# a partial list while silently dropping malformed rows.
			var valid_rows := true
			for entry in data:
				if not (entry is Dictionary and entry.has("sector_id")
						and _authoritative_density_id(entry["sector_id"])
						and _authoritative_density_value(entry.get("density", null))):
					valid_rows = false
					break
			if valid_rows:
				for entry in data:
					rows.append("Sector %s · Density: %s" % [_sector_identifier(entry["sector_id"]), _density_value(entry["density"])])
		else:
			# The compact server form is [sector_id, density, ...]. Keep IDs and
			# values intact; never turn array indexes or malformed fields into
			# fake authoritative data. Validate every pair before displaying any.
			if data.size() % 2 == 0:
				var valid_pairs := true
				for index in range(0, data.size(), 2):
					if not (_authoritative_density_id(data[index])
							and _authoritative_density_value(data[index + 1])):
						valid_pairs = false
						break
				if valid_pairs:
					for index in range(0, data.size(), 2):
						rows.append("Sector %s · Density: %s" % [_sector_identifier(data[index]), _density_value(data[index + 1])])
	if rows.size() == 2:
		rows.append("No density values were returned by the server.")
	return "\n".join(rows)

func _authoritative_density_id(value: Variant) -> bool:
	return _is_integral_number(value) and int(value) > 0

func _is_integral_number(value: Variant) -> bool:
	return (typeof(value) == TYPE_INT and int(value) >= 0) or (typeof(value) == TYPE_FLOAT and is_finite(value) and value >= 0.0 and value == floor(value))

func _authoritative_positive_id(value: Variant) -> int:
	return int(value) if _is_integral_number(value) and int(value) > 0 else 0

func _format_visibility_refusal(payload: Dictionary, operation: String) -> String:
	var error: Variant = payload.get("error", {})
	var message := "The server did not provide %s information." % operation.to_lower()
	if error is Dictionary:
		var reported: Variant = error.get("message", "")
		if typeof(reported) == TYPE_STRING and not reported.strip_edges().is_empty():
			message = reported
	return "%s refused: %s" % [operation, message]

func _authoritative_density_value(value: Variant) -> bool:
	return _is_integral_number(value)

func _sector_identifier(value: Variant) -> String:
	if value is float and is_finite(value) and value == floor(value):
		return str(int(value))
	return str(value)

func _density_value(value: Variant) -> String:
	return str(int(value)) if _authoritative_density_value(value) else "unavailable"

func set_activity_history(entries: Array[String]) -> void:
	_activity_history = entries.duplicate()

func show_activity_history() -> void:
	var content := "No server events have arrived during this session." if _activity_history.is_empty() else "\n\n".join(_activity_history)
	var parent := get_parent()
	if parent and parent.has_method("show_information"):
		parent.show_information("Recent server events", content)
	else:
		_result_dialog.title = "Recent server events"
		_result_text.text = content
		DialogLayout.popup(_result_dialog, Vector2i(620, 420))

func show_help() -> void:
	var content := "SELECT\nClick an illustrated object or its contents row. Keyboard focus and selection stay synchronized. Use Tab to move between controls, arrow keys to move through focused lists, Enter to activate, and Escape to clear selection.\n\nCOMMANDS AND SHORTCUTS\nChoose an available action from the command drawer. Actions are discrete requests; the server confirms or refuses them, and results appear in the Information panel. Favourites and keyboard shortcuts remain available through settings/context actions. Shortcuts only run when a text field is not focused.\n\nSHIP AND PORT SERVICES\nRepair ship restores hull to 100 at a shipyard and charges the server-calculated cost. Hull exchanges and hardware purchases are separate shipyard/catalogue actions. Jettison permanently discards the selected cargo quantity.\n\nNEWS AND NOTICES\nNews feed shows published game headlines. Notices lists current server notices and their read state. Daily top-trader/fighter announcements appear here only if the server publishes them.\n\nMOVE\nSelect an adjacent warp destination, then confirm Move. The screen position of a marker is decorative; it does not represent distance or a flight path. Autonav asks the server to confirm every warp. Cancel stops after the current warp has been confirmed.\n\nPORT AND PLANET\nDock opens the port view after port information is confirmed. Land is a server command; the planet surface opens only after success. Trading requires a server quote and your confirmation.\n\nCONNECTION\nAfter a disconnect, the last confirmed scene is marked stale. Reconnect to refresh it before acting. Zero values are distinct from unavailable values."
	var parent := get_parent()
	if parent and parent.has_method("show_information"):
		parent.show_information("Trade Wars · Field Guide", content)
	else:
		_result_dialog.title = "Trade Wars · Field Guide"
		_result_text.text = content
		DialogLayout.popup(_result_dialog, Vector2i(620, 520))

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
	DialogLayout.popup(_shipyard_dialog, Vector2i(580, 450))

func show_hardware_catalogue(data: Dictionary) -> void:
	_shipyard_dialog.title = "Hardware catalogue · server-authoritative"
	for child in _shipyard_rows.get_children():
		child.queue_free()
	var items: Variant = data.get("items", data.get("hardware", []))
	if not (items is Array) or items.is_empty():
		var empty := Label.new()
		empty.text = "No equipment is available at this dock."
		_shipyard_rows.add_child(empty)
	else:
		for item in items:
			if not (item is Dictionary):
				continue
			var code := str(item.get("code", item.get("item_code", ""))).strip_edges()
			if code.is_empty():
				continue
			var max_purchase := int(item.get("max_purchase", 0))
			var eligible := max_purchase > 0 and bool(item.get("ship_has_capacity", true))
			var label := "%s [%s] · %s CR" % [str(item.get("name", code)), code, str(item.get("price", "—"))]
			if item.has("stock_quantity"):
				label += " · stock %s" % str(item.get("stock_quantity"))
			if not eligible:
				label += " · unavailable"
			var button := Button.new()
			button.text = label
			button.alignment = HORIZONTAL_ALIGNMENT_LEFT
			button.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
			button.size_flags_horizontal = Control.SIZE_EXPAND_FILL
			button.disabled = not eligible
			button.tooltip_text = "Select this server-listed item." if eligible else "The server reports this item is currently unavailable."
			button.pressed.connect(_choose_hardware_item.bind(code, max_purchase))
			_shipyard_rows.add_child(button)
	DialogLayout.popup(_shipyard_dialog, Vector2i(620, 450))

func _choose_hardware_item(code: String, max_purchase: int) -> void:
	if code.is_empty() or max_purchase <= 0:
		return
	_shipyard_dialog.hide()
	_choose_action({
		"label": "Buy %s" % code,
		"command": "hardware.buy",
		"fixed": {"code": code},
		"fields": [["quantity", FIELD_INT, "Quantity (maximum %d)" % max_purchase, "1"]],
		"mutating": true,
		"idempotency": true,
	})

func _choose_shipyard_hull(type_id: int, current_ship_name: String) -> void:
	if type_id <= 0:
		return
	_shipyard_dialog.hide()
	_shipyard_type_id = type_id
	_shipyard_name_input.text = current_ship_name
	DialogLayout.popup(_shipyard_name_dialog, Vector2i(520, 220))

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
			# Asset identity comes from the server and is the only value that can
			# authorize the recall request. Do not coerce strings, booleans, or
			# floats into an actionable integer ID.
			if typeof(asset_id) != TYPE_INT or int(asset_id) <= 0:
				continue
			var button := Button.new()
			button.text = "Asset %d · %s units · %s" % [int(asset_id), str(entry.get("count", "—")), str(entry.get("offense_mode", "mode unknown"))]
			button.alignment = HORIZONTAL_ALIGNMENT_LEFT
			button.disabled = sector_id <= 0
			button.pressed.connect(_recall_asset.bind(int(asset_id)))
			_shipyard_rows.add_child(button)
	DialogLayout.popup(_shipyard_dialog, Vector2i(580, 450))

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
	if _settings_mode:
		result.append("SETTINGS & NOTES")
	if not _pinned_actions.is_empty():
		result.append("PINNED")
	if not _selection.is_empty():
		var kind := str(_selection.get("kind", ""))
		if kind in ["planet", "port", "ship"]:
			result.append(kind.to_upper())
	for category in TAB_CATEGORIES:
		result.append(category)
	if _player_has_corporation():
		result.append("CORPORATION")
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

func select_category(category: String) -> void:
	# Presentation tabs select the existing command registry; they do not
	# create a second dispatcher or duplicate command definitions.
	# Keep old category names usable by saved UI state and existing shortcuts,
	# while presenting only the five player-facing tabs.
	var legacy_category_map := {
		"PORT": "TRADING",
		"FINANCES": "TRADING",
		"MARKET": "TRADING",
		"NEWS & RECORDS": "COMMUNICATIONS",
		"MAIL & SUBSCRIPTIONS": "COMMUNICATIONS",
		"TAVERN": "COMPUTER",
		"OPERATIONS": "SECTOR",
		"SETTINGS & NOTES": "COMPUTER",
		"SESSION": "COMPUTER"
	}
	if legacy_category_map.has(category):
		category = str(legacy_category_map[category])
	_settings_mode = category == "SETTINGS & NOTES"
	var categories := _categories()
	if category in categories:
		_active_category = category
		_rebuild_categories()

func open_settings_category() -> void:
	_settings_mode = true
	_active_category = "SETTINGS & NOTES"
	_rebuild_categories()

func invoke_local_action(label: String) -> bool:
	# Used by the gear menu for actions that do not belong in a gameplay tab.
	for group_name in ["SETTINGS & NOTES", "SESSION"]:
		for action in COMMANDS.get(group_name, []):
			if str(action.get("label", "")) == label:
				_choose_action(action)
				return true
	return false

func _render_actions() -> void:
	for child in _action_list.get_children():
		child.queue_free()
	var actions: Array = []
	if _active_category == "PLANET":
		actions = [{"label": "Planet information", "command": "planet.info", "context_id": "planet_id"}]
		var ship_state: Dictionary = _snapshot.get("ship", {})
		var landed_planet_id: Variant = ship_state.get("onplanet", null)
		# `onplanet` is a server-owned planet ID. Do not coerce booleans or
		# floating-point metadata into an actionable launch/surface workflow.
		var selected_planet_id: Variant = _authoritative_selection_id(_selection.get("data", {}), "planet_id")
		var selected_is_landed_planet := selected_planet_id != null and typeof(landed_planet_id) == TYPE_INT and int(landed_planet_id) > 0 and int(selected_planet_id) == int(landed_planet_id)
		if selected_is_landed_planet:
			actions.append({"label": "Launch from planet", "command": "planet.launch", "fixed": {}, "mutating": true})
			actions.append({"label": "Planet colonist status", "command": "planet.colonists.get", "context_id": "planet_id"})
			actions.append({"label": "Allocate planet colonists", "command": "planet.colonists.allocate", "context_id": "planet_id", "fields": [["ore", FIELD_INT, "Ore colonists", "0"], ["organics", FIELD_INT, "Organics colonists", "0"], ["equipment", FIELD_INT, "Equipment colonists", "0"]], "mutating": true})
			# The server exposes a harvest schema, but the handler is currently
			# unavailable and V3 has no pinned harvest semantics. Keep the
			# capability visible without fabricating a request or local formula.
			actions.append({"label": "Harvest planet production · unavailable", "command": "planet.harvest", "context_id": "planet_id", "unavailable_reason": "Planet harvest is unavailable until the authoritative server implementation and V3 policy are released."})
			actions.append({"label": "Build planet citadel", "command": "citadel.build", "context_id": "planet_id", "mutating": true})
			actions.append({"label": "Upgrade planet citadel", "command": "citadel.upgrade", "context_id": "planet_id", "mutating": true})
		elif typeof(landed_planet_id) == TYPE_INT and int(landed_planet_id) > 0:
			# A ship can only use surface commands for the exact server-confirmed
			# planet it occupies.  Keep the selected planet's read action visible,
			# but fail closed for launch/colonist/citadel operations instead of
			# sending a valid ID for a different planet.
			actions.append({"label": "Planet surface actions unavailable · different landed planet", "unavailable_reason": "Surface actions are available only for the server-confirmed landed planet; launch before selecting another planet."})
		else:
			actions.append({"label": "Land on planet", "command": "planet.land", "context_id": "planet_id", "mutating": true})
		var selected: Dictionary = _selection.get("data", {})
		# Selection metadata is server-owned JSON.  Do not coerce booleans or
		# floating-point values into an ID while deciding whether an action is
		# restricted; dispatch uses the same fail-closed identity validation.
		var attack_action := {"label": "Attack this planet", "command": "combat.attack_planet", "context_id": "planet_id", "mutating": true}
		actions.append(attack_action)
		var selected_metadata: Dictionary = _selection.get("data", {})
		if selected_metadata.has("planet_id") and _authoritative_selection_id(selected_metadata, "planet_id") == null:
			_add_unavailable("Selected planet has no valid planet ID; server actions are unavailable.")
	elif _active_category == "PORT":
		actions = [
			{"label": "Dock · open port screen", "command": "port.info", "context_id": "port_id", "fixed": {}},
			{"label": "Request commodity quote", "command": "trade.quote", "context_id": "port_id", "fields": [["commodity", FIELD_TEXT, "Commodity code or name", ""], ["quantity", FIELD_INT, "Quantity", "1"]]},
			{"label": "Shipyard · choose eligible hull", "command": "shipyard.list", "requires_shipyard_dock": true},
		]
	elif _active_category == "SHIP":
		var ship_data: Dictionary = _selection.get("data", {})
		# Keep the command rail fail-closed until the selected object carries a
		# server-owned JSON integer ID.  _choose_action already rejects malformed
		# context IDs; rendering must not make those actions look executable.
		var selected_ship_id: Variant = _authoritative_selection_id(ship_data)
		# The HUD identity is server-owned.  Do not coerce strings/floats into an
		# active-ship ID while deciding whether a selected vessel is actionable;
		# an untyped identity must not silently change the ownership workflow.
		var active_ship_id := _authoritative_positive_id(_snapshot.get("hud", {}).get("ship_id", 0))
		if selected_ship_id == null:
			_add_unavailable("Selected ship actions are unavailable until the server provides a valid ship ID.")
			return
		elif int(selected_ship_id) == active_ship_id:
			# The active ship is still an authoritative owned-ship selection.  Keep
			# target-only mutations hidden, but allow the ordinary-player read path
			# so selecting the ship currently being flown does not make inspection
			# unavailable.  The server remains authoritative for the returned facts.
			actions = [
				{"label": "Inspect ship details", "command": "ship.inspect", "context_id": "ship_id"},
			]
		else:
			var attack_action := {"label": "Attack this ship", "command": "combat.attack", "context_id": "target_ship_id", "mutating": true}
			var current_sector_id := int(_snapshot.get("hud", {}).get("sector_id", 0))
			actions = [
				{"label": "Inspect ship details", "command": "ship.inspect", "context_id": "ship_id"},
				{"label": "Transfer cargo to this ship", "command": "ship.transfer_cargo", "context_id": "target_ship_id", "fields": [["commodity_id", FIELD_INT, "Commodity ID", ""], ["quantity", FIELD_INT, "Quantity", "1"]], "mutating": true},
				{"label": "Sell this ship · unavailable", "command": "ship.sell", "context_id": "ship_id", "unavailable_reason": "The authoritative server sale handler is not implemented yet; no request was sent."},
				{"label": "Transfer this ship", "command": "ship.transfer", "context_id": "ship_id", "fields": [["target_player_id", FIELD_INT, "Recipient player ID", ""]], "mutating": true, "tooltip": "The server validates ownership, recipient eligibility and the atomic ownership transfer."},
				attack_action,
				{"label": "Claim this ship · server verifies eligibility", "command": "ship.claim", "context_id": "ship_id", "mutating": true},
				{"label": "Tow this ship · requires owned, unpiloted vessel", "command": "ship.tow", "context_id": "target_ship_id", "mutating": true},
				{"label": "Intercept this ship", "command": "move.intercept", "context_id": "target_ship_id", "mutating": true, "requires_fresh_sector": true, "tooltip": "The server validates the target, location, turns, and interception outcome."},
			]
			if current_sector_id >= 1 and current_sector_id <= 10:
				attack_action["unavailable_reason"] = "Attacks are disabled in protected FedSpace sectors 1–10."
	elif _active_category == "PINNED":
		actions = _pinned_actions
	else:
		actions = _actions_for_category(_active_category)
	var query := _search_input.text.strip_edges().to_lower() if is_instance_valid(_search_input) else ""
	if not query.is_empty():
		actions = actions.filter(func(action: Dictionary) -> bool: return str(action.get("label", "")).to_lower().contains(query))
	if actions.is_empty():
		_add_unavailable("No commands are available in this category.")
		return
	for action in actions:
		var button := Button.new()
		button.text = str(action.get("label", action.get("command", "Command")))
		if bool(action.get("local_reduced_motion", false)):
			button.text = "Reduced motion · %s" % ("ON" if _reduced_motion_enabled else "OFF")
			button.tooltip_text = "Skip animated movement and screen transitions when enabled."
		button.alignment = HORIZONTAL_ALIGNMENT_LEFT
		button.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
		button.custom_minimum_size.y = 48
		_style_button(button, false)
		var connected := bool(_snapshot.get("authenticated", false)) and not bool(_snapshot.get("disconnected", true))
		var raw_freshness: Variant = _snapshot.get("freshness", {})
		var freshness: Dictionary = raw_freshness if raw_freshness is Dictionary else {}
		var context_stale: bool = (action.has("context_id") or bool(action.get("requires_fresh_sector", false))) and str(freshness.get("sector", "")) != "fresh"
		var unavailable_reason := str(action.get("unavailable_reason", ""))
		var missing_shipyard_context := bool(action.get("requires_shipyard_dock", false)) and not _is_docked_at_shipyard()
		var missing_supported_dock := bool(action.get("requires_supported_dock", false)) and not _is_docked_at_supported_hardware()
		var missing_corporation := bool(action.get("requires_corporation", false)) and int(_snapshot.get("player", {}).get("corp_id", 0)) <= 0
		button.disabled = not unavailable_reason.is_empty() or _busy or not connected or context_stale or missing_shipyard_context or missing_supported_dock or missing_corporation
		var action_tooltip := str(action.get("tooltip", "")) if action.has("tooltip") else ""
		if unavailable_reason.is_empty():
			if missing_corporation:
				action_tooltip = "Join or create a corporation first."
			elif missing_supported_dock:
				action_tooltip = "Dock at a supported hardware port in this sector first."
			elif missing_shipyard_context:
				action_tooltip = "Dock at a supported shipyard port in this sector first."
			elif not connected:
				action_tooltip = "Unavailable while disconnected."
			elif context_stale:
				action_tooltip = "Refresh the current sector before using this command." if bool(action.get("requires_fresh_sector", false)) else "Refresh the sector before using this selected object."
			elif _busy:
				action_tooltip = "A command is already waiting for the server."
		button.tooltip_text = unavailable_reason if not unavailable_reason.is_empty() else action_tooltip
		button.pressed.connect(_choose_action.bind(action))
		button.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		# Keep favourites and shortcut bindings in the model and dispatch paths,
		# but do not spend command-rail width on per-row controls.  Their future
		# settings/context-menu surface can use _toggle_pin and
		# _begin_shortcut_binding without changing command dispatch.
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

func _is_docked_at_supported_hardware() -> bool:
	var ship: Dictionary = _snapshot.get("ship", {})
	var docked_port_id := int(ship.get("ported", 0))
	if docked_port_id <= 0:
		return false
	var sector: Dictionary = _snapshot.get("sector", {})
	for port in sector.get("ports", []):
		if port is Dictionary and int(port.get("id", port.get("port_id", 0))) == docked_port_id:
			return int(port.get("type", -1)) in [9, 10]
	return false

func set_busy(value: bool) -> void:
	_busy = value
	_render_actions()

func set_reduced_motion_enabled(enabled: bool) -> void:
	_reduced_motion_enabled = enabled
	if is_instance_valid(_action_list):
		_render_actions()

func _load_local_command_preferences() -> void:
	var config := ConfigFile.new()
	if config.load(LOCAL_PREFERENCES_PATH) != OK:
		return
	var saved_pins = config.get_value("commands", "pinned", [])
	if saved_pins is Array:
		for item in saved_pins:
			if item is Dictionary:
				_pinned_actions.append(item.duplicate(true))
	var saved_bindings = config.get_value("commands", "key_bindings", {})
	if saved_bindings is Dictionary:
		_key_bindings = saved_bindings.duplicate(true)

func _save_local_command_preferences() -> void:
	var config := ConfigFile.new()
	config.load(LOCAL_PREFERENCES_PATH)
	config.set_value("commands", "pinned", _pinned_actions)
	config.set_value("commands", "key_bindings", _key_bindings)
	config.save(LOCAL_PREFERENCES_PATH)

func _action_storage_key(action: Dictionary) -> String:
	var command := str(action.get("command", ""))
	return command if not command.is_empty() else "local:" + str(action.get("label", "Command"))

func _is_pinned(action_key: String) -> bool:
	for item in _pinned_actions:
		if _action_storage_key(item) == action_key:
			return true
	return false

func _toggle_pin(action: Dictionary) -> void:
	var key := _action_storage_key(action)
	for index in _pinned_actions.size():
		if _action_storage_key(_pinned_actions[index]) == key:
			_pinned_actions.remove_at(index)
			_key_bindings.erase(key)
			_save_local_command_preferences()
			_rebuild_categories()
			return
	_pinned_actions.append(action.duplicate(true))
	_save_local_command_preferences()
	_rebuild_categories()

func _begin_shortcut_binding(action_key: String) -> void:
	_awaiting_binding_command = action_key
	shortcut_binding_prompt.emit("Press a key to bind this pinned command · Esc clears the binding.")
	_render_actions()

func capture_shortcut(event: InputEventKey) -> bool:
	if _awaiting_binding_command.is_empty() or not event.pressed or event.echo:
		return false
	if event.ctrl_pressed or event.alt_pressed or event.meta_pressed or event.shift_pressed:
		shortcut_binding_prompt.emit("Use a single key for this shortcut; modifier combinations are not supported.")
		return true
	var key := _awaiting_binding_command
	_awaiting_binding_command = ""
	if event.keycode == KEY_ESCAPE:
		_key_bindings.erase(key)
	else:
		if event.keycode == KEY_CTRL or event.keycode == KEY_SHIFT or event.keycode == KEY_ALT or event.keycode == KEY_META:
			_awaiting_binding_command = key
			shortcut_binding_prompt.emit("Choose a non-modifier key, or press Escape to clear the binding.")
			return true
		for other_key in _key_bindings.keys().duplicate():
			if int(_key_bindings[other_key]) == int(event.keycode):
				_key_bindings.erase(other_key)
		_key_bindings[key] = int(event.keycode)
	_save_local_command_preferences()
	_render_actions()
	shortcut_binding_prompt.emit("Shortcut saved." if event.keycode != KEY_ESCAPE else "Shortcut cleared.")
	return true

func dispatch_shortcut(keycode: int) -> bool:
	if not _awaiting_binding_command.is_empty():
		return false
	for action_key in _key_bindings:
		if int(_key_bindings[action_key]) != keycode:
			continue
		for action in _pinned_actions:
			if _action_storage_key(action) == str(action_key):
				if action.has("command") and (_busy or not bool(_snapshot.get("authenticated", false)) or bool(_snapshot.get("disconnected", true))):
					_add_unavailable("This shortcut is unavailable while disconnected or while another command is pending.")
					return true
				_choose_action(action)
				return true
	return false

func _add_unavailable(message: String) -> void:
	_result_text.text = message
	var label := Label.new()
	label.text = message
	label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	label.add_theme_color_override("font_color", Color(0.82, 0.7, 0.58))
	_action_list.add_child(label)

func _authoritative_selection_id(source: Dictionary, preferred_key: String = "") -> Variant:
	var keys: Array[String] = []
	if not preferred_key.is_empty():
		keys.append(preferred_key)
	for key in ["planet_id", "port_id", "ship_id", "target_ship_id", "id"]:
		if not keys.has(key):
			keys.append(key)
	var selected_id := 0
	var strict_ship_identity := source.has("ship_id") or source.has("target_ship_id")
	for key in keys:
		if not source.has(key) or source[key] == null:
			continue
		# Selection metadata is untrusted until every supplied identity alias is
		# a positive JSON integer.  A valid ship_id must not authorize an action
		# when the same object also carries a malformed or contradictory id.
		if typeof(source[key]) != TYPE_INT:
			if not strict_ship_identity and key != preferred_key:
				continue
			return null
		var candidate := int(source[key])
		if candidate <= 0:
			if not strict_ship_identity and key != preferred_key:
				continue
			return null
		if selected_id > 0 and candidate != selected_id:
			if not strict_ship_identity and key != preferred_key:
				continue
			return null
		selected_id = candidate
	return selected_id if selected_id > 0 else null

func _choose_action(action: Dictionary) -> void:
	if bool(action.get("local_reduced_motion", false)):
		reduced_motion_toggle_requested.emit()
		return
	if bool(action.get("local_hud_settings", false)):
		hud_settings_requested.emit()
		return
	if action.has("unavailable_reason"):
		_add_unavailable(str(action["unavailable_reason"]))
		return
	if bool(action.get("local_logout", false)):
		if bool(_snapshot.get("authenticated", false)) and not bool(_snapshot.get("disconnected", true)):
			DialogLayout.popup(_logout_dialog, Vector2i(480, 200))
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
	var raw_freshness: Variant = _snapshot.get("freshness", {})
	var sector_is_fresh := raw_freshness is Dictionary and str(raw_freshness.get("sector", "")) == "fresh"
	if bool(action.get("requires_fresh_sector", false)) and not sector_is_fresh:
		_add_unavailable("Refresh the current sector before planning from its location.")
		return
	var data: Dictionary = action.get("fixed", {}).duplicate(true)
	if bool(action.get("context_player_corp_id", false)):
		var player: Dictionary = _snapshot.get("player", {})
		var corp_id := int(player.get("corp_id", 0))
		if corp_id <= 0:
			_add_unavailable("Your confirmed player state does not include a corporation ID.")
			return
		data["corp_id"] = corp_id
	if bool(action.get("context_sector", false)):
		var current_sector = _snapshot.get("hud", {}).get("sector_id", null)
		if typeof(current_sector) != TYPE_INT or int(current_sector) <= 0:
			_add_unavailable("The current sector has no valid server identifier in the confirmed state.")
			return
		if str(action.get("command", "")) == "sector.set_beacon" and int(current_sector) >= 1 and int(current_sector) <= 10:
			_add_unavailable("The server does not permit marker beacons in FedSpace sectors 1–10.")
			return
		data["sector_id"] = int(current_sector)
	if action.has("context_id"):
		var source: Dictionary = _selection.get("data", {})
		var value = _authoritative_selection_id(source, str(action["context_id"]))
		if value == null:
			_add_unavailable("This object has no valid server identifier, so the command cannot be sent safely.")
			return
		var authoritative_id := int(value)
		if authoritative_id <= 0:
			_add_unavailable("This object has no valid server identifier, so the command cannot be sent safely.")
			return
		data[str(action["context_id"])] = authoritative_id
	var fields: Array = action.get("fields", [])
	if fields.is_empty():
		# Landing is an immediate navigation action. The server still validates
		# whether the ship can land and reports any refusal.
		if str(action.get("command", "")) == "planet.land":
			command_requested.emit(str(action["command"]), data, str(action["label"]), true)
			return
		if action.get("mutating", false):
			_form_action = action
			_form_fields.clear()
			_form_dialog.dialog_text = "Send %s? The server remains authoritative." % str(action.get("label", "this command"))
			_form_dialog.get_label().visible = true
			_form_dialog.ok_button_text = "CONFIRM"
			DialogLayout.popup(_form_dialog, Vector2i(480, 200))
			set_meta("pending_data", data)
		else:
			command_requested.emit(str(action["command"]), data, str(action["label"]), false)
		return
	if bool(action.get("idempotency", false)):
		data["idempotency_key"] = str(Time.get_ticks_usec())
	_form_action = action
	_form_fields.clear()
	for child in _inline_form_content.get_children():
		child.queue_free()
	_inline_form_content.add_theme_constant_override("separation", 8)
	var inline_title := Label.new()
	inline_title.text = str(action.get("label", "Command")).to_upper()
	inline_title.add_theme_font_size_override("font_size", 14)
	inline_title.add_theme_color_override("font_color", Color(0.48, 0.86, 0.84))
	_inline_form_content.add_child(inline_title)
	var form_notice := str(action.get("form_notice", ""))
	if not form_notice.is_empty():
		var notice_label := Label.new()
		notice_label.text = form_notice
		notice_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
		notice_label.add_theme_color_override("font_color", Color(1.0, 0.72, 0.52))
		_inline_form_content.add_child(notice_label)
	for field in fields:
		var row := VBoxContainer.new()
		var label := Label.new()
		label.text = str(field[2])
		DialogLayout.prepare_label(label)
		row.add_child(label)
		if str(field[1]) == "boolean":
			var check := CheckBox.new()
			check.button_pressed = str(field[3]) == "true"
			row.add_child(check)
			_form_fields[str(field[0])] = check
		else:
			var input := LineEdit.new()
			input.size_flags_horizontal = Control.SIZE_EXPAND_FILL
			input.text = str(field[3])
			input.placeholder_text = str(field[2])
			input.secret = str(field[1]) == "password"
			if field.size() > 4:
				input.max_length = int(field[4])
			row.add_child(input)
			_form_fields[str(field[0])] = input
		_inline_form_content.add_child(row)
	var inline_actions := HBoxContainer.new()
	_inline_submit = Button.new()
	_inline_submit.text = "SEND"
	_inline_submit.pressed.connect(_submit_form)
	_style_button(_inline_submit, true)
	inline_actions.add_child(_inline_submit)
	var inline_cancel := Button.new()
	inline_cancel.text = "CANCEL"
	inline_cancel.pressed.connect(_clear_inline_form)
	inline_actions.add_child(inline_cancel)
	_inline_form_content.add_child(inline_actions)
	_inline_form_panel.visible = true
	set_meta("pending_data", data)
	if not _form_fields.is_empty():
		var first_field: Control = _form_fields.values()[0]
		first_field.grab_focus()

func _clear_inline_form() -> void:
	_form_action.clear()
	_form_fields.clear()
	set_meta("pending_data", {})
	if is_instance_valid(_inline_form_panel):
		_inline_form_panel.visible = false

func _confirm_logout() -> void:
	command_requested.emit("auth.logout", {}, "Log out", true)

func _submit_form() -> void:
	var data: Dictionary = get_meta("pending_data", {}).duplicate(true)
	var password_values: Dictionary = {}
	for key in _form_fields:
		var control: Control = _form_fields[key]
		if control is CheckBox:
			data[key] = (control as CheckBox).button_pressed
		elif control is LineEdit:
			var text: String = (control as LineEdit).text
			if str(_field_type(key)) != "password":
				text = text.strip_edges()
			if text.is_empty():
				_add_unavailable("Every visible field must be completed before sending.")
				return
			if str(_field_type(key)) in [FIELD_INT, FIELD_NONNEGATIVE_INT]:
				var minimum := 0 if str(_field_type(key)) == FIELD_NONNEGATIVE_INT else 1
				if not text.is_valid_int() or int(text) < minimum:
					_add_unavailable("Enter a whole number of at least %d for %s." % [minimum, key])
					return
				data[key] = int(text)
			else:
				data[key] = text
				if str(_field_type(key)) == "password":
					password_values[key] = text
	if str(_form_action.get("command", "")) == "auth.change_password":
		if password_values.get("new_password", "") != password_values.get("confirm_password", ""):
			_add_unavailable("Password change cancelled: the new passwords do not match.")
			return
		data.erase("confirm_password")
		for control in _form_fields.values():
			if control is LineEdit:
				(control as LineEdit).clear()
		set_meta("pending_data", {})
	if str(_form_action.get("command", "")) == "player.computer.recommend_routes":
		data["require_two_way"] = 1 if bool(data.get("require_two_way", false)) else 0
	if _form_action.has("preference_key"):
		data = {"items": [{"key": str(_form_action["preference_key"]), "type": str(_form_action.get("preference_type", "string")), "value": data.get("value")}]} 
	if str(_form_action.get("command", "")) == "ship.transfer":
		var transfer_ship_id: Variant = data.get("ship_id", null)
		if typeof(transfer_ship_id) != TYPE_INT or not _owned_ship_ids.has(int(transfer_ship_id)):
			_add_unavailable("Ship transfer cancelled: refresh Owned ship roster and choose a server-listed ship ID.")
			return
	command_requested.emit(str(_form_action["command"]), data, str(_form_action.get("label", "Command")), bool(_form_action.get("mutating", false)))
	_clear_inline_form()

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
