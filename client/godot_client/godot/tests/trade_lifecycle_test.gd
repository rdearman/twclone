extends SceneTree

const Main = preload("res://Main.gd")

class FakeAuth extends RefCounted:
	var session_token := "test-session"
	func is_authenticated() -> bool: return true

class FakeTransport extends RefCounted:
	var calls: Array[Dictionary] = []
	func request(command: String, data: Dictionary, token: String) -> String:
		calls.append({"command": command, "data": data.duplicate(true), "token": token})
		return "request-%d" % calls.size()

class FakeView extends Node:
	var quotes: Array[Dictionary] = []
	var messages: Array[String] = []
	func set_command_busy(_busy: bool) -> void: pass
	func show_notification(message: String) -> void: messages.append(message)
	func confirm_trade_quote(direction: String, commodity: String, quantity: int, account: int, quote: Dictionary) -> void:
		quotes.append({"direction": direction, "commodity": commodity, "quantity": quantity, "account": account, "quote": quote})

func _initialize() -> void:
	var main = Main.new()
	var view = FakeView.new()
	var transport = FakeTransport.new()
	main.gameplay_view = view
	main.auth_session = FakeAuth.new()
	main.transport = transport
	main._on_trade_requested("buy", 7, 17, "ORE", 3, 1)
	assert(transport.calls.size() == 1 and transport.calls[0].command == "trade.quote")
	assert(transport.calls[0].data == {"port_id": 7, "commodity": "ORE", "quantity": 3})
	main._pending_game_commands.clear()
	main._finish_trade_quote({"status": "ok", "data": {"total_buy_price": 120}})
	assert(view.quotes.size() == 1 and view.quotes[0].account == 1)
	main._on_trade_confirmation(false)
	assert(main._pending_trade.is_empty() and transport.calls.size() == 1)
	main._on_trade_requested("sell", 7, 17, "ORG", 2, 1)
	main._pending_game_commands.clear()
	main._finish_trade_quote({"status": "ok", "data": {"total_sell_price": 80}})
	main._on_trade_confirmation(true)
	assert(transport.calls.size() == 3 and transport.calls[2].command == "trade.sell")
	var payload: Dictionary = transport.calls[2].data
	assert(payload.account == 1 and payload.port_id == 7 and payload.sector_id == 17)
	assert(payload.items == [{"commodity": "ORG", "quantity": 2}] and not str(payload.idempotency_key).is_empty())
	main._pending_trade.clear()
	main._pending_game_commands.clear()
	main._on_trade_requested("buy", 7, 17, "ORE", 1, 0)
	main._finish_trade_quote({"status": "refused", "error": {"message": "Unavailable"}})
	assert(main._pending_trade.is_empty() and view.messages.back().contains("Unavailable"))
	view.free()
	main.free()
	print("Godot trade lifecycle tests: 8 passed")
	quit(0)
