class_name TwPlanetCombatPresenter
extends RefCounted

## Formats only server-confirmed planetary combat outcomes. It never estimates
## damage or determines whether an attack succeeds.

static func landing_refusal(response: Dictionary) -> Dictionary:
	return _defense_report(response, "Landing was refused; the server did not grant planetary access.")

static func attack_refusal(response: Dictionary) -> Dictionary:
	return _defense_report(response, "Planet attack was refused; the server did not confirm a planetary attack.")

static func _defense_report(response: Dictionary, closing_message: String) -> Dictionary:
	# The gateway may carry the authoritative combat report in either envelope
	# location. Preserve both, with error.data taking precedence for repeated
	# fields, and never derive a report from the refusal status alone.
	var response_data: Variant = response.get("data", {})
	var error: Variant = response.get("error", {})
	var details: Dictionary = {}
	if response_data is Dictionary:
		details = response_data.duplicate(true)
	var error_data: Variant = error.get("data", {}) if error is Dictionary else {}
	if error_data is Dictionary:
		for key in error_data:
			details[key] = error_data[key]
	if details.is_empty():
		return {"has_combat": false, "message": ""}
	var combat: Variant = details.get("combat", [])
	if not (combat is Array):
		combat = []
	if combat.is_empty() and details.get("ship_remains_in_sector", true) != false:
		return {"has_combat": false, "message": ""}
	var lines: Array[String] = ["Planetary defense report:"]
	for value in combat:
		if not (value is Dictionary):
			continue
		if str(value.get("defense", "")) == "atmosphere_quasar":
			var text := "Atmospheric Quasar · %s damage; shields lost %s, hull lost %s; remaining shields %s, hull %s" % [
				_reported_int(value.get("damage")), _reported_int(value.get("shields_lost")),
				_reported_int(value.get("hull_lost")), _reported_int(value.get("shields_remaining")),
				_reported_int(value.get("hull_remaining")),
			]
			if value.get("destroyed") is bool and value["destroyed"] == true:
				text += " · ship destroyed"
			lines.append(text)
		else:
			var text := "Planetary fighters %s · ship lost %s fighters (%s remain); planet lost %s (%s remain); citadel shields absorbed %s (%s remain)" % [
				str(value.get("result", "engaged")), _reported_int(value.get("attacker_fighters_lost")),
				_reported_int(value.get("attacker_fighters_remaining")),
				_reported_int(value.get("planet_fighters_lost")),
				_reported_int(value.get("planet_fighters_remaining")),
				_reported_int(value.get("citadel_shields_absorbed")),
				_reported_int(value.get("citadel_shields_remaining")),
			]
			if value.get("destroyed") is bool and value["destroyed"] == true:
				text += " · ship destroyed"
			lines.append(text)
	if details.get("ship_remains_in_sector") == false:
		lines.append("Server reports that your ship did not remain in the sector.")
	lines.append(closing_message)
	return {"has_combat": true, "message": "\n".join(lines), "data": details}

static func _reported_int(value: Variant) -> String:
	# JSON booleans, strings, and floats are not interchangeable with the
	# server's integer combat counters. Keep malformed/missing fields visible as
	# unavailable instead of presenting a client-coerced combat result.
	if value is int:
		return str(value)
	return "unknown"

static func attack_result(data: Dictionary) -> String:
	# The server owns capture/loss and fighter accounting. Keep malformed
	# fields visibly unavailable instead of stringifying a schema mismatch into
	# a gameplay claim.
	var planet_id := _reported_positive_int(data.get("planet_id"))
	var captured := _reported_bool(data.get("captured"))
	var attacker_remaining := _reported_nonnegative_int(data.get("attacker_remaining_fighters"))
	var defender_remaining := _reported_nonnegative_int(data.get("defender_remaining_fighters"))
	return "Planet %s · captured: %s · your fighters remaining: %s · planet fighters remaining: %s" % [
		planet_id, captured, attacker_remaining, defender_remaining,
	]

static func _reported_positive_int(value: Variant) -> String:
	return str(value) if value is int and value > 0 else "not reported"

static func _reported_nonnegative_int(value: Variant) -> String:
	return str(value) if value is int and value >= 0 else "not reported"

static func _reported_bool(value: Variant) -> String:
	return str(value) if value is bool else "not reported"
