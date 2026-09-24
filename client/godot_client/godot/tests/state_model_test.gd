extends SceneTree

const ClientState = preload("res://ClientState.gd")
const Refresh = preload("res://AuthoritativeRefresh.gd")
const Protocol = preload("res://Protocol.gd")
const AuthSession = preload("res://AuthSession.gd")

class FakeTransport:
	extends RefCounted
	var sequence := 0
	var calls: Array = []

	func request(command: String, data: Dictionary, token: String) -> String:
		sequence += 1
		var request_id := "req-%d" % sequence
		calls.append({"id": request_id, "command": command, "data": data, "token": token})
		return request_id

class FakeAuth:
	extends RefCounted
	var session_token := "session-1"
	var authenticated := true

	func is_authenticated() -> bool:
		return authenticated

var failures: Array = []

func _init() -> void:
	_run("player normalization", _test_player_normalization)
	_run("ship normalization", _test_ship_normalization)
	_run("sector normalization", _test_sector_normalization)
	_run("JSON numeric normalization", _test_json_numeric_normalization)
	_run("unknown fields", _test_unknown_fields)
	_run("zero fields", _test_zero_fields)
	_run("integer money", _test_integer_money)
	_run("optional fields", _test_optional_fields)
	_run("malformed fields", _test_malformed_fields)
	_run("authenticated refresh", _test_authenticated_refresh)
	_run("scoped refresh", _test_scoped_refresh)
	_run("unauthenticated refresh", _test_unauthenticated_refresh)
	_run("refusal retention", _test_refusal_retention)
	_run("timeout retention", _test_timeout_retention)
	_run("disconnect retention", _test_disconnect_retention)
	_run("partial refresh", _test_partial_refresh)
	_run("late generation", _test_late_generation)
	_run("unexpected type", _test_unexpected_type)
	_run("reauth refresh", _test_reauth_refresh)
	_run("scene state boundary", _test_scene_state_boundary)
	_run("normal output safety", _test_normal_output_safety)
	_run("debug redaction", _test_debug_redaction)
	if failures.is_empty():
		print("Godot state tests: 22 passed")
		quit(0)
	else:
		for failure in failures:
			push_error(failure)
		print("Godot state tests: %d failed" % failures.size())
		quit(1)

func _run(name: String, test: Callable) -> void:
	var before := failures.size()
	test.call()
	if failures.size() == before:
		return
	for index in range(before, failures.size()):
		failures[index] = name + ": " + failures[index]

func _check(condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)

func _test_player_normalization() -> void:
	var player: Dictionary = ClientState.normalize_player({"player": {
		"id": 7, "username": "Alice", "credits": "1200.00", "turns_remaining": 4,
		"sector": 9, "ship_id": 12, "corp_id": 3,
	}})
	_check(player["id"] == 7 and player["sector_id"] == 9, "player identifiers were not normalized")
	_check(player["credits"] == 1200 and player["turns_remaining"] == 4, "player values were not normalized")

func _test_ship_normalization() -> void:
	var ship: Dictionary = ClientState.normalize_ship({"ship": {
		"id": 12, "name": "Wayfarer", "type_id": 2, "holds": 40,
		"fighters": 250, "shields": 80,
		"cargo": [{"commodity": "ore", "quantity": 10}, {"commodity": "fuel", "qty": 8}],
	}})
	_check(ship["id"] == 12 and ship["holds"] == 40, "ship identity/capacity was not normalized")
	_check(ship["fighters"] == 250 and ship["shields"] == 80, "ship defence was not normalized")
	_check(ship["cargo_used"] == 18 and ship["cargo"].size() == 2, "ship cargo was not normalized")

