class_name TwClientState
extends RefCounted

## Client-owned, normalized state. It never stores protocol envelopes.

signal changed(snapshot: Dictionary)

const Protocol = preload("res://Protocol.gd")

const UNAVAILABLE := "unavailable"
const REFRESHING := "refreshing"
const FRESH := "fresh"
const PARTIAL := "partially available"
const STALE := "stale"
const DISCONNECTED := "disconnected"

var player: Dictionary = {}
var ship: Dictionary = {}
var sector: Dictionary = {}
var disconnected := true
var authenticated := false
var refresh_generation: int = 0
var player_availability := UNAVAILABLE
var ship_availability := UNAVAILABLE
var sector_availability := UNAVAILABLE
var last_refresh_generation: int = 0

func mark_authenticated() -> void:
	authenticated = true
	disconnected = false
	player_availability = STALE if not player.is_empty() else UNAVAILABLE
	ship_availability = STALE if not ship.is_empty() else UNAVAILABLE
	sector_availability = STALE if not sector.is_empty() else UNAVAILABLE
	_emit_changed()

func mark_disconnected() -> void:
	authenticated = false
	disconnected = true
	player_availability = DISCONNECTED
	ship_availability = DISCONNECTED
	sector_availability = DISCONNECTED
	_emit_changed()

func clear_session_state() -> void:
	player.clear()
	ship.clear()
	sector.clear()
	authenticated = false
	disconnected = true
	refresh_generation = 0
	last_refresh_generation = 0
	player_availability = UNAVAILABLE
	ship_availability = UNAVAILABLE
	sector_availability = UNAVAILABLE
	_emit_changed()

func begin_refresh(generation: int, requested_domains: Array = ["player", "ship", "sector"]) -> bool:
	if not authenticated or disconnected:
		return false
	refresh_generation = generation
	player_availability = _refresh_availability("player", requested_domains, player)
	ship_availability = _refresh_availability("ship", requested_domains, ship)
	sector_availability = _refresh_availability("sector", requested_domains, sector)
	_emit_changed()
	return true

func _refresh_availability(domain: String, requested_domains: Array, value: Dictionary) -> String:
	if domain in requested_domains:
		return REFRESHING
	return STALE if not value.is_empty() else UNAVAILABLE

func accept_player(data: Dictionary, generation: int) -> bool:
	if not authenticated or disconnected or generation != refresh_generation or not _has_object(data, "player"):
		return false
	var normalized := normalize_player(data)
	if normalized.is_empty() and not data["player"].is_empty():
		return false
	player = normalized
	player_availability = FRESH
	_emit_changed()
	return true

func accept_ship(data: Dictionary, generation: int) -> bool:
	if not authenticated or disconnected or generation != refresh_generation or not data.has("ship") or not (data["ship"] is Dictionary):
		return false
	# When the player refresh has identified an active ship, a later ship
	# response is authoritative only if it describes that same server-owned
	# object.  Never replace usable state with a well-formed response for a
	# different ship (for example, a delayed reply from a previous request).
	var expected_ship_id: Variant = player.get("ship_id", null)
	var actual_ship_id: Variant = data["ship"].get("id", data["ship"].get("ship_id", null))
	# player.my_info is the authoritative relationship between the session and
	# its active ship.  Do not accept a well-formed ship.status for an
	# unconfirmed ship when that relationship is missing or malformed; this can
	# otherwise replace the HUD with a delayed response for another ship after a
	# movement, hazard, or reconnect refresh.
	if not _is_authoritative_id(expected_ship_id):
		return false
	if not _is_authoritative_id(actual_ship_id) or int(actual_ship_id) != int(expected_ship_id):
		return false
	# ship.status/ship.info establish the active ship used by subsequent
	# actions.  Do not mark the domain fresh when the server omitted or
	# malformed that identity; an incomplete record must not hide stale hull,
	# cargo, or equipment state.
	if not _has_authoritative_ship_id(data["ship"]):
		return false
	var normalized := normalize_ship(data)
	ship = normalized
	ship_availability = FRESH
	_emit_changed()
	return true

