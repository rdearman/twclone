extends SceneTree

const Main = preload("res://Main.gd")

class FakeAuth extends RefCounted:
	var session_token := "test-session"
	func is_authenticated() -> bool: return true
	func invalidate(_reason: String) -> void: pass

class FakeTransport extends RefCounted:
	var calls: Array[Dictionary] = []
	var return_empty := false
	func request(command: String, data: Dictionary, token: String) -> String:
		calls.append({"command": command, "data": data.duplicate(true), "token": token})
		if return_empty:
			return ""
		return "request-%d" % calls.size()

class FakeView extends Node:
	var _snapshot: Dictionary = {}
	var quotes: Array[Dictionary] = []
	var messages: Array[String] = []
	var busy := false
	var port_workflow = FakePort.new()
	var command_menu = FakeMenu.new()
	var ports: Array[Dictionary] = []
	var quote_dismissed := false
	func set_command_busy(value: bool) -> void: busy = value
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
	var domains: Array[Array] = []
	func refresh(_force := false, requested_domains: Array = []) -> bool:
		calls += 1
		domains.append(requested_domains.duplicate(true))
		return true
	func handle_disconnect() -> void: pass
	func accepts_request(_request_id: String) -> bool: return false

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
	view._snapshot = {"hud": {"sector_id": 17}, "freshness": {"sector": "fresh"}, "sector": {"ports": [{"id": 7}]}}
	assert(main._response_port_id({"port": {"id": 7}}) == 7)
	assert(main._response_port_id({"port": {"port_id": 7}}) == 7)
	assert(main._response_port_id({"port": {"id": true, "port_id": 7}}) == 0, "malformed canonical port ID fell through to a compatibility alias")
	assert(main._response_port_id({"port": {"id": 7.0, "port_id": 7}}) == 0, "floating-point canonical port ID fell through to a compatibility alias")
	var port_workflow = preload("res://PortWorkflow.gd").new()
	port_workflow._port = {"id": true, "port_id": 7}
	assert(port_workflow._authoritative_port_id() == 0, "port workflow accepted a compatibility alias after malformed canonical ID")
	port_workflow._port = {"port_id": 7}
	assert(port_workflow._authoritative_port_id() == 7, "port workflow rejected the documented compatibility alias when canonical ID was absent")
	port_workflow.free()
	main._on_trade_requested("buy", 7, 17, "ORE", 3, 1)
	assert(transport.calls.size() == 1 and transport.calls[0].command == "trade.quote")
	assert(transport.calls[0].data == {"port_id": 7, "commodity": "ORE", "quantity": 3})
	main._pending_trade.clear()
	main._pending_game_commands.clear()
	main._on_trade_requested("buy", 7, 17, " ORE ", 3, 1)
	assert(transport.calls.size() == 2 and transport.calls[1].data == {"port_id": 7, "commodity": "ORE", "quantity": 3}, "validated commodity code was not used for the authoritative quote")
	main._pending_trade.clear()
	main._pending_game_commands.clear()
	main._on_trade_requested("buy", 7, 17, "ORE", 0, 1)
	assert(transport.calls.size() == 1 and main.refresh_coordinator.calls == 0, "zero-quantity trade crossed the protocol boundary")
	main._on_trade_requested("buy", 7, 17, "ORE", 1, 2)
	assert(transport.calls.size() == 1 and main.refresh_coordinator.calls == 0, "invalid trade account crossed the protocol boundary")
	main._on_trade_requested("buy", 7, 17, "   ", 1, 0)
	assert(transport.calls.size() == 1 and main.refresh_coordinator.calls == 0, "blank commodity crossed the protocol boundary")
	main._pending_trade.clear()
	main._pending_game_commands.clear()
	main._on_trade_requested("buy", 8, 17, "ORE", 1, 0)
	assert(transport.calls.size() == 1 and main.refresh_coordinator.calls == 1)
	assert(view.messages.back().contains("not the confirmed current port"))
	main.refresh_coordinator.calls = 0
	main._pending_game_commands.clear()
	view._snapshot.freshness.sector = "stale"
	main._on_trade_requested("buy", 7, 17, "ORE", 1, 0)
	assert(transport.calls.size() == 1 and main.refresh_coordinator.calls == 1)
	assert(view.messages.back().contains("current sector is stale"))
	view._snapshot.freshness.sector = "fresh"
	main._command_refresh_pending = false
	main._pending_game_commands.clear()
	main._pending_trade = {"direction": "buy", "port_id": 7, "sector_id": 17, "commodity": "ORE", "quantity": 3, "account": 1}
	main._finish_trade_quote({"status": "ok", "type": "trade.quote", "data": {"port_id": 7, "commodity": "ORE", "quantity": 3, "buy_price": 40, "total_buy_price": 120}})
	assert(view.quotes.size() == 1 and view.quotes[0].account == 1)
	assert(main._trade_quote_response_type_matches({"status": "ok", "type": "trade.quote"}))
	assert(not main._trade_quote_response_type_matches({"status": "ok"}))
	assert(not main._trade_quote_response_type_matches({"status": "ok", "type": "trade.sell_receipt_v1"}))
	assert(not main._trade_quote_response_type_matches({"status": "ok", "type": true}))
	assert(main._port_response_type_matches({"status": "ok", "type": "trade.port_info"}))
	assert(not main._port_response_type_matches({"status": "ok"}), "port refresh accepted a response without the authoritative schema type")
	assert(not main._port_response_type_matches({"status": "ok", "type": ""}), "port refresh accepted an empty response schema type")
	assert(not main._port_response_type_matches({"status": "ok", "type": "port.info"}), "port refresh accepted a command name as a response schema")
	main._on_trade_confirmation(false)
	assert(main._pending_trade.is_empty() and transport.calls.size() == 1)
	main._on_trade_requested("sell", 7, 17, "org", 2, 1)
	main._pending_game_commands.clear()
	main._finish_trade_quote({"status": "ok", "type": "trade.quote", "data": {"port_id": 7, "commodity": "ORG", "quantity": 2, "sell_price": 40, "total_sell_price": 80}})
	assert(view.quotes[1].commodity == "ORG", "trade confirmation did not use the quote's canonical commodity code")
	main._on_trade_confirmation(true)
	assert(transport.calls.size() == 3 and transport.calls[2].command == "trade.sell")
	var payload: Dictionary = transport.calls[2].data
	assert(payload.account == 1 and payload.port_id == 7 and payload.sector_id == 17)
	assert(payload.items == [{"commodity": "ORG", "quantity": 2}] and not str(payload.idempotency_key).is_empty())
	var operation: Dictionary = main._pending_game_commands["request-3"]
	main._pending_game_commands.clear()
	await main._finish_command_reply({"status": "ok", "type": "trade.sell_receipt_v1", "data": {"port_id": 7, "sector_id": 17, "player_id": 1, "lines": [{"commodity": "ORG", "quantity": 2}]}}, operation)
	assert(main.refresh_coordinator.calls == 1 and main._command_refresh_pending)
	main._on_refresh_finished({})
	assert(transport.calls.back().command == "port.info")
	assert(transport.calls.back().data == {"port_id": 7}, "post-trade port refresh did not use the confirmed port ID")
	await main._on_transport_reply("request-4", {"status": "ok", "data": {"port": {"port_id": 7}}}, "port.info")
	assert(view.ports.size() == 1 and main._pending_trade.is_empty())
	main._pending_game_commands["request-port"] = {"kind": "command", "command": "port.info", "label": "Open port", "data": {"port_id": 7}}
	await main._finish_command_reply({"status": "ok", "type": "trade.port_info", "data": {"port": {"id": 7}}}, main._pending_game_commands["request-port"])
	assert(view.ports.size() == 2, "selected port.info response was not accepted using its requested authoritative port ID")
	main._pending_game_commands.erase("request-port")
	assert(not main._accept_authoritative_port({"port": {"id": 8}}, "Open port", 7), "port response for a different selected port was accepted")
	assert(not main._accept_authoritative_port({"port": {"id": 7, "sector": 18}}, "Open port", 7, 17), "legacy sector field allowed a remote port response")
	assert(not main._accept_authoritative_port({"port": {"id": 7, "sector": true}}, "Open port", 7, 17), "malformed legacy sector field allowed a port response")
	assert(main._accept_authoritative_port({"port": {"id": 7, "sector": 17}}, "Open port", 7, 17), "matching legacy sector field was rejected")
	main._on_trade_requested("buy", 7, 17, "ORE", 1, 0)
	main._finish_trade_quote({"status": "refused", "error": {"message": "Unavailable"}})
	assert(main._pending_trade.is_empty() and not view.busy and view.messages.back().contains("Unavailable") and view.messages.back().contains("refreshing port stock"), "quote refusal left the trade workflow locked")
	assert(transport.calls.back().command == "port.info" and main._pending_game_commands.has("request-6"))
	main._pending_game_commands.clear()
	main._on_trade_requested("buy", 7, 17, "ORE", 1, 0)
	main._finish_trade_quote({"status": "ok", "type": "trade.quote", "data": {"port_id": 8, "commodity": "ORE", "quantity": 1, "buy_price": 1, "total_buy_price": 1}})
	assert(main._pending_trade.is_empty() and view.quotes.size() == 2 and view.messages.back().contains("did not match"))
	assert(transport.calls.back().command == "port.info" and main._pending_game_commands.has("request-8"))
	assert(not main._valid_trade_quote({"port_id": {}, "quantity": 1, "commodity": "ORE", "buy_price": 1, "total_buy_price": 1}, {"port_id": 7, "quantity": 1, "commodity": "ORE"}, "buy_price", "total_buy_price"))
	assert(not main._valid_trade_quote({"port_id": 7, "quantity": 1, "commodity": "ORE", "buy_price": INF, "total_buy_price": 1}, {"port_id": 7, "quantity": 1, "commodity": "ORE"}, "buy_price", "total_buy_price"))
	assert(not main._valid_trade_quote({"port_id": true, "quantity": 1, "commodity": "ORE", "buy_price": 1, "total_buy_price": 1}, {"port_id": 7, "quantity": 1, "commodity": "ORE"}, "buy_price", "total_buy_price"))
	assert(not main._valid_trade_quote({"port_id": 7.0, "quantity": 1, "commodity": "ORE", "buy_price": 1, "total_buy_price": 1}, {"port_id": 7, "quantity": 1, "commodity": "ORE"}, "buy_price", "total_buy_price"))
	assert(not main._valid_trade_quote({"port_id": 0, "quantity": 1, "commodity": "ORE", "buy_price": 1, "total_buy_price": 1}, {"port_id": 7, "quantity": 1, "commodity": "ORE"}, "buy_price", "total_buy_price"), "zero quote port ID was accepted")
	assert(not main._valid_trade_quote({"port_id": 7, "quantity": 0, "commodity": "ORE", "buy_price": 1, "total_buy_price": 1}, {"port_id": 7, "quantity": 1, "commodity": "ORE"}, "buy_price", "total_buy_price"), "zero quote quantity was accepted")
	assert(not main._valid_trade_quote({"port_id": 7, "quantity": 3.0, "commodity": "ORE", "buy_price": 1, "total_buy_price": 3}, {"port_id": 7, "quantity": 3, "commodity": "ORE"}, "buy_price", "total_buy_price"))
	assert(not main._valid_trade_quote({"port_id": 7, "quantity": 1, "commodity": "ORE", "buy_price": 1.0, "total_buy_price": 1}, {"port_id": 7, "quantity": 1, "commodity": "ORE"}, "buy_price", "total_buy_price"))
	assert(main._valid_trade_quote({"port_id": 7, "quantity": 1, "commodity": "ORE"}, {"port_id": 7, "quantity": 1, "commodity": "ORE"}, "buy_price", "total_buy_price"), "schema-valid quote without optional price fields was rejected")
	assert(not main._valid_trade_quote({"port_id": 7, "quantity": 1, "commodity": " ORE "}, {"port_id": 7, "quantity": 1, "commodity": "ORE"}, "buy_price", "total_buy_price"), "whitespace-padded quote commodity was accepted")
	assert(not main._valid_trade_receipt({"port_id": 7, "sector_id": 17}, {"direction": "buy", "port_id": 7, "sector_id": 17}))
	assert(not main._valid_trade_receipt({"port_id": 7, "sector_id": 17, "lines": []}, {"direction": "buy", "port_id": 7, "sector_id": 17, "items": [{"commodity": "ORE", "quantity": 1}]}, "trade.buy_receipt_v1"), "empty settlement lines were accepted")
	assert(not main._valid_trade_receipt({"port_id": 7, "sector_id": 17}, {"direction": "buy", "port_id": 7, "sector_id": 17}, "trade.buy_receipt_v1"), "receipt without player identity was accepted without a known session")
	assert(not main._valid_trade_receipt({"port_id": 7, "sector_id": 17}, {"direction": "buy", "port_id": 7, "sector_id": 17}, ""), "missing receipt type was accepted")
	assert(not main._valid_trade_receipt({"port_id": 7, "sector_id": 17}, {"direction": "buy", "port_id": 7, "sector_id": 17}, "trade.sell_receipt_v1"))
	assert(not main._valid_trade_receipt({"port_id": 8, "sector_id": 17}, {"direction": "buy", "port_id": 7, "sector_id": 17}))
	assert(not main._valid_trade_receipt({"port_id": 7.0, "sector_id": 17}, {"direction": "buy", "port_id": 7, "sector_id": 17}))
	assert(not main._valid_trade_receipt({"port_id": 7, "sector_id": 17, "lines": [{"commodity": "ORG", "quantity": 1}]}, {"direction": "buy", "port_id": 7, "sector_id": 17, "items": [{"commodity": "ORE", "quantity": 1}]}), "receipt line did not match the requested commodity")
	assert(not main._valid_trade_receipt({"port_id": 7, "sector_id": 17, "lines": [{"commodity": true, "quantity": 1}]}, {"direction": "buy", "port_id": 7, "sector_id": 17, "items": [{"commodity": "ORE", "quantity": 1}]}), "malformed receipt commodity was coerced into the requested identity")
	assert(not main._valid_trade_receipt({"port_id": 7, "sector_id": 17, "lines": [{"commodity": "ORE", "quantity": 2}]}, {"direction": "buy", "port_id": 7, "sector_id": 17, "items": [{"commodity": "ORE", "quantity": 1}]}), "receipt line did not match the requested quantity")
	assert(not main._valid_trade_receipt({"port_id": 7, "sector_id": 17, "player_id": 99}, {"direction": "buy", "port_id": 7, "sector_id": 17}, "trade.buy_receipt_v1", "buy", 14), "receipt for another player was accepted")
	assert(not main._valid_trade_receipt({"port_id": 7, "sector_id": 17, "player_id": 14}, {"direction": "buy", "port_id": 7, "sector_id": 17}, "trade.buy_receipt_v1", "buy", 0), "receipt was accepted without an authenticated player identity")
	assert(not main._valid_trade_receipt({"port_id": 7, "sector_id": 17}, {"direction": "buy", "port_id": 7, "sector_id": 17}, "trade.buy_receipt_v1", "buy", 14), "receipt without player identity was accepted")
	main._pending_game_commands.clear()
	main._request_port_refresh()
	main._on_request_timed_out("request-9", "port.info")
	assert(view.port_workflow.closed and view.messages.back().contains("reopen"))
	main._on_trade_requested("buy", 7, 17, "ORE", 1, 0)
	main._pending_game_commands.clear()
	main._finish_trade_quote({"status": "ok", "type": "trade.quote", "data": {"port_id": 7, "commodity": "ORE", "quantity": 1, "buy_price": 3, "total_buy_price": 3}})
	main._on_trade_confirmation(true)
	operation = main._pending_game_commands["request-11"]
	main._pending_game_commands.clear()
	await main._finish_command_reply({"status": "refused", "error": {"message": "Stock exhausted"}}, operation)
	assert(main._pending_trade.get("refresh_port_after", false) and view.messages.back().contains("Stock exhausted") and main.refresh_coordinator.calls == 2)
	main._on_refresh_finished({})
	assert(transport.calls.back().command == "port.info")
	assert(transport.calls.back().data == {"port_id": 7}, "refused-trade refresh did not use the confirmed port ID")
	main._on_trade_requested("buy", 7, 17, "ORE", 1, 0)
	main._pending_game_commands.clear()
	main._finish_trade_quote({"status": "ok", "type": "trade.quote", "data": {"port_id": 7, "commodity": "ORE", "quantity": 1, "buy_price": 3, "total_buy_price": 3}})
	view.port_workflow.closed = false
	main._on_transport_disconnected("test")
	assert(main._pending_trade.is_empty() and view.quote_dismissed and view.port_workflow.closed and not view.command_menu.tavern_access)
	main._on_trade_confirmation(true)
	assert(transport.calls.size() == 13)
	main._on_trade_requested("buy", 7, 17, "ORE", 1, 0)
	main._pending_game_commands.clear()
	main._finish_trade_quote({"status": "ok", "type": "trade.quote", "data": {"port_id": 7, "commodity": "ORE", "quantity": 1, "buy_price": 3.0, "total_buy_price": 3.0}})
	assert(not main._pending_trade.is_empty(), "documented integral JSON-real quote prices were rejected")
	main._pending_trade.clear()
	main._finish_trade_quote({"status": "ok", "type": "trade.quote", "data": {"port_id": 7, "commodity": "ORE", "quantity": 1, "buy_price": 3, "total_buy_price": 3}})
	main._on_trade_confirmation(true)
	main._pending_trade.clear()
	main._pending_game_commands.clear()
	main._on_trade_requested("buy", 7, 17, "ORE", 1, 0)
	main._finish_trade_quote({"status": "ok", "type": "trade.quote", "data": {"port_id": 7, "commodity": "ORE", "quantity": 1, "buy_price": 3.25, "total_buy_price": 3.25}})
	assert(not main._pending_trade.is_empty(), "schema-valid real-valued trade quote was rejected")
	main._on_request_failed("request-15", "trade.buy", {"kind": "disconnected", "message": "Connection lost"})
	assert(main._pending_trade.is_empty() and view.messages.back().contains("outcome is uncertain"))
	main._pending_game_commands["request-17"] = {"kind": "command", "command": "trade.buy", "label": "Buy", "data": {}}
	main._on_request_failed("request-17", "trade.buy", {"kind": "transport", "message": "reply channel failed"})
	assert(main._pending_trade.get("refresh_port_after", false), "trade request failure did not retain the port-refresh reconciliation marker")
	assert(main.refresh_coordinator.calls == 3 and main.refresh_coordinator.domains.back() == ["player", "ship", "sector"], "trade request failure did not refresh authoritative player, ship, and sector state")
	assert(view.messages.back().contains("refreshing confirmed balances, cargo and port stock"))
	main._pending_trade = {"direction": "buy", "port_id": 7, "sector_id": 17, "commodity": "ORE", "quantity": 1, "account": 0}
	main._pending_game_commands.clear()
	main._on_request_timed_out("request-16", "trade.quote")
	assert(main._pending_trade.is_empty() and view.messages.back().contains("refreshing port stock"), "quote timeout did not reconcile cached port stock")
	assert(transport.calls.back().command == "port.info" and main._pending_game_commands.has("request-16"), "quote timeout did not request an authoritative port refresh")
	main._pending_trade = {"direction": "buy", "port_id": 7, "sector_id": 17, "commodity": "ORE", "quantity": 1, "account": -1}
	main._on_trade_confirmation(true)
	var saved_preference_payload: Dictionary = transport.calls.back().data
	assert(not saved_preference_payload.has("account"), "server trade-account preference was serialized as account -1")
	assert(main._pending_trade.is_empty() and view.busy, "confirmed trade did not remain busy while awaiting the server")
	transport.return_empty = true
	main._on_trade_confirmation(true)
	assert(main._pending_trade.is_empty() and not view.busy, "unsent trade left the client command rail busy")
	main.login_view.free()
	view.free()
	main.free()
	print("Godot trade lifecycle tests: 22 passed")
	quit(0)
