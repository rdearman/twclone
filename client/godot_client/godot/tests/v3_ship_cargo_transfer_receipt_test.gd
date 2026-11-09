extends SceneTree

const MainScene = preload("res://Main.tscn")

class FakeRefresh extends RefCounted:
	var calls: Array = []
	func refresh(_force: bool = false, domains: Array = []) -> bool:
		calls.append(domains.duplicate())
		return true

var failures: Array[String] = []

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	var app = MainScene.instantiate()
	root.add_child(app)
	await process_frame
	app.gameplay_view._snapshot = {"hud": {"ship_id": 12}}
	var refresh := FakeRefresh.new()
	app.refresh_coordinator = refresh
	var operation := {"label": "Transfer cargo", "command": "ship.transfer_cargo", "mutating": true,
		"data": {"commodity_id": 2, "quantity": 3, "target_ship_id": 88}}

	await app._finish_command_reply({
		"status": "ok", "type": "ship.cargo_transferred_v1",
		"data": {"source_ship_id": 12, "target_ship_id": 88, "commodity_id": 2,
			"quantity": 3, "source_remaining": 4, "target_quantity": 7},
	}, operation)
	_check(refresh.calls == [[]], "valid cargo receipt did not refresh all authoritative domains")
	_check(app.gameplay_view.information_text.text.contains("Transfer cargo"), "valid cargo transfer was not presented")

	await app._finish_command_reply({
		"status": "ok", "type": "ship.cargo_transferred_v1",
		"data": {"source_ship_id": 12, "target_ship_id": 99, "commodity_id": 2,
			"quantity": 3, "source_remaining": 1, "target_quantity": 3},
	}, operation)
	_check(refresh.calls.size() == 2 and refresh.calls[1] == ["player", "ship", "sector"], "mismatched target receipt did not reconcile authoritative state")
	_check(app.gameplay_view.notification_label.text.contains("did not match"), "mismatched target receipt was not surfaced")

	await app._finish_command_reply({
		"status": "ok", "type": "ship.status",
		"data": {"source_ship_id": 12, "target_ship_id": 88, "commodity_id": 2,
			"quantity": 3, "source_remaining": 4, "target_quantity": 7},
	}, operation)
	_check(refresh.calls.size() == 3 and refresh.calls[2] == ["player", "ship", "sector"], "wrong response type did not reconcile authoritative state")

	app.queue_free()
	await process_frame
	if failures.is_empty():
		print("Godot V3 ship cargo transfer receipt tests: 6 checks passed")
		quit(0)
	for failure in failures:
		push_error(failure)
	quit(1)

func _check(condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)
