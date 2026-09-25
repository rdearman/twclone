class_name TwAuthoritativeRefresh
extends RefCounted

## Sequential player -> ship -> sector refresh over the Slice 1 transport.

const Protocol = preload("res://Protocol.gd")
const ClientState = preload("res://ClientState.gd")

signal refresh_started(generation: int)
signal refresh_finished(snapshot: Dictionary)
signal refresh_failed(snapshot: Dictionary)

var transport
var auth_session
var state
var _generation: int = 0
var _active := false
var _pending: Dictionary = {}
var _retired: Dictionary = {}
var _domains: Array[String] = []
var _domain_index := 0

func _init(transport_ref, auth_session_ref, state_ref = null) -> void:
	transport = transport_ref
	auth_session = auth_session_ref
	state = state_ref if state_ref != null else ClientState.new()

func refresh(force: bool = false, requested_domains: Array = []) -> bool:
	if not auth_session.is_authenticated():
		return false
	if _active and not force:
		return false
	for request_id in _pending:
		_retired[request_id] = true
	_pending.clear()
	_generation += 1
	_active = true
	_domains.clear()
	var requested: Array = ["player", "ship", "sector"] if requested_domains.is_empty() else requested_domains
	for domain in ["player", "ship", "sector"]:
		if domain in requested and not _domains.has(domain):
			_domains.append(domain)
	if _domains.is_empty() or not state.begin_refresh(_generation, _domains):
		_active = false
		return false
	refresh_started.emit(_generation)
	_domain_index = 0
	_request_step(_domains[0])
	return true

func accepts_request(request_id: String) -> bool:
	return _pending.has(request_id) or _retired.has(request_id)

func handle_reply(request_id: String, response: Dictionary, command: String) -> bool:
	if _retired.has(request_id):
		_retired.erase(request_id)
		return true
	if not _pending.has(request_id):
		return false
	var operation: Dictionary = _pending[request_id]
	_pending.erase(request_id)
	if int(operation["generation"]) != _generation:
		return true
	var domain := str(operation["domain"])
	var expected_command := str(operation["command"])
	var expected_type := str(operation["type"])
	var result: Dictionary = Protocol.result_from_response(response)
	var accepted: bool = result["kind"] == "ok" and command == expected_command and str(response.get("type", "")) == expected_type
	if accepted:
		var data = response.get("data", {})
		if not (data is Dictionary):
			accepted = false
		else:
			match domain:
				"player": accepted = state.accept_player(data, _generation)
				"ship": accepted = state.accept_ship(data, _generation)
				"sector": accepted = state.accept_sector(data, _generation)
	if not accepted:
		state.record_failure(domain, _generation)
	_next_or_finish(domain)
	return true

func handle_failure(request_id: String, _command: String, _result: Dictionary) -> bool:
	if _retired.has(request_id):
		_retired.erase(request_id)
		return true
	if not _pending.has(request_id):
		return false
	var operation: Dictionary = _pending[request_id]
	_pending.erase(request_id)
	if int(operation["generation"]) != _generation:
		return true
	var domain := str(operation["domain"])
	state.record_failure(domain, _generation)
	_next_or_finish(domain)
	return true

func handle_disconnect() -> void:
	for request_id in _pending:
		_retired[request_id] = true
	_pending.clear()
	_active = false
	state.mark_disconnected()

func is_active() -> bool:
	return _active

func current_generation() -> int:
	return _generation

func _request_step(domain: String) -> void:
	var command := ""
	var response_type := ""
	match domain:
		"player":
			command = "player.my_info"
			response_type = "player.info"
		"ship":
			command = "ship.status"
			response_type = "ship.status"
		"sector":
			command = "sector.info"
			response_type = "sector.info"
		_:
			return
	var request_id: String = transport.request(command, {}, auth_session.session_token)
	if request_id.is_empty():
		state.record_failure(domain, _generation)
		_next_or_finish(domain)
		return
	_pending[request_id] = {
		"generation": _generation,
		"domain": domain,
		"command": command,
		"type": response_type,
	}

func _next_or_finish(domain: String) -> void:
	_domain_index += 1
	if _domain_index < _domains.size():
		_request_step(_domains[_domain_index])
		return
	_active = false
	var snapshot: Dictionary = state.finish_refresh(_generation)
	var requested_are_fresh := true
	for requested_domain in _domains:
		if str(snapshot["freshness"].get(requested_domain, "")) != ClientState.FRESH:
			requested_are_fresh = false
		break
	if requested_are_fresh:
		refresh_finished.emit(snapshot)
	else:
		refresh_failed.emit(snapshot)
