extends SceneTree

const EventPresenter = preload("res://EventPresenter.gd")

func _initialize() -> void:
	var failures: Array[String] = []
	_check(EventPresenter.notification_for({"type": "system.notice", "category": "system", "data": {"title": "Maintenance", "body": "Ten minutes."}}) == "Maintenance · Ten minutes.", "system notice was not presented clearly")
	_check(EventPresenter.notification_for({"type": "combat.ship_damage", "category": "combat", "data": {"target_ship_id": 8, "damage": 4}}) == "Combat · Ship Damage", "combat event exposed IDs or was not summarized")
	_check(EventPresenter.notification_for({"type": "vendor.private", "category": "unknown", "data": {"body": "hidden"}}).is_empty(), "unknown event was surfaced to gameplay")
	_check(EventPresenter.notification_for({"type": "system.notice", "data": []}).is_empty(), "malformed event data was surfaced")
	if failures.is_empty():
		print("Godot event presenter tests: 4 passed")
		quit(0)
	else:
		for failure in failures:
			push_error(failure)
		quit(1)

func _check(condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)

var failures: Array[String] = []
