class_name TwPlanetCombatPresenter
extends RefCounted

## Formats only server-confirmed planetary combat outcomes. It never estimates
## damage or determines whether an attack succeeds.

static func landing_refusal(response: Dictionary) -> Dictionary:
	var error: Variant = response.get("error", {})
	var details: Variant = error.get("data", {}) if error is Dictionary else {}
	if not (details is Dictionary):
		return {"has_combat": false, "message": ""}
	var combat: Variant = details.get("combat", [])
	if not (combat is Array) or combat.is_empty():
		return {"has_combat": false, "message": ""}
	var lines: Array[String] = ["Planetary defense report:"]
	for value in combat:
		if not (value is Dictionary):
			continue
		if str(value.get("defense", "")) == "atmosphere_quasar":
			var text := "Atmospheric Quasar · %s damage; shields lost %s, hull lost %s; remaining shields %s, hull %s" % [
				str(value.get("damage", "unknown")), str(value.get("shields_lost", "unknown")),
				str(value.get("hull_lost", "unknown")), str(value.get("shields_remaining", "unknown")),
				str(value.get("hull_remaining", "unknown")),
			]
			if bool(value.get("destroyed", false)):
				text += " · ship destroyed"
			lines.append(text)
		else:
			var text := "Planetary fighters %s · ship lost %s fighters (%s remain); planet lost %s (%s remain); citadel shields absorbed %s (%s remain)" % [
				str(value.get("result", "engaged")), str(value.get("attacker_fighters_lost", "unknown")),
				str(value.get("attacker_fighters_remaining", "unknown")),
				str(value.get("planet_fighters_lost", "unknown")),
				str(value.get("planet_fighters_remaining", "unknown")),
				str(value.get("citadel_shields_absorbed", "unknown")),
				str(value.get("citadel_shields_remaining", "unknown")),
			]
			if bool(value.get("destroyed", false)):
				text += " · ship destroyed"
			lines.append(text)
	if details.get("ship_remains_in_sector") == false:
		lines.append("Server reports that your ship did not remain in the sector.")
	lines.append("Landing was refused; the server did not grant planetary access.")
	return {"has_combat": true, "message": "\n".join(lines), "data": details}

static func attack_result(data: Dictionary) -> String:
	return "Planet %s · captured: %s · your fighters remaining: %s · planet fighters remaining: %s" % [
		str(data.get("planet_id", "not reported")), str(data.get("captured", "not reported")),
		str(data.get("attacker_remaining_fighters", "not reported")),
		str(data.get("defender_remaining_fighters", "not reported")),
	]
