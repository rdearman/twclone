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
	var fake_refresh := FakeRefresh.new()
	app.refresh_coordinator = fake_refresh
	await app._finish_command_reply({
		"status": "ok",
		"type": "move.result",
		"data": {"hazards": [{"hazard_type": "minefield", "hull_lost": 8}]},
	}, {"label": "Tow ship", "command": "ship.tow", "mutating": true})

	_check(fake_refresh.calls == [["player", "ship", "sector"]], "mismatched tow response did not refresh all affected domains")
	_check(not app.gameplay_view.information_text.text.contains("minefield"), "mismatched tow response rendered untrusted hazard data")
	_check(app.gameplay_view.notification_label.text.contains("Tow response was not recognized"), "mismatched tow response was not rejected")

	fake_refresh.calls.clear()
	app.gameplay_view.information_text.text = ""
	await app._finish_command_reply({
		"status": "ok",
		"data": {"hazards": [{"hazard_type": "minefield", "hull_lost": 8}]},
	}, {"label": "Tow ship", "command": "ship.tow", "mutating": true})

	_check(fake_refresh.calls == [["player", "ship", "sector"]], "missing tow response type did not refresh all affected domains")
	_check(not app.gameplay_view.information_text.text.contains("minefield"), "missing tow response type rendered untrusted hazard data")

	fake_refresh.calls.clear()
	app.gameplay_view.information_text.text = ""
	await app._finish_command_reply({
		"status": "refused",
		"type": "error",
		"error": {"message": "Entry hazard stopped towing", "data": {
			"hazards": [{"hazard_type": "limpet", "damage": 4}],
		}},
	}, {"label": "Tow ship", "command": "ship.tow", "mutating": true})

	_check(fake_refresh.calls == [["player", "ship", "sector"]], "canonical tow refusal did not refresh all affected domains")
	_check(app.gameplay_view.information_text.text.contains("limpet"), "canonical tow refusal hid server hazard details")
	_check(app.gameplay_view.notification_label.text.contains("Tow refused"), "canonical tow refusal was not presented")

	app.queue_free()
	await process_frame
	if failures.is_empty():
		print("Godot V3 tow response type guard test: 3 checks passed")
		quit(0)
	else:
		for failure in failures:
			push_error(failure)
		quit(1)

func _check(condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)
