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
		var error_data: Variant = error_value.get("data", {})
		if error_data is Dictionary:
			var nested_message := str(error_data.get("message", ""))
			if not nested_message.is_empty():
				return nested_message
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

## Accept both the documented move.result field and the server's legacy
## destination aliases. Godot decodes JSON numbers as floats in some runtime
## paths, so an integral float is valid wire data; fractional values and
## strings remain schema-mismatched and cannot confirm movement.
static func confirmed_move_sector(data: Variant) -> int:
	if not data is Dictionary:
		return 0
	for key in ["current_sector", "to_sector_id", "sector_id"]:
		var value: Variant = data.get(key)
		if typeof(value) == TYPE_INT and int(value) > 0:
			return value
		if typeof(value) == TYPE_FLOAT and value > 0.0 and is_equal_approx(value, round(value)):
			return int(value)
	return 0

## `move.transwarp` has one authoritative success destination field. Keep this
## stricter than the shared warp compatibility helper so an unrelated movement
## alias cannot authorize a transwarp completion.
static func confirmed_transwarp_sector(data: Variant) -> int:
	if not data is Dictionary:
		return 0
	var value: Variant = data.get("sector_id")
	if typeof(value) == TYPE_INT and int(value) > 0:
		return int(value)
	return 0

static func move_hazard_report(frame: Dictionary) -> String:
	var envelope_data: Variant = frame.get("data", {})
	var data: Variant = envelope_data
	var error_data: Variant = {}
	var error: Variant = frame.get("error", {})
	if error is Dictionary and error.get("data", {}) is Dictionary:
		error_data = error.get("data", {})
	# Some hazard refusals carry an empty success-shaped data object and the
	# actual outcome in error.data. Prefer the richer server payload without
	# merging or inferring fields that were not supplied.
	if not data is Dictionary:
		data = {}
	var data_hazards: Variant = data.get("hazards", null)
	var error_hazards: Variant = error_data.get("hazards", null) if error_data is Dictionary else null
	# A malformed success-shaped field must not hide a valid authoritative
	# report carried by error.data. Only arrays are usable hazard lists.
	if (not data_hazards is Array
			or (data_hazards.is_empty() and error_hazards is Array and not error_hazards.is_empty())) and error_hazards is Array:
		data = error_data
	var hazards: Variant = data.get("hazards", []) if data is Dictionary else []
	if not hazards is Array:
		return ""
	var lines: Array[String] = []
	for hazard in hazards:
		if not hazard is Dictionary:
			continue
		# Hazard identity is server-owned presentation data. Do not coerce a
		# malformed boolean/number/object into a visible hazard label.
		var raw_hazard_type: Variant = hazard.get("hazard_type", hazard.get("type", null))
		if not (raw_hazard_type is String) or raw_hazard_type.strip_edges().is_empty():
			continue
		var hazard_type: String = raw_hazard_type
		var parts: Array[String] = [hazard_type]
		for field in [["severity", "severity"], ["damage", "damage"], ["shields_lost", "shields lost"], ["fighters_lost", "fighters lost"], ["hull_lost", "hull lost"]]:
			if not hazard.has(field[0]):
				continue
			var value: Variant = hazard[field[0]]
			# Preserve only the server's documented scalar type. In particular,
			# do not render bools or strings as numeric damage values.
			if field[0] == "severity":
				if value is String and not value.strip_edges().is_empty():
					parts.append("%s: %s" % [field[1], value])
			elif typeof(value) == TYPE_INT and int(value) >= 0:
				# The server hazard schema emits loss counters as JSON integers.
				# Do not let a schema-mismatched float become a displayed
				# authoritative damage value. Negative loss counters are also
				# schema-invalid; Python and Godot must fail closed identically at
				# this reconciliation boundary.
				parts.append("%s: %s" % [field[1], str(value)])
		for field in [["destroyed", "destroyed"], ["ship_remains_in_sector", "ship remains in sector"]]:
			if hazard.has(field[0]) and typeof(hazard[field[0]]) == TYPE_BOOL:
				parts.append("%s: %s" % [field[1], str(hazard[field[0]]).to_lower()])
		if hazard.has("message") and hazard["message"] is String and not hazard["message"].strip_edges().is_empty():
			parts.append(hazard["message"])
		lines.append("Sector hazard · " + " · ".join(parts))
	# Destructive entry responses may carry this terminal state beside the
	# hazard array (or without one). Display only the field supplied by the
	# server; Main.gd still reconciles authoritative state before another act.
	# A partial/error envelope may put different terminal fields in `data` and
	# `error.data`. Preserve each authoritative field independently; an explicit
	# value in `data` wins only for that same field.
	var terminal_sources: Array[Dictionary] = []
	# Keep the original envelope separate from the selected hazard source. A
	# partial refusal may put hazards in error.data while terminal state remains
	# in data; selecting the richer hazard payload must not discard that field.
	if envelope_data is Dictionary:
		terminal_sources.append(envelope_data)
	if data is Dictionary and data != envelope_data:
		terminal_sources.append(data)
	if error_data is Dictionary and not terminal_sources.has(error_data):
		terminal_sources.append(error_data)
	for field in [["sector_id", "reported sector"], ["ship_destroyed", "ship destroyed"], ["ship_remains_in_sector", "ship remains in sector"]]:
		for source in terminal_sources:
			var value: Variant = source.get(field[0])
			var valid: bool = (field[0] == "sector_id" and typeof(value) == TYPE_INT and value > 0) or (field[0] != "sector_id" and typeof(value) == TYPE_BOOL)
			if valid:
				lines.append("Movement outcome · %s: %s" % [field[1], str(value).to_lower()])
				break
	return "\n".join(lines)

## Return only terminal movement facts explicitly supplied by the server.
## Hazard presentation may combine data/error.data for display, but callers
## that decide whether another movement is safe must not infer destruction or
## relocation from a missing field or from a rendered string.
static func move_terminal_outcome(frame: Dictionary) -> Dictionary:
	var sources: Array[Dictionary] = []
	var data: Variant = frame.get("data", {})
	if data is Dictionary:
		sources.append(data)
	var error: Variant = frame.get("error", {})
	if error is Dictionary and error.get("data", {}) is Dictionary:
		sources.append(error.get("data", {}))
	var outcome := {}
	for field in ["ship_destroyed", "ship_remains_in_sector"]:
		for source in sources:
			# JSON booleans are authoritative; do not coerce 0/1 or strings.
			if source.has(field) and typeof(source[field]) == TYPE_BOOL:
				outcome[field] = source[field]
				break
	# A server may attach the same explicit terminal booleans to the hazard
	# row. Preserve that authority for callers that decide whether the current
	# scene remains actionable; do not infer terminal state from damage values.
	for source in sources:
		var hazards: Variant = source.get("hazards", [])
		if not hazards is Array:
			continue
		for hazard in hazards:
			if not hazard is Dictionary:
				continue
			if hazard.get("destroyed") == true and not outcome.has("ship_destroyed"):
				outcome["ship_destroyed"] = true
			if (typeof(hazard.get("ship_remains_in_sector")) == TYPE_BOOL
					and not outcome.has("ship_remains_in_sector")):
				outcome["ship_remains_in_sector"] = hazard["ship_remains_in_sector"]
	return outcome

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
		"broadcast.message_v1": "chat",
		"chat.": "chat",
		"comm.": "chat",
		"mail.": "chat",
		"tavern.": "chat",
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
			var normalized_key := key_text.to_lower()
			if SECRET_KEYS.has(normalized_key) or normalized_key.ends_with("_password") or normalized_key.ends_with("_passwd"):
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
