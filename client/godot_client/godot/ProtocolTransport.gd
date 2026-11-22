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

const DEFAULT_CONNECTION_TIMEOUT_SECONDS := 5.0
var request_timeout_seconds: float = 15.0
var connection_timeout_seconds: float = DEFAULT_CONNECTION_TIMEOUT_SECONDS
var max_events: int = 200

var _socket: StreamPeerTCP = StreamPeerTCP.new()
var _tls_socket: StreamPeerTLS
var _tls_enabled := false
var _tls_host := ""
var _trusted_certificate_path := ""
var _buffer := ""
var _requests := RequestManager.new()
var _events: Array = []
var _connected_announced := false
var _connecting := false
var _connection_deadline_ms := 0
var _diagnostics: Array[Dictionary] = []
var _diagnostic_starts: Dictionary = {}
const DIAGNOSTIC_CAPACITY := 100
const SAFE_DIAGNOSTIC_FIELDS := ["id", "request_id", "reply_to", "command", "timestamp", "status", "type", "data", "meta", "error", "code", "error_code", "request", "response", "response_type", "duration_ms", "timeout", "client", "client_version", "build_id", "protocol_version", "os", "sector_id", "ship_id", "planet_id", "player_id", "quantity", "amount", "count", "level", "class", "action", "destination", "to_sector_id", "from_sector_id", "commodity", "port_id", "port_class", "turns_remaining", "credits", "cargo", "shields", "fighters", "hull", "message"]
const SENSITIVE_DIAGNOSTIC_PARTS := ["pass", "token", "secret", "cookie", "auth", "email", "ip", "path", "private", "chat", "message"]

func _init(timeout_seconds: float = 15.0, event_limit: int = 200, connect_timeout_seconds: float = DEFAULT_CONNECTION_TIMEOUT_SECONDS) -> void:
	request_timeout_seconds = timeout_seconds
	connection_timeout_seconds = maxf(0.1, connect_timeout_seconds)
	max_events = event_limit
	_requests.timeout_seconds = timeout_seconds

func connect_to_server(host: String, port: int, use_tls: bool = false, trusted_certificate_path: String = "") -> Error:
	print("[AUTH_TRACE] Transport connect start host=%s port=%d" % [host, port])
	close("Connection reset.")
	_socket = StreamPeerTCP.new()
	_tls_socket = null
	_tls_enabled = use_tls
	_tls_host = host
	_trusted_certificate_path = trusted_certificate_path
	_buffer = ""
	_connected_announced = false
	_connecting = true
	_connection_deadline_ms = Time.get_ticks_msec() + int(connection_timeout_seconds * 1000.0)
	var error := _socket.connect_to_host(host, port)
	print("[AUTH_TRACE] Transport connect requested result=%s" % [str(error)])
	if error != OK:
		_connecting = false
		_mark_disconnected("Unable to connect to the server.")
	return error

func poll(_delta: float = 0.0) -> void:
	_socket.poll()
	var status := _socket.get_status()
	if _connecting and status != StreamPeerTCP.STATUS_CONNECTED and Time.get_ticks_msec() >= _connection_deadline_ms:
		_mark_disconnected("Connection timed out after %d seconds." % int(ceil(connection_timeout_seconds)))
		return
	if status == StreamPeerTCP.STATUS_CONNECTED:
		if _tls_enabled:
			if _tls_socket == null:
				var tls_options: TLSOptions = TLSOptions.client()
				if not _trusted_certificate_path.is_empty():
					var cert := X509Certificate.new()
					var cert_text := FileAccess.get_file_as_string(_trusted_certificate_path)
					var cert_error := cert.load_from_string(cert_text)
					if cert_error != OK:
						_mark_disconnected("Could not load the configured TLS trust certificate.")
						return
					tls_options = TLSOptions.client(cert)
				_tls_socket = StreamPeerTLS.new()
				var tls_error := _tls_socket.connect_to_stream(_socket, _tls_host, tls_options)
				if tls_error != OK:
					_mark_disconnected("Could not start the verified TLS handshake.")
					return
			_tls_socket.poll()
			var tls_status := _tls_socket.get_status()
			if tls_status == StreamPeerTLS.STATUS_CONNECTED:
				_announce_connected()
				_read_available()
			elif tls_status == StreamPeerTLS.STATUS_ERROR or tls_status == StreamPeerTLS.STATUS_ERROR_HOSTNAME_MISMATCH:
				_mark_disconnected("TLS certificate verification failed.")
		else:
			_announce_connected()
			_read_available()
	elif status == StreamPeerTCP.STATUS_ERROR:
		_mark_disconnected("The connection to the server failed.")
	elif status == StreamPeerTCP.STATUS_NONE and (_connected_announced or _connecting):
		_mark_disconnected("The server closed the connection.")

	for expired in _requests.expire():
		var request_id := str(expired["id"])
		var command := str(expired["command"])
		_record_timeout(request_id)
		var result: Dictionary = Protocol.timeout_result(command)
		request_timed_out.emit(request_id, command)
		request_failed.emit(request_id, command, result)