func _test_sector_normalization() -> void:
	var sector: Dictionary = ClientState.normalize_sector({
		"sector_id": 9, "name": "Sol", "beacon": "Home",
		"adjacent_sectors": [2, 7], "ships_present": [{"id": 12, "name": "Wayfarer"}],
		"ports": [{"id": 4, "name": "Orion", "type": 3}],
		"celestial_objects": [{"id": 5, "name": "Earth"}],
		"counts": {"fighters": 0, "mines": 2},
	})
	_check(sector["id"] == 9 and sector["name"] == "Sol", "sector identity was not normalized")
	_check(sector["adjacent_sector_ids"] == [2, 7], "sector adjacency was not normalized")
	_check(sector["ports"].size() == 1 and sector["planets"].size() == 1, "sector objects were not normalized")
	_check(sector["counts"]["fighters"] == 0, "sector zero count was lost")

func _test_json_numeric_normalization() -> void:
	var player := ClientState.normalize_player({"player": {
		"id": 3.0, "username": "newguy", "credits": "5000.00", "turns_remaining": 999999.0,
		"sector": 1.0, "ship_id": 3.0,
	}})
	var ship := ClientState.normalize_ship({"ship": {
		"id": 3.0, "name": "Bit Banger", "type_id": 2.0, "holds": 10.0,
		"fighters": 1.0, "shields": 1.0, "cargo": [],
	}})
	var sector := ClientState.normalize_sector({
		"sector_id": 1.0, "name": "Fedspace 1", "beacon": "The Federation -- Do Not Dump!",
		"adjacent_sectors": [{"to_sector": 2.0}, {"to_sector": 3.0}],
		"celestial_objects": [{"name": "Terra", "planet_id": 1.0, "type": 1.0}],
		"ships_present": [{"name": "Bit Banger", "ship_id": 3.0}],
		"counts": {"fighters": 0.0, "mines": 0.0},
	})
	_check(player.get("sector_id") == 1 and player.get("turns_remaining") == 999999, "integral JSON player numbers were discarded")
	_check(ship.get("holds") == 10 and ship.get("fighters") == 1 and ship.get("shields") == 1, "integral JSON ship numbers were discarded")
	_check(sector.get("id") == 1 and sector.get("adjacent_sector_ids") == [2, 3], "JSON sector identity or object-form warps were discarded")
	_check(sector.get("planets", []).size() == 1 and sector.get("ships", []).size() == 1, "JSON entity identifiers were discarded")
	_check(sector.get("counts", {}).get("fighters") == 0, "JSON zero count was not preserved")

func _test_unknown_fields() -> void:
	var state := ClientState.new()
	state.mark_authenticated()
	state.begin_refresh(1)
	state.accept_player({"player": {"id": 1}}, 1)
	var hud: Dictionary = state.snapshot()["hud"]
	_check(not state.player.has("credits"), "missing credits became a value")
	_check(hud["credits"] == null and hud["turns_remaining"] == null, "unknown HUD fields were not null")

func _test_zero_fields() -> void:
	var state := ClientState.new()
	state.mark_authenticated()
	state.begin_refresh(1)
	state.accept_player({"player": {"credits": 0, "turns_remaining": 0}}, 1)
	state.accept_ship({"ship": {"holds": 0, "fighters": 0, "shields": 0, "cargo": []}}, 1)
	var hud: Dictionary = state.snapshot()["hud"]
	_check(hud["credits"] == 0 and hud["turns_remaining"] == 0, "valid player zero became unknown")
	_check(hud["cargo_used"] == 0 and hud["cargo_total"] == 0, "valid ship zero became unknown")

func _test_integer_money() -> void:
	var integer: Dictionary = ClientState.normalize_player({"player": {"credits": 42}})
	var json_number: Dictionary = ClientState.normalize_player({"player": {"credits": 42.0}})
	var decimal_string: Dictionary = ClientState.normalize_player({"player": {"credits": "42.00"}})
	var malformed: Dictionary = ClientState.normalize_player({"player": {"credits": "42.50"}})
	_check(typeof(integer["credits"]) == TYPE_INT and integer["credits"] == 42, "integer credits changed type")
	_check(typeof(json_number["credits"]) == TYPE_INT and json_number["credits"] == 42, "integral JSON-number credits were not normalized")
	_check(decimal_string["credits"] == 42, "server decimal-string credits were not normalized")
	_check(not malformed.has("credits"), "fractional malformed credits were accepted")

