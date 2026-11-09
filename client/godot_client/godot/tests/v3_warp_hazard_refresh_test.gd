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

	# A refusal that carries a terminal hazard report still requires a complete
	# authoritative refresh because movement/hazard application is server-owned.
	await app._finish_warp_reply({
		"status": "refused",
		"error": {"message": "Entry hazard stopped movement.", "data": {
			"hazards": [{"hazard_type": "fighter", "hull_lost": 9}],
		}},
	}, 44)
	_check(fake_refresh.calls.size() == 1, "warp hazard refusal did not refresh authoritative state")
	if fake_refresh.calls.size() == 1:
		_check(fake_refresh.calls[0] == ["player", "ship", "sector"], "warp hazard refusal refreshed the wrong domains")
	_check(app.gameplay_view.information_text.text.contains("fighter"), "warp hazard details were not presented")
	_check(app.gameplay_view.notification_label.text.contains("Warp refused"), "warp refusal was not presented")

	app.queue_free()
	await process_frame
	if failures.is_empty():
		print("Godot V3 warp hazard refresh test: 4 checks passed")
		quit(0)
	else:
		for failure in failures:
			push_error(failure)
		quit(1)

func _check(condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)
