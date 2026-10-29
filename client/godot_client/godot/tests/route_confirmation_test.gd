extends SceneTree

const Main = preload("res://Main.gd")
const Protocol = preload("res://Protocol.gd")

class FakeView extends Node:
	var messages: Array[String] = []
	func clear_planned_route() -> void: pass
	func show_notification(message: String) -> void: messages.append(message)
	func set_action_pending(_pending: bool) -> void: pass

class FakeRefresh extends RefCounted:
	var calls := 0
	func refresh(_include_sector: bool) -> bool:
		calls += 1
		return false

func _initialize() -> void:
	assert(Protocol.confirmed_move_sector({"current_sector": 42}) == 42)
	assert(Protocol.confirmed_move_sector({"to_sector_id": 42, "sector_id": 10}) == 42)
	assert(Protocol.confirmed_move_sector({"sector_id": 42}) == 42)
	assert(Protocol.confirmed_move_sector({}) == 0)
	assert(Protocol.confirmed_move_sector({"sector_id": "42"}) == 0)
	for data in [{}, {"to_sector_id": 99}]:
		var main = Main.new()
		var view = FakeView.new()
		var refresh = FakeRefresh.new()
		main.gameplay_view = view
		main.refresh_coordinator = refresh
		main._route_running = true
		main._planned_route.append(43)
		main._finish_route_warp({"status": "ok", "data": data}, {"destination": 42})
		assert(not main._route_running)
		assert(main._planned_route.is_empty())
		assert(main._route_completed_hops == 0)
		assert(refresh.calls == 1)
		assert(view.messages[0].contains("did not confirm"))
		view.free()
		main.free()
	var main = Main.new()
	var view = FakeView.new()
	var refresh = FakeRefresh.new()
	main.gameplay_view = view
	main.refresh_coordinator = refresh
	main._route_running = true
	main._finish_route_warp({"status": "ok", "data": {"to_sector_id": 42}}, {"destination": 42})
	assert(main._route_completed_hops == 1)
	assert(view.messages[0].contains("Autonav complete"))
	assert(refresh.calls == 1)
	view.free()
	main.free()
	print("Godot route confirmation tests: 18 passed")
	quit(0)
