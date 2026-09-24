extends SceneTree

const PortWorkflow = preload("res://PortWorkflow.gd")

var failures: Array[String] = []
var captured: Dictionary = {}
var tavern_enters := 0
var tavern_exits := 0
var tavern_requests := 0

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	var view = PortWorkflow.new()
	root.add_child(view)
	await process_frame
	_check(view.get_node("WorkflowDim").offset_left == PortWorkflow.COMMAND_RAIL_CLEARANCE, "port/Tavern overlay covers the persistent command rail")
	_check(view.get_node("WorkflowMargin").offset_left > PortWorkflow.COMMAND_RAIL_CLEARANCE, "port/Tavern content does not begin beyond the command rail")
	view.trade_requested.connect(func(direction: String, port_id: int, sector_id: int, commodity: String, quantity: int) -> void:
		captured = {"direction": direction, "port_id": port_id, "sector_id": sector_id, "commodity": commodity, "quantity": quantity}
	)
	view.tavern_entered.connect(func() -> void: tavern_enters += 1)
	view.tavern_exited.connect(func() -> void: tavern_exits += 1)
	view.tavern_enter_requested.connect(func() -> void: tavern_requests += 1)
	view.show_port({"port": {"id": 7, "name": "Helix", "commodities": [
		{"code": "ORE", "quantity": 8.0, "max_quantity": 30.0, "price": "40.00"},
		{"code": "ORG", "quantity": 0.0, "max_quantity": 30.0, "price": "50.00"},
	]}}, 17)
	_check(view.visible, "port screen did not open after supplied server data")
	_check(view._commodity_list.get_child_count() == 2, "port commodities were not composed into rows")
	view._choose_trade("buy", "ORE", {"quantity": 8, "max_quantity": 30})
	view._quantity.value = 3
	view._submit_trade()
	_check(captured.get("direction") == "buy" and captured.get("port_id") == 7 and captured.get("sector_id") == 17, "trade selection lost authoritative context")
	_check(captured.get("commodity") == "ORE" and captured.get("quantity") == 3, "trade quantity selection was not preserved")
	_check(not view._tavern_button.visible, "a non-StarDock port incorrectly exposed Tavern access")
	view.show_port({"port": {"id": 8, "name": "Old port payload", "commodities": []}}, 17)
	_check(not view._tavern_button.visible, "a port without an authoritative type incorrectly exposed Tavern access")
	view.show_port({"port": {"id": 8, "name": "Federation StarDock", "type": 0, "commodities": []}}, 17)
	_check(not view._tavern_button.visible, "a StarDock-like name granted Tavern access without the server StarDock type")
	view.show_port({"port": {"id": 8, "name": "Black Market", "type": 10, "commodities": []}}, 17)
	_check(not view._tavern_button.visible, "a black-market port incorrectly exposed StarDock Tavern access")
	view.show_port({"port": {"id": 8, "name": "Helix", "type": 9, "commodities": []}}, 17)
	_check(view._tavern_button.visible, "StarDock did not expose the Enter Tavern navigation control")
	view._tavern_button.pressed.emit()
	_check(tavern_requests == 1 and tavern_enters == 0 and not view._tavern_mode and view._commodity_region.visible, "Tavern entry bypassed the server presence check")
	view.confirm_tavern_entered()
	_check(tavern_enters == 1 and view._tavern_mode and not view._commodity_region.visible, "confirmed StarDock tavern access did not switch the port workflow mode")
	view._back_button.pressed.emit()
	_check(tavern_exits == 1 and not view._tavern_mode and view._commodity_region.visible, "leaving the tavern did not restore the port screen")
	view._tavern_button.pressed.emit()
	_check(tavern_requests == 2 and not view._tavern_mode, "re-entering the tavern skipped the server presence check")
	view.confirm_tavern_entered()
	view.show_port({"port": {"id": 9, "name": "Helix Trade Port", "type": 0, "commodities": []}}, 17)
	_check(tavern_exits == 2 and not view._tavern_mode and not view._tavern_button.visible, "replacing the port view failed to revoke Tavern access")
	view.enter_tavern()
	_check(tavern_requests == 2 and not view._tavern_mode, "Tavern access was granted on a non-StarDock port")
	view.queue_free()
	await process_frame
	if failures.is_empty():
		print("Godot port workflow tests: 17 passed")
		quit(0)
	else:
		for failure in failures:
			push_error(failure)
		quit(1)

func _check(condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)
