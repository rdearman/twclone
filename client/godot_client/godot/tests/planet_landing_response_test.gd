extends SceneTree

const MainScene = preload("res://Main.tscn")

class FakeRefresh extends RefCounted:
	var calls: Array = []

	func refresh(_force: bool = false, domains: Array = []) -> bool:
		calls.append(domains.duplicate())
		return true

class FakeRefreshUnavailable extends RefCounted:
	func refresh(_force: bool = false, _domains: Array = []) -> bool:
		return false

	func handle_disconnect() -> void:
		pass

var failures: Array[String] = []
var checks := 0

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	var app = MainScene.instantiate()
	root.add_child(app)
	await process_frame
	var fake_refresh := FakeRefresh.new()
	app.refresh_coordinator = fake_refresh
	# A refusal for a new landing must not erase an earlier server-confirmed
	# surface; it only must avoid claiming the new target.
	app._landed_planet_id = 66
	app.gameplay_view.enter_planet_workflow(66)
	# A refusal is not a landing. Reconcile every state domain that the
	# authoritative landing handler may have touched, without opening the
	# planet surface or inventing local state.
	await app._finish_command_reply({
		"status": "refused",
		"error": {"message": "Planetary defenses refused landing."},
	}, {
		"label": "Land on planet", "command": "planet.land",
		"data": {"planet_id": 77}, "mutating": true,
	})
	_check(fake_refresh.calls.size() == 1, "landing refusal did not refresh authoritative state")
	if fake_refresh.calls.size() == 1:
		_check(fake_refresh.calls[0] == ["player", "ship", "sector"], "landing refusal refreshed the wrong domains")
	_check(app._landed_planet_id == 66, "landing refusal erased the prior confirmed planet")
	_check(app.gameplay_view.planet_workflow.visible, "landing refusal hid the prior confirmed planet surface")
	_check(app.gameplay_view.notification_label.text.contains("Planetary defenses refused landing"), "landing refusal reason was not shown")

	# A stale pending success must not survive a later refusal and get consumed
	# by an unrelated refresh. A different already-confirmed surface remains
	# usable until the refusal reconciliation says otherwise.
	app._pending_landing_surface_id = 77
	app._landed_planet_id = 77
	app._landing_surface_ready = false
	fake_refresh.calls.clear()
	await app._finish_command_reply({
		"status": "refused",
		"error": {"message": "Landing is no longer permitted."},
	}, {
		"label": "Land on planet", "command": "planet.land",
		"data": {"planet_id": 77}, "mutating": true,
	})
	_check(app._pending_landing_surface_id == 0, "landing refusal retained a stale pending transition")
	_check(app._landed_planet_id == 0, "refused pending landing retained an unconfirmed planet")
	_check(not app._landing_surface_ready, "refused pending landing remained surface-ready")
	_check(fake_refresh.calls.size() == 1 and fake_refresh.calls[0] == ["player", "ship", "sector"], "refused pending landing did not refresh all affected state")

	# Refusal envelopes may contain diagnostic data, but that data cannot grant
	# landing access or replace the previously confirmed surface context.
	fake_refresh.calls.clear()
	await app._finish_command_reply({
		"status": "refused",
		"error": {"message": "Planetary defenses refused landing.", "data": {"planet_id": 77, "landing_allowed": true}},
	}, {
		"label": "Land on planet", "command": "planet.land",
		"data": {"planet_id": 77}, "mutating": true,
	})
	_check(fake_refresh.calls.size() == 1, "diagnostic landing refusal did not refresh authoritative state")
	if fake_refresh.calls.size() == 1:
		_check(fake_refresh.calls[0] == ["player", "ship", "sector"], "diagnostic landing refusal refreshed the wrong domains")
	_check(app._landed_planet_id == 66, "diagnostic landing refusal replaced the confirmed planet")
	_check(app.gameplay_view.planet_workflow.visible, "diagnostic landing refusal hid the confirmed surface")
	_check(app.gameplay_view.notification_label.text.contains("Planetary defenses refused landing"), "diagnostic landing refusal reason was not shown")
	app._landed_planet_id = 0
	app.gameplay_view.leave_planet_workflow()

	fake_refresh.calls.clear()
	await app._finish_command_reply({
		"status": "ok", "data": {"planet_id": 88},
	}, {
		"label": "Land on planet", "command": "planet.land",
		"data": {"planet_id": 77}, "mutating": true,
	})
	_check(fake_refresh.calls.size() == 1, "mismatched landing response did not refresh authoritative state")
	if fake_refresh.calls.size() == 1:
		_check(fake_refresh.calls[0] == ["player", "ship", "sector"], "mismatched landing refreshed the wrong domains")
	_check(app._landed_planet_id == 0, "mismatched landing response opened a different planet")
	_check(app.gameplay_view.notification_label.text.contains("returned a different planet ID"), "mismatched landing response was not explained")

	fake_refresh.calls.clear()
	await app._finish_command_reply({
		"status": "ok", "data": {"message": "Landed"},
	}, {
		"label": "Land on planet", "command": "planet.land",
		"data": {"planet_id": 77}, "mutating": true,
	})
	_check(fake_refresh.calls.size() == 1, "landing response without an ID did not refresh authoritative state")
	if fake_refresh.calls.size() == 1:
		_check(fake_refresh.calls[0] == ["player", "ship", "sector"], "missing landing ID refreshed the wrong domains")
	_check(app._landed_planet_id == 0, "landing response without an ID opened a planet surface")
	_check(app.gameplay_view.notification_label.text.contains("omitted the planet ID"), "missing landing ID was not explained")

	# A successful envelope with a contradictory response type is not a
	# confirmed landing. Reconcile affected state, but do not open the surface.
	fake_refresh.calls.clear()
	await app._finish_command_reply({
		"status": "ok", "type": "planet.info", "data": {"planet_id": 77},
	}, {
		"label": "Land on planet", "command": "planet.land",
		"data": {"planet_id": 77}, "mutating": true,
	})
	_check(app._landed_planet_id == 0, "unexpected landing response type opened a planet surface")
	_check(fake_refresh.calls.size() == 1, "unexpected landing response type did not refresh authoritative state")
	if fake_refresh.calls.size() == 1:
		_check(fake_refresh.calls[0] == ["player", "ship", "sector"], "unexpected landing response type refreshed the wrong domains")
	_check(app.gameplay_view.notification_label.text.contains("unexpected response type"), "unexpected landing response type was not explained")

	fake_refresh.calls.clear()
	await app._finish_command_reply({
		"status": "ok", "data": {"planet_id": true},
	}, {
		"label": "Land on planet", "command": "planet.land",
		"data": {"planet_id": 1}, "mutating": true,
	})
	_check(fake_refresh.calls.size() == 1, "boolean landing ID did not refresh authoritative state")
	if fake_refresh.calls.size() == 1:
		_check(fake_refresh.calls[0] == ["player", "ship", "sector"], "boolean landing ID refreshed the wrong domains")
	_check(app._landed_planet_id == 0, "boolean landing ID was coerced into a real planet ID")
	_check(app.gameplay_view.notification_label.text.contains("omitted the planet ID"), "boolean landing ID was not explained")

	fake_refresh.calls.clear()
	await app._finish_command_reply({
		"status": "ok", "data": {"planet_id": 77.0},
	}, {
		"label": "Land on planet", "command": "planet.land",
		"data": {"planet_id": 77}, "mutating": true,
	})
	_check(fake_refresh.calls.size() == 1, "floating-point landing ID did not refresh authoritative state")
	if fake_refresh.calls.size() == 1:
		_check(fake_refresh.calls[0] == ["player", "ship", "sector"], "floating-point landing ID refreshed the wrong domains")
	_check(app._landed_planet_id == 0, "floating-point landing ID was coerced into a real planet ID")
	_check(app.gameplay_view.notification_label.text.contains("omitted the planet ID"), "floating-point landing ID was not explained")

	# A malformed JSON boolean must not be coerced to planet ID 1 by GDScript.
	fake_refresh.calls.clear()
	var action_count_before: int = app.gameplay_view.context_action_list.get_child_count()
	app.gameplay_view._add_object_actions("planet", {"data": {"planet_id": true}})
	_check(app.gameplay_view.context_action_list.get_child_count() == action_count_before + 2, "malformed planet metadata did not render both unavailable actions")
	if app.gameplay_view.context_action_list.get_child_count() == action_count_before + 2:
		for index in range(action_count_before, action_count_before + 2):
			var unavailable_action: Button = app.gameplay_view.context_action_list.get_child(index)
			_check(unavailable_action.disabled and unavailable_action.has_meta("unavailable_reason"), "malformed planet metadata exposed an actionable button")
	_check(fake_refresh.calls.is_empty(), "malformed planet metadata triggered a refresh")

	fake_refresh.calls.clear()
	await app._finish_command_reply({
		"status": "ok", "data": {"planet_id": 77},
	}, {
		"label": "Land on planet", "command": "planet.land",
		"data": {"planet_id": 77}, "mutating": true,
	})
	_check(fake_refresh.calls.size() == 1, "confirmed landing did not refresh authoritative player and ship state")
	if fake_refresh.calls.size() == 1:
		_check(fake_refresh.calls[0] == ["player", "ship"], "confirmed landing refreshed unrelated domains")
	_check(app._landed_planet_id == 77, "confirmed landing did not retain the authoritative planet ID")
	_check(not app._landing_surface_ready, "landing became surface-ready before the authoritative refresh")
	_check(not app.gameplay_view.planet_workflow.visible, "planet workflow opened before authoritative refresh completed")
	_check(app.gameplay_view.notification_label.text.contains("refreshing player and ship"), "confirmed landing refresh state was not presented to the player")
	await app._on_refresh_finished({
		"freshness": {"player": "fresh", "ship": "fresh"},
		"ship": {"onplanet": 77},
	})
	_check(app.gameplay_view.planet_workflow.visible, "confirmed landing did not open the planet workflow after refresh")
	_check(app._landing_surface_ready, "successful landing refresh did not enable surface reads")
	_check(app.gameplay_view.notification_label.text.contains("Landing confirmed"), "confirmed landing was not presented after refresh")

	# If a new landing is confirmed but its player/ship reconciliation fails,
	# any previously visible surface must also be closed. Otherwise stale planet
	# controls remain actionable while the new landing outcome is unresolved.
	app.gameplay_view.enter_planet_workflow(66)
	app._landed_planet_id = 77
	app._pending_landing_surface_id = 77
	app._command_refresh_pending = true
	await app._on_refresh_failed({"freshness": {"player": "partial", "ship": "unavailable"}})
	_check(not app.gameplay_view.planet_workflow.visible, "failed landing refresh left stale planet controls visible")
	_check(app._landed_planet_id == 0 and not app._landing_surface_ready, "failed landing refresh retained stale landing state")

	# A malformed refresh envelope must not raise a typed GDScript error or
	# expose the requested surface.  The landing remains unavailable until a
	# later authoritative refresh supplies dictionary-shaped player/ship data.
	var malformed_app = MainScene.instantiate()
	root.add_child(malformed_app)
	await process_frame
	malformed_app._command_refresh_pending = true
	malformed_app._pending_landing_surface_id = 77
	malformed_app._landed_planet_id = 77
	await malformed_app._on_refresh_finished({"freshness": "invalid", "ship": []})
	_check(malformed_app._pending_landing_surface_id == 0, "malformed landing refresh retained a pending surface transition")
	_check(not malformed_app._landing_surface_ready, "malformed landing refresh enabled planet actions")
	_check(not malformed_app.gameplay_view.planet_workflow.visible, "malformed landing refresh exposed the planet surface")
	_check(malformed_app.gameplay_view.notification_label.text.contains("refresh was incomplete"), "malformed landing refresh was not explained")
	malformed_app.queue_free()
	await process_frame

	# A different authoritative planet ID must not make the requested surface
	# actionable, even when player and ship freshness are both reported.
	app._landed_planet_id = 77
	app._pending_landing_surface_id = 77
	app._landing_surface_ready = false
	app.gameplay_view.leave_planet_workflow()
	fake_refresh.calls.clear()
	await app._on_refresh_finished({
		"freshness": {"player": "fresh", "ship": "fresh"},
		"ship": {"onplanet": 88},
	})
	_check(not app._landing_surface_ready, "refresh for another planet opened the requested surface")
	_check(not app.gameplay_view.planet_workflow.visible, "contradictory landed planet ID left surface actions visible")

	# A post-landing planet read can be refused if the location/ownership state
	# changed. Do not keep presenting the stale surface without reconciliation.
	fake_refresh.calls.clear()
	app._pending_game_commands["planet-info-refused"] = {
		"kind": "planet_info", "command": "planet.info", "label": "Planet information",
	}
	app._on_transport_reply("planet-info-refused", {
		"status": "refused", "error": {"message": "Planet is no longer available."},
	}, "planet.info")
	_check(fake_refresh.calls.size() == 1, "refused planet read did not refresh authoritative state")
	if fake_refresh.calls.size() == 1:
		_check(fake_refresh.calls[0] == ["player", "ship", "sector"], "refused planet read refreshed the wrong state domains")
	_check(app.gameplay_view.notification_label.text.contains("Planet is no longer available"), "planet read refusal reason was not shown")
	_check(app._landed_planet_id == 0, "refused planet read retained stale landed-planet context")
	_check(not app._landing_surface_ready and not app.gameplay_view.planet_workflow.visible, "refused planet read left stale surface actions visible")

	fake_refresh.calls.clear()
	app._command_refresh_pending = false
	app._pending_game_commands["planet-info-timeout"] = {
		"kind": "planet_info", "command": "planet.info", "label": "Planet information",
	}
	app._on_request_timed_out("planet-info-timeout", "planet.info")
	_check(fake_refresh.calls.size() == 1, "timed-out planet read did not refresh authoritative state")
	if fake_refresh.calls.size() == 1:
		_check(fake_refresh.calls[0] == ["player", "ship", "sector"], "timed-out planet read refreshed the wrong state domains")
	_check(app.gameplay_view.notification_label.text.contains("Refreshing confirmed player, ship and sector state"), "planet read timeout was not explained")
	_check(app._landed_planet_id == 0, "timed-out planet read retained stale landed-planet context")
	_check(not app._landing_surface_ready and not app.gameplay_view.planet_workflow.visible, "timed-out planet read left stale surface actions visible")

	# Start the uncertain-reply case from the normal sector view; the timeout
	# itself must not create or retain a local landing claim.
	app._landed_planet_id = 0
	app.gameplay_view.leave_planet_workflow()
	fake_refresh.calls.clear()
	app._pending_game_commands["landing-timeout"] = {
		"kind": "command", "command": "planet.land", "label": "Land on planet",
		"data": {"planet_id": 77}, "mutating": true,
	}
	app._on_request_timed_out("landing-timeout", "planet.land")
	_check(fake_refresh.calls.size() == 1, "uncertain landing timeout did not refresh authoritative state")
	if fake_refresh.calls.size() == 1:
		_check(fake_refresh.calls[0] == ["player", "ship", "sector"], "landing timeout refreshed the wrong domains")
	_check(app._landed_planet_id == 0, "uncertain landing timeout opened a planet surface")
	_check(app.gameplay_view.notification_label.text.contains("outcome is uncertain"), "landing timeout was not presented as uncertain")

	# A confirmed landing must not open the surface when the required
	# player/ship refresh fails. Clear the pending transition so a later
	# unrelated refresh cannot open a stale planet workflow.
	app._landed_planet_id = 77
	app._pending_landing_surface_id = 77
	app._command_refresh_pending = true
	app.gameplay_view.leave_planet_workflow()
	app._on_refresh_failed({"freshness": {"player": "stale", "ship": "stale"}})
	_check(app._pending_landing_surface_id == 0, "failed landing refresh left a pending surface transition")
	_check(app._landed_planet_id == 0, "failed landing refresh retained an unconfirmed planet ID")
	_check(not app._landing_surface_ready, "failed landing refresh left stale surface reads enabled")
	_check(not app.gameplay_view.planet_workflow.visible, "failed landing refresh opened a stale planet surface")
	_check(app.gameplay_view.notification_label.text.contains("refresh was incomplete"), "failed landing refresh was not explained")

	# If the refresh cannot start at all, a later unrelated refresh must not
	# consume the stale pending landing transition.
	app._landed_planet_id = 77
	app._pending_landing_surface_id = 77
	app._landing_surface_ready = false
	app.gameplay_view.leave_planet_workflow()
	app.refresh_coordinator = FakeRefreshUnavailable.new()
	app._refresh_after_command(["player", "ship"])
	_check(app._pending_landing_surface_id == 0, "failed-to-start landing refresh left a pending surface transition")
	_check(app._landed_planet_id == 0, "failed-to-start landing refresh retained the landed planet")
	_check(not app.gameplay_view.planet_workflow.visible, "failed-to-start landing refresh exposed stale planet actions")
	_check(app.gameplay_view.notification_label.text.contains("refresh could not start"), "failed-to-start landing refresh was not explained")

	# Surface reads are requested as a pair. A refusal must still reconcile after
	# the companion read succeeds; otherwise stale player/ship/sector state can
	# remain actionable on the open planet surface.
	app._landed_planet_id = 77
	app._planet_pending_count = 2
	app._planet_read_refused = false
	fake_refresh.calls.clear()
	app._pending_game_commands["planet-info-mixed"] = {
		"kind": "planet_info", "command": "planet.info", "label": "Planet information",
	}
	await app._on_transport_reply("planet-info-mixed", {
		"status": "refused", "error": {"message": "Planet details are unavailable."},
	}, "planet.info")
	_check(fake_refresh.calls.is_empty(), "mixed surface read refreshed before the companion read completed")
	app._pending_game_commands["planet-colonists-mixed"] = {
		"kind": "planet_colonists", "command": "planet.colonists.get", "label": "Colony information",
	}
	await app._on_transport_reply("planet-colonists-mixed", {
		"status": "ok", "data": {"planet_colonists": 3},
	}, "planet.colonists.get")
	_check(fake_refresh.calls.size() == 1, "mixed surface read did not reconcile after a companion refusal")
	if fake_refresh.calls.size() == 1:
		_check(fake_refresh.calls[0] == ["player", "ship", "sector"], "mixed surface read refreshed the wrong domains")
	_check(app._landed_planet_id == 0, "mixed surface read retained stale landed-planet context")
	_check(not app._landing_surface_ready and not app.gameplay_view.planet_workflow.visible, "mixed surface read left stale surface actions visible")

	# A successful planet.info envelope must still identify the confirmed landed
	# planet before its fields become displayable or actionable.
	app._landed_planet_id = 77
	app._landing_surface_ready = true
	app._planet_pending_count = 1
	app.gameplay_view.enter_planet_workflow(77)
	app._pending_game_commands["planet-info-mismatch"] = {
		"kind": "planet_info", "command": "planet.info", "label": "Planet information",
	}
	fake_refresh.calls.clear()
	await app._on_transport_reply("planet-info-mismatch", {
		"status": "ok", "data": {"planet_id": 88, "name": "Untrusted planet"},
	}, "planet.info")
	_check(app._landed_planet_id == 0, "mismatched planet.info retained the landed planet identity")
	_check(not app.gameplay_view.planet_workflow.visible, "mismatched planet.info left stale surface actions visible")
	_check(fake_refresh.calls.size() == 1, "mismatched planet.info did not reconcile authoritative state")
	_check(not app.gameplay_view.notification_label.text.contains("Untrusted planet"), "mismatched planet.info displayed untrusted fields")

	# Losing transport during the post-landing reconciliation must retire the
	# unconfirmed transition; reconnect refreshes must not open the stale target.
	app.refresh_coordinator = FakeRefreshUnavailable.new()
	app._landed_planet_id = 77
	app._pending_landing_surface_id = 77
	app._landing_surface_ready = false
	app.gameplay_view.enter_planet_workflow(77)
	app._on_transport_disconnected("socket closed")
	_check(app._pending_landing_surface_id == 0, "disconnect retained an unconfirmed landing transition")
	_check(app._landed_planet_id == 0, "disconnect retained an unconfirmed landed planet")
	_check(not app._landing_surface_ready, "disconnect left unconfirmed planet actions enabled")
	_check(not app.gameplay_view.planet_workflow.visible, "disconnect left an unconfirmed planet surface visible")

	app.queue_free()
	await process_frame
	if failures.is_empty():
		print("Godot planet landing response tests: %d checks passed" % checks)
		quit(0)
	else:
		for failure in failures:
			push_error(failure)
		quit(1)

func _check(condition: bool, message: String) -> void:
	checks += 1
	if not condition:
		failures.append(message)
