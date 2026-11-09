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
	var refresh := FakeRefresh.new()
	app.refresh_coordinator = refresh
	app.gameplay_view.set_snapshot({
		"authenticated": true,
		"disconnected": false,
		"hud": {"sector_id": 12},
		"freshness": {"sector": "fresh", "overall": "fresh"},
		"sector": {"sector_id": 12, "name": "Known sector"},
	})
	app.gameplay_view.show_information("Current sector", "Known sector only")

	await app._finish_command_reply({
		"status": "ok",
		"type": "sector.scan",
		"data": {
			"sector_id": 99,
			"name": "Remote sector",
			"ships": [{"name": "Untrusted ship"}],
		},
	}, {
		"label": "Detailed current-sector scan",
		"command": "sector.scan",
		"mutating": false,
	})

	_check(not app.gameplay_view.information_text.text.contains("Untrusted ship"),
		"scan for a different sector was rendered")
	_check(app.gameplay_view.information_text.text.contains("Known sector only"),
		"confirmed information was replaced by a mismatched scan")
	_check(app.gameplay_view.notification_label.text.contains("returned sector 99"),
		"mismatched scan identity was not explained")
	_check(refresh.calls.size() == 1,
		"mismatched scan did not request an authoritative refresh")
	if refresh.calls.size() == 1:
		_check(refresh.calls[0] == ["sector"],
			"mismatched scan refreshed player or ship state")

	refresh.calls.clear()
	await app._finish_command_reply({
		"status": "refused",
		"type": "sector.scan",
		"error": {
			"message": "The requested sector is not visible.",
			"data": {
				"sector_id": 99,
				"name": "Untrusted refusal payload",
				"ships": [{"name": "Untrusted ship"}],
			},
		},
	}, {
		"label": "Detailed current-sector scan",
		"command": "sector.scan",
		"mutating": false,
	})

	_check(not app.gameplay_view.information_text.text.contains("Untrusted refusal payload"),
		"refused scan payload replaced confirmed information")
	_check(app.gameplay_view.information_text.text.contains("Known sector only"),
		"refused scan discarded the confirmed information")
	_check(app.gameplay_view.notification_label.text.contains("not visible"),
		"scan refusal was not explained")
	_check(refresh.calls.size() == 1,
		"refused scan did not request an authoritative refresh")
	if refresh.calls.size() == 1:
		_check(refresh.calls[0] == ["sector"],
			"refused scan refreshed player or ship state")

	refresh.calls.clear()
	await app._finish_command_reply({
		"status": "ok",
		"type": "sector.density.scan",
		"data": {"sectors": [{"sector_id": 12, "density": "not-a-number"}]},
	}, {
		"label": "Sector density scan",
		"command": "sector.scan.density",
		"mutating": false,
	})

	_check(not app.gameplay_view.information_text.text.contains("not-a-number"),
		"malformed density was rendered as authoritative")
	_check(refresh.calls.size() == 1,
		"malformed density did not request an authoritative refresh")
	if refresh.calls.size() == 1:
		_check(refresh.calls[0] == ["sector"],
			"malformed density refreshed unrelated state domains")

	app.queue_free()
	await process_frame
	if failures.is_empty():
		print("Godot V3 visibility scan identity tests: 12 checks passed")
		quit(0)
	else:
		for failure in failures:
			push_error(failure)
		quit(1)

func _check(condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)