func accept_sector(data: Dictionary, generation: int) -> bool:
	if not authenticated or disconnected or generation != refresh_generation or not _has_authoritative_sector_id(data):
		return false
	# player.my_info is the authoritative session/location binding.  Once it
	# has supplied a valid sector, a delayed sector.info response for another
	# sector must not replace the confirmed visibility state.
	var expected_sector_id: Variant = player.get("sector_id", null)
	if _is_authoritative_id(expected_sector_id):
		if int(data["sector_id"]) != int(expected_sector_id):
			return false
	var normalized := normalize_sector(data)
	if normalized.is_empty() and not data.is_empty():
		return false
	sector = normalized
	sector_availability = FRESH
	_emit_changed()
	return true

func record_failure(domain: String, generation: int) -> bool:
	if generation != refresh_generation:
		return false
	var status := PARTIAL
	match domain:
		"player":
			player_availability = status
		"ship":
			ship_availability = status
		"sector":
			sector_availability = status
		_:
			return false
	_emit_changed()
	return true

func finish_refresh(generation: int) -> Dictionary:
	if generation != refresh_generation:
		return snapshot()
	last_refresh_generation = generation
	_emit_changed()
	return snapshot()

func apply_response(command: String, response: Dictionary) -> bool:
	var result: Dictionary = Protocol.result_from_response(response)
	if result["kind"] != "ok":
		return false
	var data = response.get("data", {})
	if not (data is Dictionary):
		return false
	var generation := refresh_generation
	match command:
		"player.my_info":
			return accept_player(data, generation)
		"ship.status", "ship.info":
			return accept_ship(data, generation)
		"sector.info":
			return accept_sector(data, generation)
	return false

func snapshot() -> Dictionary:
	return {
		"player": player.duplicate(true),
		"ship": ship.duplicate(true),
		"sector": sector.duplicate(true),
		"hud": hud_snapshot(),
		"freshness": {
			"overall": overall_availability(),
			"player": player_availability,
			"ship": ship_availability,
			"sector": sector_availability,
		},
		"authenticated": authenticated,
		"disconnected": disconnected,
		"refresh_generation": refresh_generation,
		"last_refresh_generation": last_refresh_generation,
	}

func hud_snapshot() -> Dictionary:
	return {
		"player_name": player.get("username", null),
		"credits": player.get("credits", null),
		"turns_remaining": player.get("turns_remaining", null),
		"sector_id": sector.get("id", player.get("sector_id", null)),
		"sector_name": sector.get("name", null),
		"ship_id": ship.get("id", player.get("ship_id", null)),
		"ship_name": ship.get("name", null),
		"cargo_used": ship.get("cargo_used", null),
		"cargo_total": ship.get("holds", null),
		"fighters": ship.get("fighters", null),
		"shields": ship.get("shields", null),
		"hull": ship.get("hull", null),
	}

func overall_availability() -> String:
	if disconnected:
		return DISCONNECTED
	var statuses := [player_availability, ship_availability, sector_availability]
	if REFRESHING in statuses:
		return REFRESHING
	if PARTIAL in statuses:
		return PARTIAL
	if STALE in statuses:
		return STALE
	if FRESH in statuses:
		return FRESH
	return UNAVAILABLE

static func normalize_player(data: Dictionary) -> Dictionary:
	if not _has_object(data, "player"):
		return {}
	var source: Dictionary = data["player"]
	var result := {}
	_copy_int(source, result, "id")
	_copy_string(source, result, "username")
	_copy_money(source, result, "credits")
	_copy_int(source, result, "turns_remaining")
	_copy_int(source, result, "sector", "sector_id")
	_copy_int(source, result, "ship_id")
	_copy_int(source, result, "corp_id")
	_copy_int(source, result, "alignment")
	_copy_int(source, result, "experience")
	return result

