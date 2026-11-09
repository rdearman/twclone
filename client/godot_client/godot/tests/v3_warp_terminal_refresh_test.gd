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

	await app._finish_warp_reply({
		"status": "ok",
		"data": {
			"current_sector": 88,
			"ship_destroyed": true,
			"ship_remains_in_sector": false,
			"hazards": [{"hazard_type": "minefield", "damage": 100}],
		},
	}, 88)

	_check(fake_refresh.calls.size() == 1, "terminal warp did not refresh authoritative state")
	if fake_refresh.calls.size() == 1:
		_check(fake_refresh.calls[0] == [], "warp refresh unexpectedly narrowed authoritative domains")
	_check(app.gameplay_view.notification_label.text.contains("Warp outcome is terminal"), "terminal warp outcome was not presented")
	_check(not app.gameplay_view.notification_label.text.contains("Warp confirmed"), "terminal warp was presented as confirmed")

	if failures.is_empty():
		print("Godot terminal warp refresh test: 4 checks passed")
		quit(0)
	else:
		for failure in failures:
			push_error(failure)
		quit(1)

func _check(condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)
