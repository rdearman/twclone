class_name TwRequestManager
extends RefCounted

## Tracks requests independently from the socket so it can be tested without IO.

var timeout_seconds: float = 15.0
var _sequence: int = 0
var _pending: Dictionary = {}

func next_id() -> String:
	_sequence += 1
	return "godot-%04d" % _sequence

func register(request_id: String, command: String, now_ms: int = -1) -> void:
	if now_ms < 0:
		now_ms = Time.get_ticks_msec()
	_pending[request_id] = {
		"command": command,
		"deadline_ms": now_ms + int(timeout_seconds * 1000.0),
	}

func resolve(request_id: String) -> Dictionary:
	if not _pending.has(request_id):
		return {}
	var request: Dictionary = _pending[request_id]
	_pending.erase(request_id)
	return request

func expire(now_ms: int = -1) -> Array:
	if now_ms < 0:
		now_ms = Time.get_ticks_msec()
	var expired := []
	for request_id in _pending.keys():
		var request: Dictionary = _pending[request_id]
		if now_ms >= int(request["deadline_ms"]):
			expired.append({"id": request_id, "command": request["command"]})
			_pending.erase(request_id)
	return expired

func clear() -> Array:
	var cleared := []
	for request_id in _pending.keys():
		cleared.append({"id": request_id, "command": _pending[request_id]["command"]})
	_pending.clear()
	return cleared

func has(request_id: String) -> bool:
	return _pending.has(request_id)

func size() -> int:
	return _pending.size()
