extends SceneTree

const MainScene = preload("res://Main.tscn")

var failures: Array[String] = []

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	var app = MainScene.instantiate()
	root.add_child(app)
	await process_frame

	for response in [
		{"status": "ok", "data": {"sector_id": 12, "name": "Missing type"}},
		{"status": "ok", "type": "", "data": {"sector_id": 12, "name": "Empty type"}},
	]:
		var refresh := _FakeRefresh.new()
		app.refresh_coordinator = refresh
		app.gameplay_view.set_snapshot({
			"authenticated": true,
			"disconnected": false,
			"hud": {"sector_id": 12},
			"freshness": {"sector": "fresh", "overall": "fresh"},
			"sector": {"sector_id": 12, "name": "Known sector"},
		})
		app.gameplay_view.show_information("Current sector", "Known sector only")

		await app._finish_command_reply(response, {
			"label": "Detailed current-sector scan",
			"command": "sector.scan",
			"mutating": false,
		})

		_check(not app.gameplay_view.information_text.text.contains("Missing type"),
			"scan without response type was rendered")
		_check(not app.gameplay_view.information_text.text.contains("Empty type"),
			"scan with empty response type was rendered")
		_check(app.gameplay_view.information_text.text.contains("Known sector only"),
			"confirmed visibility was replaced by an untyped response")
		_check(refresh.calls.size() == 1 and refresh.calls[0] == ["sector"],
			"untyped scan did not trigger a sector refresh")

	app.queue_free()
	await process_frame
	if failures.is_empty():
		print("Godot V3 visibility missing-type tests: 8 checks passed")
		quit(0)
	else:
		for failure in failures:
			push_error(failure)
		quit(1)

func _check(condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)

class _FakeRefresh extends RefCounted:
	var calls: Array = []

	func refresh(_force: bool = false, domains: Array = []) -> bool:
		calls.append(domains.duplicate())
		return true
