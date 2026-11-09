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
		"status": "ok",
		"data": {"source_ship_id": 12, "target_ship_id": 88, "commodity_id": 2,
			"quantity": 3, "source_remaining": 4, "target_quantity": 7},
	}, operation)
	_check(refresh.calls == [["player", "ship", "sector"]], "untyped cargo receipt did not reconcile authoritative state")
	_check(not app.gameplay_view.information_text.text.contains("Transfer cargo"), "untyped cargo receipt was presented as confirmed")
	_check(app.gameplay_view.notification_label.text.contains("did not match"), "untyped cargo receipt was not surfaced")

	app.queue_free()
	await process_frame
	if failures.is_empty():
		print("Godot V3 ship cargo missing-type test: 3 checks passed")
		quit(0)
	for failure in failures:
		push_error(failure)
	quit(1)

func _check(condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)