static func normalize_ship(data: Dictionary) -> Dictionary:
	if not data.has("ship") or not (data["ship"] is Dictionary):
		return {}
	var source: Dictionary = data["ship"]
	var result := {}
	_copy_int(source, result, "id")
	_copy_string(source, result, "name")
	_copy_int(source, result, "type_id")
	_copy_int(source, result, "holds")
	_copy_int(source, result, "fighters")
	_copy_int(source, result, "shields")
	if source.has("has_long_range_scanner") and typeof(source["has_long_range_scanner"]) == TYPE_BOOL:
		result["has_long_range_scanner"] = source["has_long_range_scanner"]
	if source.has("hull") and _is_integral_number(source["hull"]) and source["hull"] >= 0 and source["hull"] <= 100:
		result["hull"] = int(source["hull"])
	_copy_int(source, result, "onplanet")
	_copy_int(source, result, "ported")
	if source.has("cargo") and source["cargo"] is Array:
		var cargo := []
		var used := 0
		for item in source["cargo"]:
			if not (item is Dictionary):
				continue
			var quantity = item.get("quantity", item.get("qty", null))
			if not _is_integral_number(quantity) or quantity < 0:
				continue
			var commodity = item.get("commodity", item.get("code", null))
			if not (commodity is String):
				continue
			var normalized_quantity := int(quantity)
			cargo.append({"commodity": commodity, "quantity": normalized_quantity})
			used += normalized_quantity
		result["cargo"] = cargo
		result["cargo_used"] = used
	return result

static func normalize_sector(data: Dictionary) -> Dictionary:
	if data.is_empty() or not data.has("sector_id") or not _is_authoritative_id(data["sector_id"]):
		return {}
	var result := {}
	_copy_int(data, result, "sector_id", "id")
	_copy_string(data, result, "name")
	# Keep the authoritative sector usable when the optional display name is
	# absent or schema-mismatched.  This mirrors the Python client: derive only
	# presentation text from the already-validated server ID; never derive an ID
	# from a name or accept malformed visibility metadata.
	if not result.has("name") and result.has("id"):
		result["name"] = "Sector %d" % int(result["id"])
	# Protocol v3 calls this field beacon. Accept beacon_text only as a
	# defensive read of older server envelopes; presentation still gets the
	# value from this normalized authoritative sector record.
	_copy_string(data, result, "beacon")
	if not result.has("beacon"):
		_copy_string(data, result, "beacon_text", "beacon")
	var adjacent_known := data.has("adjacent_sectors") or data.has("adjacent")
	var adjacent = data.get("adjacent_sectors", data.get("adjacent", null))
	if adjacent is Array:
		var adjacent_ids := []
		for item in adjacent:
			var destination = item.get("to_sector") if item is Dictionary else item
			if _is_authoritative_id(destination):
				adjacent_ids.append(int(destination))
		result["adjacent_sector_ids"] = adjacent_ids
	var ports_known := data.has("ports") or data.has("port")
	var ports: Array = []
	if data.get("ports") is Array:
		ports = _normalize_entities(data["ports"], ["id", "port_id", "name"])
	elif data.get("port") is Dictionary:
		var port: Dictionary = data["port"]
		# Visibility metadata is authoritative only when the server supplied a
		# JSON boolean.  In particular, bool("false") is true in GDScript and
		# would turn a malformed/refused disclosure into an actionable port.
		var raw_present: Variant = port.get("present", true)
		var port_present: bool = typeof(raw_present) == TYPE_BOOL and raw_present
		if port_present:
			var normalized_port := {}
			_copy_authoritative_id(port, normalized_port, "id")
			_copy_authoritative_id(port, normalized_port, "port_id")
			_copy_string(port, normalized_port, "name")
			if not normalized_port.is_empty():
				ports.append(normalized_port)
	result["ports"] = ports
	result["planets"] = _normalize_entities(data.get("celestial_objects", data.get("planets", [])), ["id", "planet_id", "name", "type", "class", "type_name"])
	result["ships"] = _normalize_entities(data.get("ships_present", data.get("ships", [])), ["id", "ship_id", "name", "ship_name", "owner"])
	result["knowledge"] = {
		"adjacent": adjacent_known,
		"ports": ports_known,
		"planets": data.has("celestial_objects") or data.has("planets"),
		"ships": data.has("ships_present") or data.has("ships"),
	}
	if data.has("counts") and data["counts"] is Dictionary:
		var counts := {}
		_copy_int(data["counts"], counts, "fighters")
		_copy_int(data["counts"], counts, "mines")
		result["counts"] = counts
	_copy_int(data, result, "server_tick")
	return result

