extends SceneTree

const PortWorkflow = preload("res://PortWorkflow.gd")

var failures: Array[String] = []
var captured: Dictionary = {}

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	var view = PortWorkflow.new()
	root.add_child(view)
	await process_frame
	view.trade_requested.connect(func(direction: String, port_id: int, sector_id: int, commodity: String, quantity: int) -> void:
		captured = {"direction": direction, "port_id": port_id, "sector_id": sector_id, "commodity": commodity, "quantity": quantity}
	)
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
	view.queue_free()
	await process_frame
	if failures.is_empty():
		print("Godot port workflow tests: 4 passed")
		quit(0)
	else:
		for failure in failures:
			push_error(failure)
		quit(1)

func _check(condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)
