class_name TwProtocolTransport
extends RefCounted

## Non-rendering NDJSON transport with correlated replies and a bounded event queue.

const Protocol = preload("res://Protocol.gd")
const RequestManager = preload("res://RequestManager.gd")

signal transport_connected
signal transport_disconnected(reason: String)
signal reply_received(request_id: String, response: Dictionary, command: String)
signal request_timed_out(request_id: String, command: String)
signal request_failed(request_id: String, command: String, result: Dictionary)
signal protocol_error(message: String)
signal protocol_warning(message: String)
signal events_available
signal diagnostic(kind: String, payload: String)

var request_timeout_seconds: float = 15.0
var max_events: int = 200

var _socket: StreamPeerTCP = StreamPeerTCP.new()
var _buffer := ""
var _requests := RequestManager.new()
var _events: Array = []
var _connected_announced := false
var _connecting := false

func _init(timeout_seconds: float = 15.0, event_limit: int = 200) -> void:
	request_timeout_seconds = timeout_seconds
	max_events = event_limit
	_requests.timeout_seconds = timeout_seconds

func connect_to_server(host: String, port: int) -> Error:
	close("Connection reset.")
	_socket = StreamPeerTCP.new()
	_buffer = ""
	_connected_announced = false
	_connecting = true
	var error := _socket.connect_to_host(host, port)
	if error != OK:
		_connecting = false
		_mark_disconnected("Unable to connect to the server.")
	return error

func poll(_delta: float = 0.0) -> void:
	_socket.poll()
	var status := _socket.get_status()
	if status == StreamPeerTCP.STATUS_CONNECTED:
		if not _connected_announced:
			_connected_announced = true
			_connecting = false
			transport_connected.emit()
			_emit_diagnostic("STATE", {"status": "connected"})
		_read_available()
	elif status == StreamPeerTCP.STATUS_ERROR:
		_mark_disconnected("The connection to the server failed.")
	elif status == StreamPeerTCP.STATUS_NONE and (_connected_announced or _connecting):
		_mark_disconnected("The server closed the connection.")

	for expired in _requests.expire():
		var request_id := str(expired["id"])
		var command := str(expired["command"])
		var result: Dictionary = Protocol.timeout_result(command)
		request_timed_out.emit(request_id, command)
		request_failed.emit(request_id, command, result)

func request(command: String, data: Dictionary, session_token: String = "") -> String:
	if _socket.get_status() != StreamPeerTCP.STATUS_CONNECTED:
		protocol_warning.emit("Cannot send a request while disconnected.")
		return ""
	var request_id := _requests.next_id()
	var envelope: Dictionary = Protocol.build_request(request_id, command, data, session_token)
	_requests.register(request_id, command)
	_emit_diagnostic("SEND", envelope)
	var error := _socket.put_data((JSON.stringify(envelope) + "\n").to_utf8_buffer())
	if error != OK:
		_requests.resolve(request_id)
		_mark_disconnected("Connection lost while sending data.")
		return ""
	return request_id

func close(reason: String = "Client disconnected.") -> void:
	var was_connected := _connected_announced
	var was_connecting := _connecting
	_socket.disconnect_from_host()
	_connected_announced = false
	_connecting = false
	_buffer = ""
	if was_connected or was_connecting:
		_fail_pending(Protocol.disconnected_result(reason))
	else:
		_requests.clear()
	if was_connected or was_connecting:
		transport_disconnected.emit(reason)

func is_transport_connected() -> bool:
	return _socket.get_status() == StreamPeerTCP.STATUS_CONNECTED

func pending_count() -> int:
	return _requests.size()

func drain_events() -> Array:
	var events := _events.duplicate()
	_events.clear()
	return events

func _read_available() -> void:
	var available := _socket.get_available_bytes()
	if available <= 0:
		return
	var data := _socket.get_utf8_string(available)
	_emit_diagnostic("RECV", {"bytes": available, "frames": data.split("\n", false).size()})
	_consume_bytes(data)

func _consume_bytes(data: String) -> void:
	_buffer += data
	while "\n" in _buffer:
		var split := _buffer.split("\n", true, 1)
		var line := str(split[0]).strip_edges()
		_buffer = split[1]
		if not line.is_empty():
			_process_line(line)

func _process_line(line: String) -> void:
	var parsed := Protocol.parse_frame(line)
	if not bool(parsed["ok"]):
		protocol_error.emit(str(parsed["error"]))
		return
	var frame: Dictionary = parsed["frame"]
	_emit_diagnostic("FRAME", frame)
	if frame.has("reply_to"):
		var request_id := str(frame.get("reply_to", ""))
		var request := _requests.resolve(request_id)
		if request.is_empty():
			protocol_warning.emit("Ignored a reply for an unknown or expired request.")
			return
		reply_received.emit(request_id, frame, str(request.get("command", "")))
		return
	if _is_async_event(frame):
		_enqueue_event(frame)
		return
	protocol_warning.emit("Ignored an uncorrelated server frame.")

func _is_async_event(frame: Dictionary) -> bool:
	if frame.has("event") or frame.get("id", "") == "evt":
		return true
	return Protocol.classify_event_type(str(frame.get("type", ""))) != "unknown"

func _enqueue_event(frame: Dictionary) -> void:
	var event_type := str(frame.get("event", frame.get("type", "unknown")))
	var event := {
		"category": Protocol.classify_event_type(event_type),
		"type": event_type,
		"data": frame.get("data", {}),
		"status": frame.get("status", ""),
	}
	_events.append(event)
	while _events.size() > max_events:
		_events.pop_front()
	events_available.emit()

func _mark_disconnected(reason: String) -> void:
	if not (_connected_announced or _connecting):
		return
	_connected_announced = false
	_connecting = false
	_buffer = ""
	_fail_pending(Protocol.disconnected_result(reason))
	_socket.disconnect_from_host()
	transport_disconnected.emit(reason)

func _fail_pending(result: Dictionary) -> void:
	for pending in _requests.clear():
		request_failed.emit(str(pending["id"]), str(pending["command"]), result)

func _emit_diagnostic(kind: String, payload) -> void:
	diagnostic.emit(kind, Protocol.diagnostic_json(payload))
