extends SceneTree

const EventPresenter = preload("res://EventPresenter.gd")

var failures: Array[String] = []
var checks := 0

func _initialize() -> void:
	_check(EventPresenter.notification_for({"type": "system.notice", "category": "system", "data": {"title": "Maintenance", "body": "Ten minutes."}}) == "Maintenance · Ten minutes.", "system notice was not presented clearly")
	_check(EventPresenter.notification_for({"type": "system.notice_v1", "category": "system", "data": {"title": "Maintenance", "body": "Ten minutes."}}) == "Maintenance · Ten minutes.", "server system.notice_v1 event was not presented")
	_check(EventPresenter.notification_for({"type": "tavern.round.bought", "category": "chat", "data": {"message": "A round has been bought for everyone!", "sector_id": 17}}) == "Comms · A round has been bought for everyone!", "tavern broadcast message was not presented")
	_check(EventPresenter.notification_for({"type": "combat.ship_damage", "category": "combat", "data": {"target_ship_id": 8, "damage": 4}}) == "Combat · Ship Damage", "combat event exposed IDs or was not summarized")
	_check(EventPresenter.notification_for({"type": "combat.hit", "category": "combat", "data": {"v": 1, "attacker_id": 12, "defender_id": 99, "weapon": "laser_mk2", "damage": 42, "defender_hp_after": 58, "sector_id": 278}}) == "Combat · laser_mk2 hit · 42 damage · target HP 58 · sector 278", "combat hit omitted authoritative damage details")
	var sector_notice := EventPresenter.notification_for({"type": "sector.notice", "category": "navigation", "data": {"subtype": "fighters_deployed", "sector_id": 42, "details": {"count": 12, "offense": 50}}})
	_check(sector_notice.contains("fighters deployed in sector 42 (12)"), "sector notice was reduced to an unhelpful generic event label")
	var beacon_notice := EventPresenter.notification_for({"type": "sector.notice", "category": "navigation", "data": {"subtype": "beacon_set", "sector_id": 9, "details": {"beacon": "Rendezvous here"}}})
	_check(beacon_notice.contains("Sector beacon set in sector 9: Rendezvous here"), "beacon notice omitted its server-provided message")
	_check(EventPresenter.notification_for({"type": "vendor.private", "category": "unknown", "data": {"body": "hidden"}}).is_empty(), "unknown event was surfaced to gameplay")
	_check(EventPresenter.notification_for({"type": "system.notice", "data": []}).is_empty(), "malformed event data was surfaced")
	if failures.is_empty():
		print("Godot event presenter tests: %d checks passed" % checks)
		quit(0)
	else:
		for failure in failures:
			push_error(failure)
		quit(1)

func _check(condition: bool, message: String) -> void:
	checks += 1
	if not condition:
		failures.append(message)
