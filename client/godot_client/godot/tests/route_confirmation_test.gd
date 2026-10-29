extends SceneTree

const Main = preload("res://Main.gd")
const Protocol = preload("res://Protocol.gd")

class FakeView extends Node:
	var messages: Array[String] = []
	var information := ""
	func clear_planned_route() -> void: pass
	func show_notification(message: String) -> void: messages.append(message)
	func show_information(_title: String, content: String) -> void: information = content
	func set_action_pending(_pending: bool) -> void: pass
	func set_command_busy(_pending: bool) -> void: pass

class FakeRefresh extends RefCounted:
	var calls := 0
	func refresh(_include_sector: bool, _domains: Array = []) -> bool:
		calls += 1
		return false

func _initialize() -> void:
	assert(Protocol.confirmed_move_sector({"current_sector": 42}) == 42)
	assert(Protocol.confirmed_move_sector({"to_sector_id": 42, "sector_id": 10}) == 42)
	assert(Protocol.confirmed_move_sector({"sector_id": 42}) == 42)
	assert(Protocol.confirmed_move_sector({}) == 0)
	assert(Protocol.confirmed_move_sector({"sector_id": "42"}) == 0)
	var hazard := {"hazard_type": "radiation", "damage": 17, "shields_lost": 10, "hull_lost": 7}
	assert(Protocol.move_hazard_report({"data": {"hazards": [hazard]}}).contains("shields lost: 10"))
	assert(Protocol.move_hazard_report({"error": {"data": {"hazards": [hazard]}}}).contains("hull lost: 7"))
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
	main._finish_route_warp({"status": "ok", "data": {"to_sector_id": 42, "hazards": [hazard]}}, {"destination": 42})
	assert(main._route_completed_hops == 1)
	assert(view.messages[0].contains("Autonav complete"))
	assert(refresh.calls == 1)
	assert(view.information.contains("radiation"))
	view.free()
	main.free()
	main = Main.new()
	view = FakeView.new()
	refresh = FakeRefresh.new()
	main.gameplay_view = view
	main.refresh_coordinator = refresh
	main._finish_command_reply({"status": "error", "error": {
		"message": "Entry hazard processing failed",
		"data": {"hazards": [hazard]},
	}}, {"command": "move.transwarp", "label": "Transwarp"})
	assert(refresh.calls == 1)
	assert(view.information.contains("radiation"))
	assert(view.messages[0].contains("refreshing confirmed"))
	view.free()
	main.free()
	print("Godot route confirmation tests: 22 passed")
	quit(0)
