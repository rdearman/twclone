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
	var normalized := normalize_ship(data)
	ship = normalized
	ship_availability = FRESH
	_emit_changed()
	return true

func accept_sector(data: Dictionary, generation: int) -> bool:
	if not authenticated or disconnected or generation != refresh_generation:
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
	if data.is_empty() or not data.has("sector_id") or not _is_integral_number(data["sector_id"]):
		return {}
	var result := {}
	_copy_int(data, result, "sector_id", "id")
	_copy_string(data, result, "name")
	_copy_string(data, result, "beacon")
	var adjacent = data.get("adjacent_sectors", data.get("adjacent", null))
	if adjacent is Array:
		var adjacent_ids := []
		for item in adjacent:
			var destination = item.get("to_sector") if item is Dictionary else item
			if _is_integral_number(destination):
				adjacent_ids.append(int(destination))
		result["adjacent_sector_ids"] = adjacent_ids
	result["ports"] = _normalize_entities(data.get("ports", []), ["id", "port_id", "name", "type", "class"])
	result["planets"] = _normalize_entities(data.get("celestial_objects", data.get("planets", [])), ["id", "planet_id", "name", "type"])
	result["ships"] = _normalize_entities(data.get("ships_present", data.get("ships", [])), ["id", "ship_id", "name", "ship_name", "owner"])
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
			if _is_integral_number(field):
				item[key] = int(field)
			elif typeof(field) == TYPE_STRING:
				item[key] = field
		if not item.is_empty():
			result.append(item)
	return result

static func _has_object(data: Dictionary, key: String) -> bool:
	return data.has(key) and data[key] is Dictionary

static func _copy_int(source: Dictionary, target: Dictionary, source_key: String, target_key: String = "") -> void:
	var key := target_key if not target_key.is_empty() else source_key
	if source.has(source_key) and _is_integral_number(source[source_key]):
		target[key] = int(source[source_key])

static func _is_integral_number(value: Variant) -> bool:
	if typeof(value) == TYPE_INT:
		return true
	return typeof(value) == TYPE_FLOAT and is_finite(value) and value == floor(value)

static func _copy_string(source: Dictionary, target: Dictionary, key: String) -> void:
	if source.has(key) and source[key] is String:
		target[key] = source[key]

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