func _test_optional_fields() -> void:
	var player: Dictionary = ClientState.normalize_player({"player": {"username": "Alice"}})
	var ship: Dictionary = ClientState.normalize_ship({"ship": {"name": "Unnamed"}})
	_check(player["username"] == "Alice" and ship["name"] == "Unnamed", "optional fields caused normalization failure")

func _test_malformed_fields() -> void:
	var state := ClientState.new()
	state.mark_authenticated()
	state.begin_refresh(1)
	_check(not state.accept_player({"player": "bad"}, 1), "malformed player object was accepted")
	_check(not state.accept_ship({"ship": [1, 2]}, 1), "malformed ship object was accepted")
	var sector: Dictionary = ClientState.normalize_sector({"sector_id": "bad", "ports": [null, "bad"]})
	_check(not sector.has("id") and sector.get("ports", []).is_empty(), "malformed sector fields leaked into state")

func _new_refresh() -> Array:
	var transport = FakeTransport.new()
	var auth = FakeAuth.new()
	var state := ClientState.new()
	state.mark_authenticated()
	var refresh = Refresh.new(transport, auth, state)
	return [transport, auth, state, refresh]

func _player_reply(name: String, credits: int = 10) -> Dictionary:
	return {"status": "ok", "type": "player.info", "data": {"player": {"username": name, "credits": credits, "turns_remaining": 3, "sector": 9, "ship_id": 12}}}

func _ship_reply(name: String = "Ship") -> Dictionary:
	return {"status": "ok", "type": "ship.status", "data": {"ship": {"id": 12, "name": name, "holds": 20, "fighters": 1, "shields": 2, "cargo": []}}}

func _sector_reply(name: String = "Sol") -> Dictionary:
	return {"status": "ok", "type": "sector.info", "data": {"sector_id": 9, "name": name, "adjacent_sectors": [2]}}

func _complete_refresh(refresh, transport, player_name: String = "Alice") -> void:
	var player_id: String = transport.calls[transport.calls.size() - 1]["id"]
	refresh.handle_reply(player_id, _player_reply(player_name), "player.my_info")
	var ship_id: String = transport.calls[transport.calls.size() - 1]["id"]
	refresh.handle_reply(ship_id, _ship_reply(), "ship.status")
	var sector_id: String = transport.calls[transport.calls.size() - 1]["id"]
	refresh.handle_reply(sector_id, _sector_reply(), "sector.info")

func _test_authenticated_refresh() -> void:
	var parts: Array = _new_refresh()
	var transport = parts[0]
	var state = parts[2]
	var refresh = parts[3]
	_check(refresh.refresh(), "authenticated refresh did not start")
	_check(transport.calls[0]["command"] == "player.my_info", "refresh did not begin with player.my_info")
	_complete_refresh(refresh, transport)
	_check(state.player["username"] == "Alice" and state.ship["name"] == "Ship", "refresh did not publish player and ship")
	_check(state.sector["id"] == 9 and state.overall_availability() == ClientState.FRESH, "refresh did not publish fresh sector snapshot")

