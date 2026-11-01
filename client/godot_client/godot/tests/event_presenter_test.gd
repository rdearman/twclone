extends SceneTree

const EventPresenter = preload("res://EventPresenter.gd")

var failures: Array[String] = []
var checks := 0

func _initialize() -> void:
	_check(EventPresenter.targets_player_ship_destruction({"type": "ship.destroyed", "data": {"player_id": 7}}, 7), "self-destruct event did not target the authenticated player")
	_check(EventPresenter.targets_player_ship_destruction({"type": "combat.ship_destroyed", "data": {"victim_player_id": 7, "attacker_player_id": 8}}, 7), "combat destruction event did not target the authenticated victim")
	_check(not EventPresenter.targets_player_ship_destruction({"type": "combat.ship_destroyed", "data": {"victim_player_id": 8}}, 7), "foreign destruction event triggered local reconciliation")
	_check(not EventPresenter.targets_player_ship_destruction({"type": "ship.destroyed", "data": {"player_id": "7"}}, 7), "string player identity was coerced into a destruction target")
	_check(EventPresenter.notification_for({"type": "system.notice", "category": "system", "data": {"title": "Maintenance", "body": "Ten minutes."}}) == "Maintenance · Ten minutes.", "system notice was not presented clearly")
	_check(EventPresenter.notification_for({"type": "system.notice_v1", "category": "system", "data": {"title": "Maintenance", "body": "Ten minutes."}}) == "Maintenance · Ten minutes.", "server system.notice_v1 event was not presented")
	_check(EventPresenter.notification_for({"type": "broadcast.message_v1", "category": "chat", "data": {"message": "All ships report to sector 7", "scope": "global", "scope_id": 0}}) == "Broadcast · All ships report to sector 7", "global broadcast was not presented")
	_check(EventPresenter.notification_for({"type": "broadcast.message_v1", "category": "chat", "data": {"message": 42}}).is_empty(), "malformed global broadcast was presented")
	_check(EventPresenter.notification_for({"type": "tavern.round.bought", "category": "chat", "data": {"message": "A round has been bought for everyone!", "sector_id": 17}}) == "Comms · A round has been bought for everyone!", "tavern broadcast message was not presented")
	_check(EventPresenter.notification_for({"type": "combat.ship_damage", "category": "combat", "data": {"target_ship_id": 8, "damage": 4}}) == "Combat · Ship Damage", "combat event exposed IDs or was not summarized")
	_check(EventPresenter.notification_for({"type": "combat.hit", "category": "combat", "data": {"v": 1, "attacker_id": 12, "defender_id": 99, "weapon": "laser_mk2", "damage": 42, "defender_hp_after": 58, "sector_id": 278}}) == "Combat · laser_mk2 hit · 42 damage · target HP 58 · sector 278", "combat hit omitted authoritative damage details")
	_check(EventPresenter.notification_for({"type": "player.attack_planet.v1", "category": "combat", "data": {"planet_id": 77, "result": "win", "ship_loss": 25, "planet_loss": 20}}) == "Combat · Planet attack · planet 77 · result: win · ship fighters lost: 25 · planet fighters lost: 20", "canonical planet attack event facts were not presented")
	_check(EventPresenter.notification_for({"type": "player.attack_planet.v1", "category": "combat", "data": {"planet_id": "77", "result": 1, "ship_loss": -1, "planet_loss": "20"}}) == "Combat · Planet attack", "malformed planet attack facts were presented")
	_check(EventPresenter.notification_for({"type": "player.capture_planet.v1", "category": "combat", "data": {"planet_id": 77, "previous_owner": 9}}) == "Combat · Planet captured · planet 77 · previous owner 9", "canonical planet capture event fact was not presented")
	_check(EventPresenter.notification_for({"type": "combat.planet_landing_defense.v1", "category": "combat", "data": {"planet_id": 42, "result": "repelled", "attacker_fighters_lost": 8, "attacker_fighters_remaining": 2, "planet_fighters_lost": 4, "planet_fighters_remaining": 16, "citadel_shields_absorbed": 12, "citadel_shields_remaining": 0}}) == "Combat · Planetary defense · repelled · attacker fighters lost: 8 · attacker fighters remaining: 2 · planet fighters lost: 4 · planet fighters remaining: 16 · citadel shields absorbed: 12 · citadel shields remaining: 0", "persisted planetary defense event was not presented")
	_check(EventPresenter.notification_for({"type": "combat.planet_landing_defense.v1", "category": "combat", "data": {"result": 7, "attacker_fighters_lost": "8", "planet_fighters_remaining": true}}) == "Combat · Planetary defense", "malformed planetary defense counters were presented as authoritative")
	_check(EventPresenter.notification_for({"type": "combat.hit.fighters", "category": "combat", "data": {"fighters_engaged": 3, "damage": 12, "shields_lost": 4, "fighters_lost": 2, "hull_lost": 6}}) == "Combat · Defensive fighters engaged · 3 engaged · damage: 12 · shields lost: 4 · fighters lost: 2 · hull lost: 6", "defensive fighter event omitted authoritative depletion")
	_check(EventPresenter.notification_for({"type": "combat.hit.quasar", "category": "combat", "data": {"source": 12, "damage": "4"}}) == "Combat · Atmospheric defense", "malformed quasar event was presented as authoritative")
	_check(EventPresenter.notification_for({"type": "combat.limpet.attached", "category": "combat", "data": {"target_ship_id": 61, "target_player_id": 9, "asset_owner_id": 4, "sector_id": 17}}) == "Combat · Limpet attached", "persisted limpet attachment event was not presented without exposing IDs")
	_check(EventPresenter.notification_for({"type": "combat.hit.environment", "category": "combat", "data": {"hazard_type": "minefield", "damage": "12", "destroyed": 1}}) == "Combat · Environmental hazard · minefield", "malformed hazard event was presented as authoritative")
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