static func _normalize_entities(value, allowed: Array) -> Array:
	var result := []
	if not (value is Array):
		return result
	for source in value:
		if not (source is Dictionary):
			continue
		var item := {}
		for key in allowed:
			if not source.has(key):
				continue
			var field = source[key]
			if key in ["id", "planet_id", "port_id", "ship_id"] and _is_authoritative_id(field):
				item[key] = int(field)
			elif key in ["id", "planet_id", "port_id", "ship_id"]:
				continue
			elif _is_integral_number(field):
				item[key] = int(field)
			elif typeof(field) == TYPE_STRING:
				item[key] = field
		if not item.is_empty():
			result.append(item)
	return result

static func _has_object(data: Dictionary, key: String) -> bool:
	return data.has(key) and data[key] is Dictionary

static func _has_authoritative_sector_id(data: Dictionary) -> bool:
	# Refresh responses establish the location used by every proximity-sensitive
	# action. Godot parses JSON numbers as floats, so integral floats are valid
	# wire representations; fractional, string and boolean values remain invalid.
	if not data.has("sector_id") or not _is_authoritative_id(data["sector_id"]):
		return false
	return int(data["sector_id"]) > 0

static func _has_authoritative_ship_id(source: Dictionary) -> bool:
	for key in ["id", "ship_id"]:
		if source.has(key) and _is_authoritative_id(source[key]):
			return true
	return false

static func _copy_int(source: Dictionary, target: Dictionary, source_key: String, target_key: String = "") -> void:
	var key := target_key if not target_key.is_empty() else source_key
	if source.has(source_key) and _is_integral_number(source[source_key]):
		target[key] = int(source[source_key])

static func _copy_authoritative_id(source: Dictionary, target: Dictionary, source_key: String, target_key: String = "") -> void:
	var key := target_key if not target_key.is_empty() else source_key
	if source.has(source_key) and _is_authoritative_id(source[source_key]):
		target[key] = int(source[source_key])

static func _is_strict_authoritative_id(value: Variant) -> bool:
	# Visibility IDs are JSON integer contract fields. Godot may parse generic
	# numeric values as floats, but an integral float is still schema-mismatched
	# here and must not become an actionable port, planet, ship, or warp target.
	return typeof(value) == TYPE_INT and int(value) > 0

static func _is_authoritative_id(value: Variant) -> bool:
	return _is_integral_number(value) and int(value) > 0

static func _is_integral_number(value: Variant) -> bool:
	if typeof(value) == TYPE_INT:
		return true
	return typeof(value) == TYPE_FLOAT and is_finite(value) and value == floor(value)

static func _copy_string(source: Dictionary, target: Dictionary, key: String, target_key: String = "") -> void:
	if source.has(key) and source[key] is String:
		target[target_key if not target_key.is_empty() else key] = source[key]

static func _copy_money(source: Dictionary, target: Dictionary, key: String) -> void:
	if not source.has(key):
		return
	var value = source[key]
	if _is_integral_number(value):
		target[key] = int(value)
		return
	if not (value is String):
		return
	var text: String = value.strip_edges()
	var regex := RegEx.new()
	regex.compile("^-?[0-9]+(?:\\.0+)?$")
	if regex.search(text) != null:
		target[key] = int(text.split(".")[0])

func _emit_changed() -> void:
	changed.emit(snapshot())