func _test_scoped_refresh() -> void:
	var parts: Array = _new_refresh()
	_seed_confirmed(parts)
	var transport = parts[0]
	var state = parts[2]
	var refresh = parts[3]
	var previous_sector: Dictionary = state.sector.duplicate(true)
	var call_count: int = transport.calls.size()
	_check(refresh.refresh(true, ["player", "ship"]), "player-and-ship scoped refresh did not start")
	var player_id: String = transport.calls[call_count]["id"]
	refresh.handle_reply(player_id, _player_reply("Alice", 20), "player.my_info")
	var ship_id: String = transport.calls[call_count + 1]["id"]
	refresh.handle_reply(ship_id, _ship_reply("Surface Ship"), "ship.status")
	_check(transport.calls.size() == call_count + 2, "scoped refresh unexpectedly requested sector.info")
	_check(state.player_availability == ClientState.FRESH and state.ship_availability == ClientState.FRESH, "scoped player/ship refresh was not fresh")
	_check(state.sector == previous_sector and state.sector_availability == ClientState.STALE, "unrequested sector was not retained and marked stale")

func _test_unauthenticated_refresh() -> void:
	var transport = FakeTransport.new()
	var auth = FakeAuth.new()
	auth.authenticated = false
	var state := ClientState.new()
	var refresh = Refresh.new(transport, auth, state)
	_check(not refresh.refresh() and transport.calls.is_empty(), "unauthenticated refresh was allowed")

func _seed_confirmed(parts: Array) -> void:
	var state = parts[2]
	var refresh = parts[3]
	refresh.refresh()
	_complete_refresh(refresh, parts[0])
	_check(state.overall_availability() == ClientState.FRESH, "test fixture was not seeded")

func _test_refusal_retention() -> void:
	var parts: Array = _new_refresh()
	_seed_confirmed(parts)
	var state = parts[2]
	var refresh = parts[3]
	var old_name: String = state.player["username"]
	refresh.refresh(true)
	var player_id: String = parts[0].calls[parts[0].calls.size() - 1]["id"]
	refresh.handle_failure(player_id, "player.my_info", {"kind": "refused", "message": "Denied"})
	var ship_id: String = parts[0].calls[parts[0].calls.size() - 1]["id"]
	refresh.handle_failure(ship_id, "ship.status", {"kind": "refused", "message": "Denied"})
	var sector_id: String = parts[0].calls[parts[0].calls.size() - 1]["id"]
	refresh.handle_failure(sector_id, "sector.info", {"kind": "refused", "message": "Denied"})
	_check(state.player["username"] == old_name and state.overall_availability() == ClientState.PARTIAL, "refusal replaced or hid confirmed state incorrectly")

func _test_timeout_retention() -> void:
	var parts: Array = _new_refresh()
	_seed_confirmed(parts)
	var state = parts[2]
	var refresh = parts[3]
	refresh.refresh(true)
	var player_id: String = parts[0].calls[parts[0].calls.size() - 1]["id"]
	refresh.handle_failure(player_id, "player.my_info", Protocol.timeout_result("player.my_info"))
	var ship_id: String = parts[0].calls[parts[0].calls.size() - 1]["id"]
	refresh.handle_failure(ship_id, "ship.status", Protocol.timeout_result("ship.status"))
	var sector_id: String = parts[0].calls[parts[0].calls.size() - 1]["id"]
	refresh.handle_failure(sector_id, "sector.info", Protocol.timeout_result("sector.info"))
	_check(state.player["username"] == "Alice" and state.overall_availability() == ClientState.PARTIAL, "timeout mutated confirmed player state")

func _test_disconnect_retention() -> void:
	var parts: Array = _new_refresh()
	_seed_confirmed(parts)
	var state = parts[2]
	var old_name: String = state.player["username"]
	state.mark_disconnected()
	_check(state.player["username"] == old_name and state.overall_availability() == ClientState.DISCONNECTED, "disconnect did not retain stale values explicitly")
	_check(state.snapshot()["hud"]["credits"] != null, "disconnect erased confirmed values")

