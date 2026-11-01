extends SceneTree

const Main = preload("res://Main.gd")
const Protocol = preload("res://Protocol.gd")

class FakeView extends Node:
	var messages: Array[String] = []
	var information := ""
	var _snapshot: Dictionary = {}
	var single_step_calls := 0
	func clear_planned_route() -> void: pass
	func show_notification(message: String) -> void: messages.append(message)
	func show_information(_title: String, content: String) -> void: information = content
	func show_restored_autopilot_route(_path: Array, _destination: int, _state: String, _mode: String) -> void: pass
	func set_action_pending(_pending: bool) -> void: pass
	func set_command_busy(_pending: bool) -> void: pass
	func play_warp_transition() -> void: pass
	func show_single_step_ready(_path: Array, _destination: int, _current_sector: int) -> void: single_step_calls += 1

class FakeRefresh extends RefCounted:
	var calls := 0
	func accepts_request(_request_id: String) -> bool: return false
	func refresh(_include_sector: bool, _domains: Array = []) -> bool:
		calls += 1
		return false

func _initialize() -> void:
	assert(Protocol.confirmed_move_sector({"current_sector": 42}) == 42)
	assert(Protocol.confirmed_move_sector({"to_sector_id": 42, "sector_id": 10}) == 42)
	assert(Protocol.confirmed_move_sector({"sector_id": 42}) == 42)
	assert(Protocol.confirmed_move_sector({}) == 0)
	assert(Protocol.confirmed_move_sector({"sector_id": "42"}) == 0)
	assert(Protocol.confirmed_move_sector({"sector_id": 42.0}) == 42)
	assert(Protocol.confirmed_move_sector({"sector_id": 42.5}) == 0)
	var hazard := {"hazard_type": "radiation", "damage": 17, "shields_lost": 10, "hull_lost": 7}
	assert(Protocol.move_hazard_report({"data": {"hazards": [hazard]}}).contains("shields lost: 10"))
	assert(Protocol.move_hazard_report({"error": {"data": {"hazards": [hazard]}}}).contains("hull lost: 7"))
	var terminal := {"hazard_type": "minefield", "destroyed": true, "ship_remains_in_sector": false}
	var terminal_report := Protocol.move_hazard_report({"data": {"hazards": [terminal]}})
	assert(terminal_report.contains("destroyed: true"))
	assert(terminal_report.contains("ship remains in sector: false"))
	for data in [{}, {"to_sector_id": 99}]:
		var main = Main.new()
		var view = FakeView.new()
		var refresh = FakeRefresh.new()
		main.gameplay_view = view
		main.refresh_coordinator = refresh
		main._route_running = true
		main._planned_route.append(43)
		main._finish_route_warp({"status": "ok", "type": "move.result", "data": data}, {"destination": 42})
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
	main._finish_warp_reply({"status": "ok", "type": "move.result", "data": {
		"to_sector_id": 42, "hazards": [hazard],
	}}, 42)
	assert(view.messages[0].contains("Warp confirmed"))
	assert(view.information.contains("radiation"))
	assert(refresh.calls == 1)
	view.free()
	main.free()
	main = Main.new()
	view = FakeView.new()
	refresh = FakeRefresh.new()
	main.gameplay_view = view
	main.refresh_coordinator = refresh
	main._route_running = true
	main._finish_route_warp({"status": "ok", "type": "move.result", "data": {"to_sector_id": 42, "hazards": [hazard]}}, {"destination": 42})
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
	main._route_running = true
	main._autopilot_mode = "single_step"
	main._route_stop_after_next = true
	main._planned_route.clear()
	main._planned_route.append(43)
	main._route_destination = 43
	main._finish_route_warp({"status": "ok", "type": "move.result", "data": {"to_sector_id": 42}}, {"destination": 42})
	assert(not main._route_running and main._planned_route == [43], "single-step completion discarded the remaining route")
	assert(view.single_step_calls == 1 and refresh.calls == 1, "single-step completion did not return to the continue/cancel state")
	view.free()
	main.free()
	main = Main.new()
	view = FakeView.new()
	refresh = FakeRefresh.new()
	main.gameplay_view = view
	main.refresh_coordinator = refresh
	main._finish_command_reply({"status": "error", "type": "error", "error": {
		"message": "Entry hazard processing failed",
		"data": {"hazards": [hazard]},
	}}, {"command": "move.transwarp", "label": "Transwarp"})
	assert(refresh.calls == 1)
	assert(view.information.contains("radiation"))
	assert(view.messages[0].contains("refreshing confirmed"))
	view.free()
	main.free()
	main = Main.new()
	view = FakeView.new()
	refresh = FakeRefresh.new()
	main.gameplay_view = view
	main.refresh_coordinator = refresh
	main._pending_game_commands["warp-failed"] = {"kind": "warp", "destination": 42}
	main._on_request_failed("warp-failed", "move.warp", {"kind": "disconnected", "message": "socket closed"})
	assert(refresh.calls == 1)
	assert(view.messages[0].contains("Refreshing authoritative state"))
	view.free()
	main.free()
	main = Main.new()
	view = FakeView.new()
	refresh = FakeRefresh.new()
	main.gameplay_view = view
	main.refresh_coordinator = refresh
	view._snapshot = {"hud": {"sector_id": 205}}
	main._finish_autopilot_status({"status": "ok", "data": {
		"state": "running", "mode": "express", "current_sector_id": 5,
		"target_sector_id": 999, "next_sector_id": 1030, "path": [5, 1030, 205, 2126, 2023, 999],
	}})
	assert(refresh.calls == 1)
	assert(view.messages[0].contains("route re-anchored"))
	assert(not view.messages[0].contains("running"))
	view.free()
	main.free()
	print("Godot route confirmation tests passed")
	quit(0)
