class_name TwProtocol
extends RefCounted

## Pure helpers for the player-client envelope contract.

const REDACTED = "<redacted>"
const SECRET_KEYS = ["password", "passwd", "token", "session", "session_token", "secret", "authorization"]

static func parse_frame(line: String) -> Dictionary:
	var parser := JSON.new()
	if parser.parse(line) != OK:
		return {"ok": false, "error": "Server sent an invalid response envelope."}
	var value = parser.data
	if not (value is Dictionary):
		return {"ok": false, "error": "Server sent an invalid response envelope."}
	if not value.has("reply_to") and not value.has("event") and value.get("id", "") != "evt" and str(value.get("type", "")).is_empty():
		return {"ok": false, "error": "Server response had no correlation or event type."}
	if value.has("reply_to") and str(value.get("reply_to", "")).is_empty():
		return {"ok": false, "error": "Server response had an empty correlation identifier."}
	return {"ok": true, "frame": value}

static func response_status(frame: Dictionary) -> String:
	var status := str(frame.get("status", "")).to_lower()
	if status == "refused" or status == "error":
		return status
	var response_type := str(frame.get("type", "")).to_lower()
	if response_type == "error" or response_type == "refused":
		return "error" if response_type == "error" else "refused"
	return "ok"

static func response_message(frame: Dictionary) -> String:
	var error_value = frame.get("error", {})
	if error_value is Dictionary:
		var message := str(error_value.get("message", ""))
		if not message.is_empty():
			return message
	var data_value = frame.get("data", {})
	if data_value is Dictionary:
		var message := str(data_value.get("message", ""))
		if not message.is_empty():
			return message
	return "The server did not accept the request."

static func result_from_response(frame: Dictionary) -> Dictionary:
	return {
		"kind": response_status(frame),
		"message": response_message(frame),
		"data": frame.get("data", {}),
		"response": frame,
	}

static func timeout_result(command: String) -> Dictionary:
	return {"kind": "timeout", "message": "Request timed out: %s." % command, "data": {}}

static func disconnected_result(reason: String) -> Dictionary:
	return {"kind": "disconnected", "message": reason, "data": {}}

static func build_request(request_id: String, command: String, data: Dictionary, session_token: String = "") -> Dictionary:
	var envelope := {
		"id": request_id,
		"command": command,
		"data": data,
		"ts": Time.get_datetime_string_from_system(true),
	}
	if not session_token.is_empty():
		envelope["auth"] = {"session": session_token}
	return envelope

static func classify_event_type(event_type: String) -> String:
	if event_type.is_empty():
		return "unknown"
	var prefixes := {
		"system.": "system",
		"chat.": "chat",
		"comm.": "chat",
		"mail.": "chat",
		"move.": "navigation",
		"nav.": "navigation",
		"sector.": "navigation",
		"combat.": "combat",
		"attack.": "combat",
		"trade.": "trade",
		"market.": "trade",
		"connection.": "connection",
		"client.": "connection",
	}
	for prefix in prefixes:
		if event_type.begins_with(prefix):
			return prefixes[prefix]
	return "unknown"

static func redact(value):
	if value is Dictionary:
		var result := {}
		for key in value:
			var key_text := str(key)
			if SECRET_KEYS.has(key_text.to_lower()):
				result[key] = REDACTED
			else:
				result[key] = redact(value[key])
		return result
	if value is Array:
		var result := []
		for item in value:
			result.append(redact(item))
		return result
	return value

static func diagnostic_json(value) -> String:
	return JSON.stringify(redact(value))
