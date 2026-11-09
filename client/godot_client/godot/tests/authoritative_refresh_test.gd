extends SceneTree

const ClientState = preload("res://ClientState.gd")
const Refresh = preload("res://AuthoritativeRefresh.gd")

class FakeTransport extends RefCounted:
	var calls: Array[Dictionary] = []
	var next_id := 1

	func request(command: String, _data: Dictionary, _token: String) -> String:
		var request_id := "refresh-%d" % next_id
		next_id += 1
		calls.append({"id": request_id, "command": command})
		return request_id

class FakeAuth extends RefCounted:
	var player_id := 0
	var session_token := "test-session"

	func is_authenticated() -> bool:
		return true

var failures: Array[String] = []
var finished := 0
var failed := 0

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	var transport := FakeTransport.new()
	var state := ClientState.new()
	state.mark_authenticated()
	var refresh := Refresh.new(transport, FakeAuth.new(), state)
	refresh.refresh_finished.connect(func(_snapshot: Dictionary) -> void: finished += 1)
	refresh.refresh_failed.connect(func(_snapshot: Dictionary) -> void: failed += 1)

	_check(refresh.refresh(false, ["player", "ship", "sector"]), "refresh did not start")
	var player_id: String = transport.calls[0]["id"]
	refresh.handle_reply(player_id, {
		"status": "ok", "type": "player.info",
		"data": {"player": {"id": 3, "ship_id": 44, "sector": 17}},
	}, "player.my_info")
	var ship_id: String = transport.calls[1]["id"]
	refresh.handle_reply(ship_id, {
		"status": "ok", "type": "ship.status",
		"data": {"ship": {"id": 45, "name": "Delayed ship"}},
	}, "ship.status")
	var sector_id: String = transport.calls[2]["id"]
	refresh.handle_reply(sector_id, {
		"status": "ok", "type": "sector.info",
		"data": {"sector_id": 17, "name": "Test Sector"},
	}, "sector.info")

	_check(finished == 0, "partial refresh was reported as fully fresh")
	_check(failed == 1, "partial refresh did not emit refresh_failed")
	_check(state.ship_availability == ClientState.PARTIAL, "failed ship domain was not marked partial")
	_check(state.sector_availability == ClientState.FRESH, "successful sector domain was not marked fresh")
	_check(state.ship.is_empty(), "mismatched ship identity replaced the confirmed ship state")

	# A delayed sector.info reply for a different location must not replace the
	# location bound to the authenticated player's fresh player record.
	var confirmed_sector_name: String = str(state.sector.get("name", ""))
	_check(not state.accept_sector({"sector_id": 99, "name": "Delayed remote sector"}, refresh.current_generation()), "mismatched sector identity was accepted")
	_check(str(state.sector.get("name", "")) == confirmed_sector_name, "mismatched sector identity replaced confirmed visibility state")

	# A response with no player->active-ship relationship is not sufficient to
	# authorize the following ship.status record, even when that record has a
	# valid-looking ship ID.
	_check(refresh.refresh(true, ["player", "ship"]), "missing active-ship refresh did not start")
	var missing_player_id: String = transport.calls[3]["id"]
	refresh.handle_reply(missing_player_id, {
		"status": "ok", "type": "player.info",
		"data": {"player": {"id": 3, "sector": 17}},
	}, "player.my_info")
	var unbound_ship_id: String = transport.calls[4]["id"]
	refresh.handle_reply(unbound_ship_id, {
		"status": "ok", "type": "ship.status",
		"data": {"ship": {"id": 44, "name": "Unbound ship"}},
	}, "ship.status")
	_check(state.ship_availability == ClientState.PARTIAL, "ship without a player binding was reported as fresh")
	_check(state.ship.is_empty(), "ship without a player binding replaced the confirmed ship state")

	# A matching server-issued active-ship ID is accepted on the next refresh.
	_check(refresh.refresh(true, ["player", "ship"]), "matching ship refresh did not start")
	var player_retry_id: String = transport.calls[5]["id"]
	refresh.handle_reply(player_retry_id, {
		"status": "ok", "type": "player.info",
		"data": {"player": {"id": 3, "ship_id": 44, "sector": 17}},
	}, "player.my_info")
	var ship_retry_id: String = transport.calls[6]["id"]
	refresh.handle_reply(ship_retry_id, {
		"status": "ok", "type": "ship.status",
		"data": {"ship": {"id": 44, "name": "Confirmed ship"}},
	}, "ship.status")
	_check(state.ship_availability == ClientState.FRESH, "matching ship identity was not accepted")
	_check(state.ship.get("id") == 44, "matching ship identity was not retained")

	# Godot's socket JSON decoder can represent JSON integer identities as
	# integral floats. The authenticated player refresh must still hydrate the
	# live HUD; only fractional, string and boolean identities are invalid.
	var numeric_transport := FakeTransport.new()
	var numeric_state := ClientState.new()
	numeric_state.mark_authenticated()
	var numeric_auth := FakeAuth.new()
	numeric_auth.player_id = 3
	var numeric_refresh := Refresh.new(numeric_transport, numeric_auth, numeric_state)
	_check(numeric_refresh.refresh(false, ["player"]), "numeric player refresh did not start")
	var numeric_request: String = numeric_transport.calls[0]["id"]
	numeric_refresh.handle_reply(numeric_request, {
		"status": "ok", "type": "player.info",
		"data": {"player": {"id": 3.0, "ship_id": 44.0, "sector": 17.0, "credits": "5000.00", "turns_remaining": 318.0}},
	}, "player.my_info")
	_check(numeric_state.player_availability == ClientState.FRESH and numeric_state.snapshot()["hud"]["credits"] == 5000, "integral JSON player identity did not hydrate the live HUD")

	# Ownership refreshes must use the published ship.status response type. An
	# omitted type is not a compatible variant because transfer/recovery code
	# must not hydrate lifecycle state from an ambiguous envelope.
	_check(refresh.refresh(true, ["ship"]), "typed ship refresh did not start")
	var untyped_ship_id: String = transport.calls[7]["id"]
	refresh.handle_reply(untyped_ship_id, {
		"status": "ok",
		"data": {"ship": {"id": 44, "name": "Untyped ship"}},
	}, "ship.status")
	_check(state.ship_availability == ClientState.PARTIAL, "untyped ship refresh was accepted as authoritative")
	_check(state.ship.get("name") == "Confirmed ship", "untyped ship refresh replaced confirmed ownership state")

	# A refresh is the recovery path after an uncertain command.  A malformed
	# identity must not become an authoritative location merely because Godot
	# can numerically coerce 12.0 to 12.
	var generation_before_malformed := refresh.current_generation()
	_check(not state.accept_sector({"sector_id": 17.0, "name": "Untrusted sector"}, generation_before_malformed), "refresh accepted a floating-point sector ID")
	_check(state.sector.get("id") == 17, "malformed sector response replaced the confirmed sector")
	_check(state.sector.get("name") == "Test Sector", "malformed sector response changed the confirmed sector name")

	# A post-landing refresh must not accept a success-shaped player record for
	# another account.  The refresh remains failed so the caller keeps surface
	# actions unavailable until the authenticated player's state is confirmed.
	var identity_transport := FakeTransport.new()
	var identity_state := ClientState.new()
	identity_state.mark_authenticated()
	var identity_auth := FakeAuth.new()
	identity_auth.player_id = 3
	var identity_refresh := Refresh.new(identity_transport, identity_auth, identity_state)
	var identity_failed := 0
	identity_refresh.refresh_failed.connect(func(_snapshot: Dictionary) -> void: identity_failed += 1)
	_check(identity_refresh.refresh(false, ["player"]), "identity refresh did not start")
	var identity_request_id: String = identity_transport.calls[0]["id"]
	identity_refresh.handle_reply(identity_request_id, {
		"status": "ok", "type": "player.info",
		"data": {"player": {"id": 99, "ship_id": 44, "sector": 17}},
	}, "player.my_info")
	_check(identity_failed == 1, "cross-account player refresh was reported as fresh")
	_check(identity_state.player_availability == ClientState.PARTIAL, "cross-account player refresh was not marked partial")
	_check(identity_state.player.is_empty(), "cross-account player data replaced the confirmed player state")

	if failures.is_empty():
		print("Godot authoritative refresh tests: 14 checks passed")
		quit(0)
	else:
		for failure in failures:
			push_error(failure)
		quit(1)

func _check(condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)
