extends SceneTree

const Main = preload("res://Main.gd")

class FakeAuth extends RefCounted:
	var session_token := "test-session"
	func is_authenticated() -> bool: return true
	func invalidate(_reason: String) -> void: pass

class FakeTransport extends RefCounted:
	var calls: Array[Dictionary] = []
	func request(command: String, data: Dictionary, token: String) -> String:
		calls.append({"command": command, "data": data.duplicate(true), "token": token})
		return "request-%d" % calls.size()

class FakeView extends Node:
	var quotes: Array[Dictionary] = []
	var messages: Array[String] = []
	var port_workflow = FakePort.new()
	var command_menu = FakeMenu.new()
	var ports: Array[Dictionary] = []
	var quote_dismissed := false
	func set_command_busy(_busy: bool) -> void: pass
	func dismiss_trade_quote() -> void: quote_dismissed = true
	func clear_planned_route() -> void: pass
	func show_notification(message: String) -> void: messages.append(message)
	func show_command_result(_label: String, _data: Dictionary) -> void: pass
	func enter_port_workflow(data: Dictionary) -> void: ports.append(data)
	func confirm_trade_quote(direction: String, commodity: String, quantity: int, account: int, quote: Dictionary) -> void:
		quotes.append({"direction": direction, "commodity": commodity, "quantity": quantity, "account": account, "quote": quote})

class FakePort extends RefCounted:
	var closed := false
	func close_for_location_change() -> void: closed = true

class FakeMenu extends RefCounted:
	var tavern_access := true
	func set_tavern_access(enabled: bool) -> void: tavern_access = enabled

class FakeRefresh extends RefCounted:
	var calls := 0
	func refresh(_force := false, _domains: Array = []) -> bool:
		calls += 1
		return true
	func handle_disconnect() -> void: pass

class FakeLogin extends Control:
	func set_overlay_mode(_enabled: bool) -> void: pass
	func set_status(_message: String) -> void: pass

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	var main = Main.new()
	var view = FakeView.new()
	var transport = FakeTransport.new()
	main.gameplay_view = view
	main.auth_session = FakeAuth.new()
	main.transport = transport
	main.refresh_coordinator = FakeRefresh.new()
	main.login_view = FakeLogin.new()
	main._on_trade_requested("buy", 7, 17, "ORE", 3, 1)
	assert(transport.calls.size() == 1 and transport.calls[0].command == "trade.quote")
	assert(transport.calls[0].data == {"port_id": 7, "commodity": "ORE", "quantity": 3})
	main._pending_game_commands.clear()
	main._finish_trade_quote({"status": "ok", "data": {"port_id": 7, "commodity": "ORE", "quantity": 3, "buy_price": 40, "total_buy_price": 120}})
	assert(view.quotes.size() == 1 and view.quotes[0].account == 1)
	main._on_trade_confirmation(false)
	assert(main._pending_trade.is_empty() and transport.calls.size() == 1)
	main._on_trade_requested("sell", 7, 17, "ORG", 2, 1)
	main._pending_game_commands.clear()
	main._finish_trade_quote({"status": "ok", "data": {"port_id": 7, "commodity": "ORG", "quantity": 2, "sell_price": 40, "total_sell_price": 80}})
	main._on_trade_confirmation(true)
	assert(transport.calls.size() == 3 and transport.calls[2].command == "trade.sell")
	var payload: Dictionary = transport.calls[2].data
	assert(payload.account == 1 and payload.port_id == 7 and payload.sector_id == 17)
	assert(payload.items == [{"commodity": "ORG", "quantity": 2}] and not str(payload.idempotency_key).is_empty())
	var operation: Dictionary = main._pending_game_commands["request-3"]
	main._pending_game_commands.clear()
	await main._finish_command_reply({"status": "ok", "data": {}}, operation)
	assert(main.refresh_coordinator.calls == 1 and main._command_refresh_pending)
	main._on_refresh_finished({})
	assert(transport.calls.back().command == "port.info")
	await main._on_transport_reply("request-4", {"status": "ok", "data": {"port": {"port_id": 7}}}, "port.info")
	assert(view.ports.size() == 1 and main._pending_trade.is_empty())
	main._on_trade_requested("buy", 7, 17, "ORE", 1, 0)
	main._finish_trade_quote({"status": "refused", "error": {"message": "Unavailable"}})
	assert(main._pending_trade.is_empty() and view.messages.back().contains("Unavailable"))
	main._pending_game_commands.clear()
	main._on_trade_requested("buy", 7, 17, "ORE", 1, 0)
	main._finish_trade_quote({"status": "ok", "data": {"port_id": 8, "commodity": "ORE", "quantity": 1, "buy_price": 1, "total_buy_price": 1}})
	assert(main._pending_trade.is_empty() and view.quotes.size() == 2 and view.messages.back().contains("did not match"))
	assert(not main._valid_trade_quote({"port_id": {}, "quantity": 1, "commodity": "ORE", "buy_price": 1, "total_buy_price": 1}, {"port_id": 7, "quantity": 1, "commodity": "ORE"}, "buy_price", "total_buy_price"))
	assert(not main._valid_trade_quote({"port_id": 7, "quantity": 1, "commodity": "ORE", "buy_price": INF, "total_buy_price": 1}, {"port_id": 7, "quantity": 1, "commodity": "ORE"}, "buy_price", "total_buy_price"))
	main._pending_game_commands.clear()
	main._request_port_refresh()
	main._on_request_timed_out("request-7", "port.info")
	assert(view.port_workflow.closed and view.messages.back().contains("reopen"))
	main._on_trade_requested("buy", 7, 17, "ORE", 1, 0)
	main._pending_game_commands.clear()
	main._finish_trade_quote({"status": "ok", "data": {"port_id": 7, "commodity": "ORE", "quantity": 1, "buy_price": 3, "total_buy_price": 3}})
	main._on_trade_confirmation(true)
	operation = main._pending_game_commands["request-9"]
	main._pending_game_commands.clear()
	await main._finish_command_reply({"status": "refused", "error": {"message": "Stock exhausted"}}, operation)
	assert(main._pending_trade.is_empty() and view.messages.back().contains("Stock exhausted") and main.refresh_coordinator.calls == 1)
	main._on_trade_requested("buy", 7, 17, "ORE", 1, 0)
	main._pending_game_commands.clear()
	main._finish_trade_quote({"status": "ok", "data": {"port_id": 7, "commodity": "ORE", "quantity": 1, "buy_price": 3, "total_buy_price": 3}})
	view.port_workflow.closed = false
	main._on_transport_disconnected("test")
	assert(main._pending_trade.is_empty() and view.quote_dismissed and view.port_workflow.closed and not view.command_menu.tavern_access)
	main._on_trade_confirmation(true)
	assert(transport.calls.size() == 10)
	main.login_view.free()
	view.free()
	main.free()
	print("Godot trade lifecycle tests: 17 passed")
	quit(0)