func _test_partial_refresh() -> void:
	var parts: Array = _new_refresh()
	var state = parts[2]
	var refresh = parts[3]
	refresh.refresh()
	var player_id: String = parts[0].calls[parts[0].calls.size() - 1]["id"]
	refresh.handle_failure(player_id, "player.my_info", Protocol.timeout_result("player.my_info"))
	var ship_id: String = parts[0].calls[parts[0].calls.size() - 1]["id"]
	refresh.handle_reply(ship_id, _ship_reply(), "ship.status")
	var sector_id: String = parts[0].calls[parts[0].calls.size() - 1]["id"]
	refresh.handle_failure(sector_id, "sector.info", Protocol.disconnected_result("lost"))
	_check(state.overall_availability() == ClientState.PARTIAL, "partial refresh was not represented")
	_check(state.player.is_empty() and not state.ship.is_empty(), "partial refresh did not retain successful domain")

func _test_late_generation() -> void:
	var parts: Array = _new_refresh()
	var transport = parts[0]
	var state = parts[2]
	var refresh = parts[3]
	refresh.refresh()
	refresh.refresh(true)
	_check(refresh.accepts_request("req-1"), "retired request was not recognized safely")
	refresh.handle_reply("req-1", _player_reply("old", 1), "player.my_info")
	var current_player_id: String = transport.calls[transport.calls.size() - 1]["id"]
	refresh.handle_reply(current_player_id, _player_reply("new", 2), "player.my_info")
	var current_ship_id: String = transport.calls[transport.calls.size() - 1]["id"]
	refresh.handle_reply(current_ship_id, _ship_reply(), "ship.status")
	var current_sector_id: String = transport.calls[transport.calls.size() - 1]["id"]
	refresh.handle_reply(current_sector_id, _sector_reply(), "sector.info")
	_check(state.player.get("username", "") == "new", "new generation did not update state")
	_check(state.player.get("credits", 0) == 2, "late old generation overwrote new state")

func _test_unexpected_type() -> void:
	var parts: Array = _new_refresh()
	var state = parts[2]
	var refresh = parts[3]
	refresh.refresh()
	refresh.handle_reply("req-1", {"status": "ok", "type": "sector.info", "data": {"sector_id": 99}}, "player.my_info")
	_check(state.player.is_empty() and state.player_availability != ClientState.FRESH, "unexpected reply type updated state")

func _test_reauth_refresh() -> void:
	var parts: Array = _new_refresh()
	var state = parts[2]
	var refresh = parts[3]
	_seed_confirmed(parts)
	var old_generation: int = refresh.current_generation()
	state.mark_disconnected()
	state.mark_authenticated()
	refresh.refresh()
	_complete_refresh(refresh, parts[0], "Reauthenticated")
	_check(refresh.current_generation() > old_generation, "reauthentication did not start a new generation")
	_check(state.player["username"] == "Reauthenticated", "reauthentication reused stale player data")

func _test_scene_state_boundary() -> void:
	var source := FileAccess.get_file_as_string("res://Main.gd")
	_check(source.contains("client_state.changed.connect"), "Main.gd does not subscribe to normalized ClientState snapshots")
	_check(source.contains("gameplay_view.set_snapshot(snapshot)"), "Main.gd does not pass normalized snapshots into GameplayView")
	_check(source.contains("client_state.mark_authenticated") and source.contains("refresh_coordinator.refresh"), "authentication does not trigger authoritative refresh")
	_check(not source.contains("JSON.stringify(data"), "Main.gd still dumps response data")

func _test_normal_output_safety() -> void:
	var refusal: Dictionary = Protocol.result_from_response({"status": "refused", "error": {"message": "Denied"}})
	_check(refusal["message"] == "Denied" and not refusal["message"].contains("{"), "normal result message was not human-readable")

func _test_debug_redaction() -> void:
	var diagnostic := Protocol.diagnostic_json({"data": {"passwd": "password-value-xyz", "auth": {"session": "session-value-xyz"}, "session_token": "session-token-value-xyz", "value": 0}})
	_check(not diagnostic.contains("password-value-xyz") and not diagnostic.contains("session-value-xyz") and not diagnostic.contains("session-token-value-xyz") and diagnostic.contains("0"), "debug diagnostic redaction failed")
