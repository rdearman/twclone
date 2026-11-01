extends SceneTree

const MainScene = preload("res://Main.tscn")

class FakeTransport extends RefCounted:
	var calls: Array[Dictionary] = []
	var next_id := 1

	func request(command: String, data: Dictionary, session_token: String = "") -> String:
		calls.append({"command": command, "data": data, "session_token": session_token})
		var request_id := "test-%d" % next_id
		next_id += 1
		return request_id

	func poll(_delta: float = 0.0) -> void:
		pass

	func drain_events() -> Array:
		return []

	func close(_reason: String = "Client disconnected.") -> void:
		pass

class FakeRefresh extends RefCounted:
	var calls: Array = []

	func accepts_request(_request_id: String) -> bool:
		return false

	func refresh(_force: bool = false, _domains: Array = []) -> bool:
		calls.append({"force": _force, "domains": _domains})
		return true

var failures: Array[String] = []
var checks := 0

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	var app = MainScene.instantiate()
	root.add_child(app)
	await process_frame
	app.auth_session.handle_response({
		"status": "ok", "type": "auth.session",
		"data": {"session": "test-session", "player_id": 3},
	})
	var fake_transport := FakeTransport.new()
	app.transport = fake_transport
	app.refresh_coordinator = FakeRefresh.new()
	app._landed_planet_id = 77
	app.gameplay_view.enter_planet_workflow(77)
	fake_transport.calls.clear()
	app.refresh_coordinator.calls.clear()

	await app._finish_command_reply(
		{"status": "refused", "error": {"message": "Permission denied."}},
		{"label": "Land on planet", "command": "planet.land", "mutating": true},
	)
	_check(app.gameplay_view.notification_label.text.contains("Permission denied"), "landing refusal was not shown")
	_check(app._landed_planet_id == 77, "landing refusal changed the confirmed landed-planet context")
	_check(app.refresh_coordinator.calls.size() == 1, "landing refusal did not refresh authoritative state")
	if app.refresh_coordinator.calls.size() == 1:
		_check(app.refresh_coordinator.calls[0]["domains"] == ["player", "ship", "sector"], "landing refusal refreshed the wrong state domains")

	app.refresh_coordinator.calls.clear()
	app._pending_game_commands["landing-timeout"] = {
		"kind": "command", "command": "planet.land", "label": "Land on planet",
		"data": {"planet_id": 77}, "mutating": true,
	}
	app._on_request_failed("landing-timeout", "planet.land", {"kind": "timeout", "message": "request timed out"})
	_check(app.gameplay_view.notification_label.text.contains("Landing outcome is uncertain"), "landing timeout did not explain the uncertain outcome")
	_check(not app._pending_game_commands.has("landing-timeout"), "landing timeout remained pending")
	_check(app.refresh_coordinator.calls.size() == 1, "landing timeout did not refresh authoritative state")
	_check(app._landed_planet_id == 0, "landing timeout retained the previously confirmed planet identity")
	_check(not app.gameplay_view.planet_workflow.visible, "landing timeout left stale planet actions visible")
	if app.refresh_coordinator.calls.size() == 1:
		_check(app.refresh_coordinator.calls[0]["domains"] == ["player", "ship", "sector"], "landing timeout refreshed the wrong state domains")

	app.refresh_coordinator.calls.clear()
	app._pending_game_commands["landing-timeout-2"] = {
		"kind": "command", "command": "planet.land", "label": "Land on planet",
		"data": {"planet_id": 77}, "mutating": true,
	}
	app._on_request_timed_out("landing-timeout-2", "planet.land")
	_check(app.gameplay_view.notification_label.text.contains("Landing outcome is uncertain"), "landing timeout did not explain the uncertain outcome")
	_check(not app._pending_game_commands.has("landing-timeout-2"), "landing timeout remained pending")
	_check(app.refresh_coordinator.calls.size() == 1, "landing timeout path did not refresh authoritative state")
	if app.refresh_coordinator.calls.size() == 1:
		_check(app.refresh_coordinator.calls[0]["domains"] == ["player", "ship", "sector"], "landing timeout path refreshed the wrong state domains")

	app.refresh_coordinator.calls.clear()
	app._pending_game_commands["landing-disconnect"] = {
		"kind": "command", "command": "planet.land", "label": "Land on planet",
		"data": {"planet_id": 77}, "mutating": true,
	}
	app._on_request_failed("landing-disconnect", "planet.land", {
		"kind": "disconnected", "message": "Server connection lost.",
	})
	_check(app.gameplay_view.notification_label.text.contains("Landing outcome is uncertain"), "landing transport failure did not explain the uncertain outcome")
	_check(not app._pending_game_commands.has("landing-disconnect"), "landing transport failure remained pending")
	_check(app._landed_planet_id == 0, "landing transport failure retained the previously confirmed planet identity")
	_check(not app.gameplay_view.planet_workflow.visible, "landing transport failure left stale planet actions visible")
	_check(app.refresh_coordinator.calls.size() == 1, "landing transport failure did not refresh authoritative state")
	if app.refresh_coordinator.calls.size() == 1:
		_check(app.refresh_coordinator.calls[0]["domains"] == ["player", "ship", "sector"], "landing transport failure refreshed the wrong state domains")

	# An explicit refusal is different from an uncertain reply: retain the
	# previously confirmed surface while reconciling the refusal.
	app._landed_planet_id = 77
	app.gameplay_view.enter_planet_workflow(77)
	app.refresh_coordinator.calls.clear()
	await app._finish_command_reply(
		{"status": "error", "error": {"message": "Planetary defenses engaged your ship; landing refused.", "data": {
			"planet_id": 77,
			"landing_allowed": false,
			"ship_remains_in_sector": true,
			"combat": [{"defense": "atmosphere_quasar", "damage": 25, "shields_lost": 10, "hull_lost": 15, "shields_remaining": 0, "hull_remaining": 85, "destroyed": false}],
		}}},
		{"label": "Land on planet", "command": "planet.land", "mutating": true, "data": {"planet_id": 77}},
	)
	_check(app.gameplay_view.notification_label.text.contains("Atmospheric Quasar"), "landing error combat details were not presented from the server response")
	_check(app._landed_planet_id == 77, "landing error changed the previously confirmed planet context")
	_check(app.refresh_coordinator.calls.size() == 1, "landing error did not refresh authoritative state")
	if app.refresh_coordinator.calls.size() == 1:
		_check(app.refresh_coordinator.calls[0]["domains"] == ["player", "ship", "sector"], "landing error refreshed the wrong state domains")

	app.refresh_coordinator.calls.clear()
	await app._finish_command_reply(
		{"status": "refused", "error": {"message": "Planetary defenses repelled the attack.", "data": {
			"planet_id": 77,
			"ship_remains_in_sector": true,
			"combat": [{"result": "repelled", "attacker_fighters_lost": 8, "attacker_fighters_remaining": 2,
				"planet_fighters_lost": 4, "planet_fighters_remaining": 16,
				"citadel_shields_absorbed": 12, "citadel_shields_remaining": 0}],
		}}},
		{"label": "Attack planet", "command": "combat.attack_planet", "mutating": true, "data": {"planet_id": 77}},
	)
	_check(app.gameplay_view.notification_label.text.contains("Planetary fighters repelled"), "planet attack refusal hid the authoritative defense report")
	_check(app.gameplay_view.notification_label.text.contains("8 fighters"), "planet attack refusal hid fighter depletion")
	_check(app.refresh_coordinator.calls.size() == 1, "planet attack refusal did not refresh authoritative state")
	if app.refresh_coordinator.calls.size() == 1:
		_check(app.refresh_coordinator.calls[0]["domains"] == ["player", "ship", "sector"], "planet attack refusal refreshed the wrong state domains")

	app.refresh_coordinator.calls.clear()
	await app._finish_command_reply(
		{"status": "ok", "type": "combat.attack_planet", "data": {
			"planet_id": 88, "attacker_remaining_fighters": 12,
			"defender_remaining_fighters": 0, "captured": true,
		}},
		{"label": "Attack planet", "command": "combat.attack_planet", "mutating": true, "data": {"planet_id": 77}},
	)
	_check(app.gameplay_view.notification_label.text.contains("no matching planet ID"), "planet attack accepted a response for a different planet")
	_check(app.refresh_coordinator.calls.size() == 1, "mismatched planet attack result did not refresh authoritative state")
	if app.refresh_coordinator.calls.size() == 1:
		_check(app.refresh_coordinator.calls[0]["domains"] == ["player", "ship", "sector"], "mismatched planet attack refreshed the wrong state domains")

	app.refresh_coordinator.calls.clear()
	await app._finish_command_reply(
		{"status": "ok", "type": "planet.info", "data": {
			"planet_id": 77, "attacker_remaining_fighters": 12,
			"defender_remaining_fighters": 0, "captured": true,
		}},
		{"label": "Attack planet", "command": "combat.attack_planet", "mutating": true, "data": {"planet_id": 77}},
	)
	_check(app.gameplay_view.notification_label.text.contains("unexpected response type"), "planet attack accepted a success envelope from another command")
	_check(app.refresh_coordinator.calls.size() == 1, "unexpected planet attack response did not refresh authoritative state")
	if app.refresh_coordinator.calls.size() == 1:
		_check(app.refresh_coordinator.calls[0]["domains"] == ["player", "ship", "sector"], "unexpected planet attack refreshed the wrong state domains")

	app.refresh_coordinator.calls.clear()
	await app._finish_command_reply(
		{"status": "ok", "data": {
			"planet_id": 77, "attacker_remaining_fighters": 12,
			"defender_remaining_fighters": 0, "captured": true,
		}},
		{"label": "Attack planet", "command": "combat.attack_planet", "mutating": true, "data": {"planet_id": 77}},
	)
	_check(app.gameplay_view.notification_label.text.contains("unexpected response type"), "planet attack without canonical type was displayed")
	_check(app.refresh_coordinator.calls.size() == 1, "missing planet attack response type did not refresh authoritative state")

	app.refresh_coordinator.calls.clear()
	await app._finish_command_reply(
		{"status": "ok", "type": "combat.attack_planet", "data": {
			"planet_id": 77, "attacker_remaining_fighters": 12,
			"defender_remaining_fighters": 0, "captured": true,
		}},
		{"label": "Attack planet", "command": "combat.attack_planet", "mutating": true, "data": {"planet_id": 77}},
	)
	_check(app.gameplay_view.notification_label.text.contains("captured: true"), "planet attack capture did not present the server-confirmed capture result")
	_check(app.refresh_coordinator.calls.size() == 1, "planet attack capture did not refresh authoritative ownership state")
	if app.refresh_coordinator.calls.size() == 1:
		_check(app.refresh_coordinator.calls[0]["domains"] == ["player", "ship", "sector"], "planet attack capture refreshed the wrong state domains")

	app.refresh_coordinator.calls.clear()
	await app._finish_command_reply(
		{"status": "ok", "type": "combat.attack_planet", "data": {
			"planet_id": 77, "attacker_remaining_fighters": 0,
			"defender_remaining_fighters": 4, "captured": false,
		}},
		{"label": "Attack planet", "command": "combat.attack_planet", "mutating": true, "data": {"planet_id": 77}},
	)
	_check(app.gameplay_view.notification_label.text.contains("captured: false"), "planet attack loss did not present the authoritative non-capture result")
	_check(app.refresh_coordinator.calls.size() == 1, "planet attack loss did not refresh authoritative state")

	app.refresh_coordinator.calls.clear()
	await app._finish_command_reply(
		{"status": "ok", "type": "combat.attack_planet", "data": {
			"planet_id": 77, "attacker_remaining_fighters": "0",
			"defender_remaining_fighters": 4, "captured": false,
		}},
		{"label": "Attack planet", "command": "combat.attack_planet", "mutating": true, "data": {"planet_id": 77}},
	)
	_check(app.gameplay_view.notification_label.text.contains("no authoritative loss/capture result"), "malformed planet loss/capture facts were presented")
	_check(app.refresh_coordinator.calls.size() == 1, "malformed planet result did not refresh authoritative state")

	app.refresh_coordinator.calls.clear()
	await app._finish_command_reply(
		{"status": "error", "error": {"message": "Entry hazard processing failed", "data": {
			"hazards": [{"hazard_type": "minefield", "hull_lost": 12, "destroyed": false, "ship_remains_in_sector": true}],
		}}},
		{"label": "Launch to sector", "command": "planet.launch", "mutating": true},
	)
	_check(app.gameplay_view.notification_label.text.contains("Entry hazard processing failed"), "launch refusal reason was not shown")
	_check(app.gameplay_view.information_text.text.contains("minefield"), "launch hazard details were not presented from the server response")
	_check(app._landed_planet_id == 77, "launch refusal changed the confirmed landed-planet context")
	_check(app.refresh_coordinator.calls.size() == 1, "launch refusal did not refresh authoritative state")
	if app.refresh_coordinator.calls.size() == 1:
		_check(app.refresh_coordinator.calls[0]["domains"] == ["player", "ship", "sector"], "launch refusal refreshed the wrong state domains")

	app.refresh_coordinator.calls.clear()
	await app._finish_command_reply(
		{"status": "ok", "type": "planet.launch.success", "data": {
			"sector_id": 12,
			"hazards": [{"hazard_type": "minefield", "hull_lost": 8, "destroyed": false, "ship_remains_in_sector": true}],
		}},
		{"label": "Launch to sector", "command": "planet.launch", "mutating": true},
	)
	_check(app.gameplay_view.information_text.text.contains("minefield"), "successful launch hazard details were not presented from the server response")
	_check(app._landed_planet_id == 0, "successful launch retained the landed-planet context")
	_check(app.refresh_coordinator.calls.size() == 1, "successful launch did not refresh authoritative state")
	if app.refresh_coordinator.calls.size() == 1:
		_check(app.refresh_coordinator.calls[0]["domains"] == ["player", "ship", "sector"], "successful launch refreshed the wrong state domains")

	await app._finish_command_reply(
		{"status": "refused", "error": {"message": "Insufficient equipment."}},
		{"label": "Build citadel", "command": "citadel.build", "mutating": true},
	)
	_check(app.gameplay_view.notification_label.text.contains("Insufficient equipment"), "citadel resource refusal was not shown")
	_check(fake_transport.calls.size() == 2, "refused construction did not refresh the confirmed planet records")
	fake_transport.calls.clear()

	await app._finish_command_reply(
		{"status": "refused", "error": {"message": "You do not control this planet."}},
		{"label": "Assign planet workers", "command": "planet.colonists.allocate", "mutating": true},
	)
	_check(app.gameplay_view.notification_label.text.contains("You do not control this planet"), "invalid-ownership refusal was not shown")
	_check(fake_transport.calls.size() == 2, "refused workforce allocation did not refresh the confirmed planet records")
	fake_transport.calls.clear()

	await app._finish_command_reply(
		{"status": "error", "error": {"message": "Database temporarily unavailable."}},
		{"label": "Deposit to planet", "command": "planet.deposit", "mutating": true},
	)
	_check(app.gameplay_view.notification_label.text.contains("Database temporarily unavailable"), "server error was not shown with its reason")
	_check(fake_transport.calls.size() == 2, "failed stock transfer did not refresh the confirmed planet records")
	fake_transport.calls.clear()

	await app._finish_command_reply(
		{"status": "refused", "error": {"message": "Planet harvest is unavailable."}},
		{"label": "Harvest planet production", "command": "planet.harvest", "mutating": true},
	)
	_check(app.gameplay_view.notification_label.text.contains("Planet harvest is unavailable"), "harvest refusal was not shown")
	_check(fake_transport.calls.size() == 2, "harvest refusal did not refresh the confirmed planet records")
	fake_transport.calls.clear()

	await app._finish_command_reply(
		{"status": "refused", "error": {"message": "Insufficient ship fighter capacity."}},
		{"label": "Planet fighter transfer", "command": "planet.fighters.transfer", "mutating": true},
	)
	_check(app.gameplay_view.notification_label.text.contains("Insufficient ship fighter capacity"), "fighter transfer capacity refusal was not shown")
	_check(fake_transport.calls.size() == 2, "rejected fighter transfer did not refresh the confirmed planet records")
	fake_transport.calls.clear()

	await app._finish_command_reply(
		{"status": "ok", "type": "citadel.upgrade_started", "data": {"target_level": 2, "days_to_complete": 4}},
		{"label": "Upgrade citadel", "command": "citadel.upgrade", "mutating": true},
	)
	_check(app.gameplay_view.notification_label.text.contains("Upgrade citadel complete"), "successful citadel mutation was not acknowledged")
	_check(app._landed_planet_id == 77, "successful citadel action lost landed-planet context")
	_check(fake_transport.calls.size() == 2, "successful planet mutation did not request fresh planet details")
	if fake_transport.calls.size() == 2:
		_check(fake_transport.calls[0]["command"] == "planet.info", "success refresh did not request authoritative planet information")
		_check(fake_transport.calls[1]["command"] == "planet.colonists.get", "success refresh did not request colonist/cargo information")
		_check(fake_transport.calls.all(func(call: Dictionary) -> bool: return not str(call["command"]).begins_with("citadel.")), "success refresh repeated the citadel mutation")

	app._landed_planet_id = 0
	fake_transport.calls.clear()
	app.refresh_coordinator.calls.clear()
	await app._finish_command_reply(
		{"status": "ok", "type": "planet.land.success", "data": {"planet_id": 77}},
		{"label": "Land on planet", "command": "planet.land", "mutating": true, "data": {"planet_id": 77}},
	)
	_check(app._landed_planet_id == 77, "successful landing did not retain the authoritative planet ID")
	_check(not app.gameplay_view.planet_workflow.visible, "successful landing opened the surface before refresh")

	_check(fake_transport.calls.size() == 2, "successful landing did not request both authoritative planet records")
	if fake_transport.calls.size() == 2:
		_check(fake_transport.calls[0]["command"] == "planet.info", "successful landing did not request planet information")
		_check(fake_transport.calls[1]["command"] == "planet.colonists.get", "successful landing did not request colonist/cargo information")
	_check(app.refresh_coordinator.calls.size() == 1, "successful landing did not refresh player, ship, and sector state")
	if app.refresh_coordinator.calls.size() == 1:
		_check(app.refresh_coordinator.calls[0]["domains"] == ["player", "ship", "sector"], "successful landing refreshed the wrong state domains")
	await app._on_refresh_finished({"freshness": {"player": "fresh", "ship": "fresh", "sector": "fresh"}, "player": {"id": 3}, "ship": {"id": 9, "onplanet": 0}, "sector": {"id": 20}})
	_check(not app.gameplay_view.planet_workflow.visible, "landing opened the surface while authoritative ship state remained off-planet")
	_check(app.gameplay_view.notification_label.text.contains("refresh was incomplete"), "off-planet refresh did not keep the surface unavailable")

	# A fresh ship record without the authoritative onplanet field cannot prove
	# that launch completed; do not infer departure from freshness alone.
	app._landed_planet_id = 77
	app._landing_surface_ready = true
	app.gameplay_view.planet_workflow.visible = true
	app._pending_launch_departure = true
	app.gameplay_view.set_command_busy(true)
	await app._finish_pending_launch_departure({
		"freshness": {"player": "fresh", "ship": "fresh", "sector": "fresh"},
		"ship": {"id": 9},
	})
	_check(app._landed_planet_id == 77, "launch refresh without onplanet incorrectly cleared the confirmed planet")
	_check(app.gameplay_view.planet_workflow.visible, "launch refresh without onplanet closed the surface")
	_check(app.gameplay_view.notification_label.text.contains("Launch state was incomplete"), "missing onplanet was not surfaced as incomplete launch state")
	app._pending_landing_surface_id = 77
	app._command_refresh_pending = true
	await app._on_refresh_finished({"freshness": {"player": "fresh", "ship": "fresh", "sector": "fresh"}, "player": {"id": 3}, "ship": {"id": 9, "onplanet": 1}, "sector": {"id": 20}})
	_check(app.gameplay_view.planet_workflow.visible, "successful landing did not open the surface after authoritative on-planet refresh")

	# A JSON-null response type is equivalent to an omitted optional type. The
	# Python client already accepts this legacy-compatible envelope; Godot must
	# not stringify null into a type mismatch.
	app._landed_planet_id = 0
	fake_transport.calls.clear()
	app.refresh_coordinator.calls.clear()
	await app._finish_command_reply(
		{"status": "ok", "type": null, "data": {"planet_id": 77}},
		{"label": "Land on planet", "command": "planet.land", "mutating": true, "data": {"planet_id": 77}},
	)
	_check(app._landed_planet_id == 77, "JSON-null landing response type was treated as a mismatch")
	_check(app.refresh_coordinator.calls.size() == 1, "JSON-null landing response did not refresh authoritative state")
	if app.refresh_coordinator.calls.size() == 1:
		_check(app.refresh_coordinator.calls[0]["domains"] == ["player", "ship", "sector"], "JSON-null landing response refreshed the wrong state domains")

	app._landed_planet_id = 0
	fake_transport.calls.clear()
	app.refresh_coordinator.calls.clear()
	await app._finish_command_reply(
		{"status": "ok", "data": {"planet_id": 88}},
		{"label": "Land on planet", "command": "planet.land", "mutating": true, "data": {"planet_id": 77}},
	)
	_check(app._landed_planet_id == 0, "landing opened a surface for a mismatched authoritative planet ID")
	_check(app.gameplay_view.notification_label.text.contains("different planet ID"), "mismatched landing ID was not explained")
	_check(app.refresh_coordinator.calls.size() == 1, "mismatched landing ID did not refresh authoritative state")
	if app.refresh_coordinator.calls.size() == 1:
		_check(app.refresh_coordinator.calls[0]["domains"] == ["player", "ship", "sector"], "mismatched landing refreshed the wrong state domains")

	fake_transport.calls.clear()
	app.refresh_coordinator.calls.clear()
	await app._finish_command_reply(
		{"status": "ok", "data": {"message": "Landed"}},
		{"label": "Land on planet", "command": "planet.land", "mutating": true, "data": {"planet_id": 77}},
	)
	_check(app._landed_planet_id == 0, "landing opened a surface when the authoritative planet ID was missing")
	_check(app.gameplay_view.notification_label.text.contains("omitted the planet ID"), "missing landing ID was not explained")
	_check(app.refresh_coordinator.calls.size() == 1, "missing landing ID did not refresh authoritative state")

	# An ambiguous success must also close a previously confirmed surface. The
	# follow-up refresh may show that the server consumed the turn or moved the
	# ship, but the malformed envelope cannot authorize stale planet actions.
	app._landed_planet_id = 66
	app._landing_surface_ready = true
	app.gameplay_view.enter_planet_workflow(66)
	fake_transport.calls.clear()
	app.refresh_coordinator.calls.clear()
	await app._finish_command_reply(
		{"status": "ok", "type": "planet.info", "data": {"planet_id": 77}},
		{"label": "Land on planet", "command": "planet.land", "mutating": true, "data": {"planet_id": 77}},
	)
	_check(app._landed_planet_id == 0, "ambiguous landing success retained the previous planet context")
	_check(not app._landing_surface_ready and not app.gameplay_view.planet_workflow.visible, "ambiguous landing success left stale surface actions visible")
	_check(app.refresh_coordinator.calls.size() == 1, "ambiguous landing success did not refresh authoritative state")

	# An explicitly malformed top-level ID must not fall through to a nested
	# legacy alias and authorize a different planet surface record.
	app._landed_planet_id = 77
	app._landing_surface_ready = true
	app.gameplay_view.enter_planet_workflow(77)
	app.refresh_coordinator.calls.clear()
	app._pending_game_commands["planet-info-malformed-id"] = {
		"kind": "planet_info", "command": "planet.info", "label": "Planet information",
	}
	app._on_transport_reply("planet-info-malformed-id", {
		"status": "ok", "data": {"planet_id": true, "planet": {"planet_id": 77}},
	}, "planet.info")
	_check(app._landed_planet_id == 0, "malformed explicit planet ID fell through to a nested alias")
	_check(not app._landing_surface_ready and not app.gameplay_view.planet_workflow.visible, "malformed explicit planet ID left stale surface actions visible")
	_check(app.refresh_coordinator.calls.size() == 1, "malformed explicit planet ID did not refresh authoritative state")

	# Colony records are also location-sensitive. A successful response for a
	# different planet must not populate the confirmed surface workflow.
	app._landed_planet_id = 77
	app._landing_surface_ready = true
	app.gameplay_view.enter_planet_workflow(77)
	app.refresh_coordinator.calls.clear()
	app._pending_game_commands["planet-colonists-mismatch"] = {
		"kind": "planet_colonists", "command": "planet.colonists.get", "label": "Colony and cargo information",
	}
	app._on_transport_reply("planet-colonists-mismatch", {
		"status": "ok", "data": {"planet_id": 88, "planet_colonists": 4, "ship_colonists": 2},
	}, "planet.colonists.get")
	_check(app._landed_planet_id == 0, "mismatched colony response retained stale landed-planet context")
	_check(not app._landing_surface_ready and not app.gameplay_view.planet_workflow.visible, "mismatched colony response left stale surface actions visible")
	_check(app.refresh_coordinator.calls.size() == 1, "mismatched colony response did not refresh authoritative state")
	if app.refresh_coordinator.calls.size() == 1:
		_check(app.refresh_coordinator.calls[0]["domains"] == ["player", "ship", "sector"], "mismatched colony response refreshed the wrong state domains")
	_check(app.gameplay_view.notification_label.text.contains("colony was not displayed"), "mismatched colony response was not explained")

	# A lost planetary attack reply cannot establish whether ownership changed or
	# fighters were depleted. Reconcile all server-owned domains before retrying.
	app.refresh_coordinator.calls.clear()
	app._pending_game_commands["planet-attack-timeout"] = {
		"kind": "command", "command": "combat.attack_planet", "label": "Attack planet",
		"data": {"planet_id": 77}, "mutating": true,
	}
	app._on_request_timed_out("planet-attack-timeout", "combat.attack_planet")
	_check(app.gameplay_view.notification_label.text.contains("Planet attack outcome is uncertain"), "planet attack timeout was not explained as uncertain")
	_check(app.refresh_coordinator.calls.size() == 1, "planet attack timeout did not refresh authoritative state")
	if app.refresh_coordinator.calls.size() == 1:
		_check(app.refresh_coordinator.calls[0]["domains"] == ["player", "ship", "sector"], "planet attack timeout refreshed the wrong state domains")

	app.refresh_coordinator.calls.clear()
	app._pending_game_commands["planet-attack-failure"] = {
		"kind": "command", "command": "combat.attack_planet", "label": "Attack planet",
		"data": {"planet_id": 77}, "mutating": true,
	}
	app._on_request_failed("planet-attack-failure", "combat.attack_planet", {"kind": "disconnected", "message": "Server connection lost."})
	_check(app.gameplay_view.notification_label.text.contains("Planet attack outcome is uncertain"), "planet attack failure was not explained as uncertain")
	_check(app.refresh_coordinator.calls.size() == 1, "planet attack failure did not refresh authoritative state")
	if app.refresh_coordinator.calls.size() == 1:
		_check(app.refresh_coordinator.calls[0]["domains"] == ["player", "ship", "sector"], "planet attack failure refreshed the wrong state domains")

	app.queue_free()
	await process_frame
	if failures.is_empty():
		print("Godot planet command response tests: %d checks passed" % checks)
		quit(0)
	else:
		for failure in failures:
			push_error(failure)
		quit(1)

func _check(condition: bool, message: String) -> void:
	checks += 1
	if not condition:
		failures.append(message)