func request(command: String, data: Dictionary, session_token: String = "") -> String:
	if not is_transport_connected():
		protocol_warning.emit("Cannot send a request while disconnected.")
		return ""
	var request_id := _requests.next_id()
	var envelope: Dictionary = Protocol.build_request(request_id, command, data, session_token)
	print("[AUTH_TRACE] Transport writing request command=%s request_id=%s" % [command, request_id])
	_requests.register(request_id, command)
	_record_request(request_id, command, data)
	_emit_diagnostic("SEND", envelope)
	var peer: StreamPeer = _tls_socket if _tls_enabled else _socket
	var error := peer.put_data((JSON.stringify(envelope) + "\n").to_utf8_buffer())
	if error != OK:
		_requests.resolve(request_id)
		_mark_disconnected("Connection lost while sending data.")
		return ""
	return request_id

func close(reason: String = "Client disconnected.") -> void:
	var was_connected := _connected_announced
	var was_connecting := _connecting
	if _tls_socket != null:
		_tls_socket.disconnect_from_stream()
		_tls_socket = null
	_socket.disconnect_from_host()
	_connected_announced = false
	_connecting = false
	_connection_deadline_ms = 0
	_buffer = ""
	if was_connected or was_connecting:
		_fail_pending(Protocol.disconnected_result(reason))
	else:
		_requests.clear()
	if was_connected or was_connecting:
		transport_disconnected.emit(reason)

func is_transport_connected() -> bool:
	if _tls_enabled:
		return _tls_socket != null and _tls_socket.get_status() == StreamPeerTLS.STATUS_CONNECTED
	return _socket.get_status() == StreamPeerTCP.STATUS_CONNECTED

func pending_count() -> int:
	return _requests.size()

func drain_events() -> Array:
	var events := _events.duplicate()
	_events.clear()
	return events

func diagnostic_snapshot(count: int = 25) -> Array[Dictionary]:
	if count not in [10, 25, 50, 100]:
		return []
	var start := maxi(0, _diagnostics.size() - count)
	var result: Array[Dictionary] = []
	for index in range(start, _diagnostics.size()):
		result.append(_diagnostics[index].duplicate(true))
	return result

func _record_request(request_id: String, command: String, data: Dictionary) -> void:
	_diagnostic_starts[request_id] = Time.get_ticks_msec()
	var entry := {"timestamp": Time.get_unix_time_from_system(), "request_id": request_id, "command": command, "duration_ms": 0, "context": {"client": "godot", "client_version": "4.5.2", "protocol_version": "v3", "os": OS.get_name()}}
	if command.begins_with("auth.") or command.begins_with("chat.") or command.begins_with("mail.") or command.begins_with("comm."):
		entry["request"] = {"omitted": true}
	else:
		entry["request"] = _sanitize_diagnostic(data)
	_diagnostics.append(entry)
	while _diagnostics.size() > DIAGNOSTIC_CAPACITY:
		_diagnostics.pop_front()

func _record_response(request_id: String, command: String, response: Dictionary) -> void:
	for index in range(_diagnostics.size() - 1, -1, -1):
		if str(_diagnostics[index].get("request_id", "")) == request_id:
			var entry := _diagnostics[index]
			entry["duration_ms"] = maxi(0, Time.get_ticks_msec() - int(_diagnostic_starts.get(request_id, Time.get_ticks_msec())))
			_diagnostic_starts.erase(request_id)
			entry["response_type"] = str(response.get("type", ""))
			entry["status"] = str(response.get("status", ""))
			if command.begins_with("auth.") or command.begins_with("chat.") or command.begins_with("mail.") or command.begins_with("comm."):
				entry["response"] = {"omitted": true}
			else:
				entry["response"] = _sanitize_diagnostic(response.get("data", {}))
			var error_value = response.get("error", {})
			if error_value is Dictionary:
				entry["error_code"] = error_value.get("code", null)
			_diagnostics[index] = entry
			return

func _record_timeout(request_id: String) -> void:
	for index in range(_diagnostics.size() - 1, -1, -1):
		if str(_diagnostics[index].get("request_id", "")) == request_id:
			var entry := _diagnostics[index]
			entry["timeout"] = true
			entry["duration_ms"] = maxi(0, Time.get_ticks_msec() - int(_diagnostic_starts.get(request_id, Time.get_ticks_msec())))
			_diagnostic_starts.erase(request_id)
			_diagnostics[index] = entry
			return

func _sanitize_diagnostic(value, depth: int = 0):
	if depth > 8:
		return "[depth-limited]"
	if value is Dictionary:
		var clean := {}
		for key in value:
			var lower := str(key).to_lower()
			var denied := false
			for part in SENSITIVE_DIAGNOSTIC_PARTS:
				if lower.contains(part):
					denied = true
					break
			if not denied and str(key) in SAFE_DIAGNOSTIC_FIELDS:
				clean[key] = _sanitize_diagnostic(value[key], depth + 1)
		return clean
	if value is Array:
		var clean_array: Array = []
		for item in value.slice(0, mini(value.size(), 100)):
			clean_array.append(_sanitize_diagnostic(item, depth + 1))
		return clean_array
	if value is String:
		var result := str(value)
		result = result.replace("/home/", "[redacted-path]/").replace("/Users/", "[redacted-path]/").replace("/tmp/", "[redacted-path]/")
		var email_regex := RegEx.new()
		email_regex.compile("[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\\.[A-Za-z]{2,}")
		result = email_regex.sub(result, "[redacted-email]", true)
		var ip_regex := RegEx.new()
		ip_regex.compile("(?:[0-9]{1,3}\\.){3}[0-9]{1,3}(?::[0-9]{1,5})?|(?i)(?:[0-9a-f]{0,4}:){2,7}[0-9a-f:]{0,4}")
		result = ip_regex.sub(result, "[redacted-ip]", true)
		var bearer_regex := RegEx.new()
		bearer_regex.compile("(?i)Bearer\\s+[A-Za-z0-9._~+/-]+=*")
		result = bearer_regex.sub(result, "[redacted-credential]", true)
		var assignment_regex := RegEx.new()
		assignment_regex.compile("(?i)(password|passwd|token|secret|api[_-]?key|authorization)\\s*[:=]\\s*[^\\s,;]+")
		result = assignment_regex.sub(result, "[redacted-credential]", true)
		var jwt_regex := RegEx.new()
		jwt_regex.compile("eyJ[A-Za-z0-9_-]{8,}\\.[A-Za-z0-9_-]{8,}\\.[A-Za-z0-9_-]{8,}")
		result = jwt_regex.sub(result, "[redacted-credential]", true)
		var path_regex := RegEx.new()
		path_regex.compile("(?:/(?:home|Users|tmp|var|opt|mnt|private|Volumes)/|[A-Za-z]:\\\\)[^\\s\\\"']+")
		result = path_regex.sub(result, "[redacted-path]", true)
		var github_regex := RegEx.new()
		github_regex.compile("(?:gh[pousr]_[A-Za-z0-9]{20,}|github_pat_[A-Za-z0-9_]{20,})")
		return github_regex.sub(result, "[redacted-credential]", true).left(12000)
	return value if value == null or value is bool or value is int or value is float else null

func _read_available() -> void:
	var peer: StreamPeer = _tls_socket if _tls_enabled else _socket
	var available := peer.get_available_bytes()
	if available <= 0:
		return
	var data := peer.get_utf8_string(available)
	_emit_diagnostic("RECV", {"bytes": available, "frames": data.split("\n", false).size()})
	_consume_bytes(data)

func _announce_connected() -> void:
	if _connected_announced:
		return
	_connected_announced = true
	_connecting = false
	print("[AUTH_TRACE] Transport connection established tls=%s" % str(_tls_enabled))
	transport_connected.emit()
	_emit_diagnostic("STATE", {"status": "connected", "tls": _tls_enabled})

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
		print("[AUTH_TRACE] Transport dispatching reply command=%s request_id=%s" % [str(request.get("command", "")), request_id])
		_record_response(request_id, str(request.get("command", "")), frame)
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
	if _tls_socket != null:
		_tls_socket.disconnect_from_stream()
		_tls_socket = null
	_socket.disconnect_from_host()
	transport_disconnected.emit(reason)

func _fail_pending(result: Dictionary) -> void:
	for pending in _requests.clear():
		request_failed.emit(str(pending["id"]), str(pending["command"]), result)

func _emit_diagnostic(kind: String, payload) -> void:
	diagnostic.emit(kind, JSON.stringify(_sanitize_diagnostic(payload)))
