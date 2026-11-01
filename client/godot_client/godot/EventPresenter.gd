extends RefCounted

static func targets_player_ship_destruction(event: Dictionary, player_id: int) -> bool:
	# Destruction aftermath is server-owned.  Only an explicit victim identity
	# may trigger reconciliation; damage, hull, or event category are not enough.
	if player_id <= 0:
		return false
	var event_type := str(event.get("type", ""))
	if event_type not in ["ship.destroyed", "combat.ship_destroyed"]:
		return false
	var data = event.get("data", {})
	if not data is Dictionary:
		return false
	var victim_key := "player_id" if event_type == "ship.destroyed" else "victim_player_id"
	var victim_id: Variant = data.get(victim_key, null)
	return typeof(victim_id) == TYPE_INT and int(victim_id) == player_id

static func notification_for(event: Dictionary) -> String:
	var event_type := str(event.get("type", ""))
	var data = event.get("data", {})
	if event_type.is_empty() or not data is Dictionary:
		return ""
	if event_type in ["system.notice", "system.notice_v1"]:
		var title := str(data.get("title", "Server notice")).strip_edges()
		var body := str(data.get("body", "")).strip_edges()
		return title + (" · " + body if not body.is_empty() else "")
	if event_type == "broadcast.message_v1":
		var message: Variant = data.get("message", null)
		if not (message is String) or message.strip_edges().is_empty():
			return ""
		return "Broadcast · " + message.strip_edges()
	if event_type == "sector.notice":
		return _sector_notice(data)
	var category := str(event.get("category", "unknown"))
	if category not in ["combat", "navigation", "trade", "chat"]:
		return ""
	if category == "combat" and event_type == "combat.hit":
		return _combat_hit(data)
	if category == "combat" and event_type == "player.attack_planet.v1":
		return _planet_attack(data)
	if category == "combat" and event_type in ["combat.hit.fighters", "combat.hit.quasar", "combat.hit.environment", "combat.limpet.attached", "combat.planet_landing_defense.v1"]:
		return _defense_hit(event_type, data)
	if category == "chat":
		var sender := str(data.get("sender_name", data.get("from", data.get("username", "")))).strip_edges()
		var message := str(data.get("message", data.get("body", data.get("subject", "")))).strip_edges()
		if not sender.is_empty():
			return "Comms · " + sender + (": " + message if not message.is_empty() else "")
		return "Comms · " + message if not message.is_empty() else "Comms · " + event_type.replace(".", " · ").replace("_", " ").capitalize()
	var readable_type := event_type.replace(".", " · ").replace("_", " ").capitalize()
	var summary := str(data.get("body", data.get("message", data.get("title", "")))).strip_edges()
	return readable_type + (" · " + summary if not summary.is_empty() else "")

static func _planet_attack(data: Dictionary) -> String:
	var summary := "Planet attack"
	var planet_id: Variant = data.get("planet_id", null)
	if planet_id is int and planet_id > 0:
		summary += " · planet %d" % planet_id
	var result: Variant = data.get("result", null)
	if result is String and not result.strip_edges().is_empty():
		summary += " · result: %s" % result.strip_edges()
	var ship_loss: Variant = data.get("ship_loss", null)
	if ship_loss is int and ship_loss >= 0:
		summary += " · ship fighters lost: %d" % ship_loss
	var planet_loss: Variant = data.get("planet_loss", null)
	if planet_loss is int and planet_loss >= 0:
		summary += " · planet fighters lost: %d" % planet_loss
	return "Combat · " + summary

static func _combat_hit(data: Dictionary) -> String:
	var weapon := str(data.get("weapon", "")).strip_edges()
	var summary := "%s hit" % weapon if not weapon.is_empty() else "Ship hit"
	var damage: Variant = data.get("damage", null)
	var hp_after: Variant = data.get("defender_hp_after", null)
	var sector_id: Variant = data.get("sector_id", null)
	if damage is int:
		summary += " · %d damage" % damage
	if hp_after is int:
		summary += " · target HP %d" % hp_after
	if sector_id is int and sector_id > 0:
		summary += " · sector %d" % sector_id
	return "Combat · " + summary

static func _defense_hit(event_type: String, data: Dictionary) -> String:
	var summary := ""
	if event_type == "combat.planet_landing_defense.v1":
		summary = "Planetary defense"
		var result: Variant = data.get("result", null)
		if result is String and not result.strip_edges().is_empty():
			summary += " · " + result.strip_edges()
		for field in [["attacker_fighters_lost", "attacker fighters lost"], ["attacker_fighters_remaining", "attacker fighters remaining"], ["planet_fighters_lost", "planet fighters lost"], ["planet_fighters_remaining", "planet fighters remaining"], ["citadel_shields_absorbed", "citadel shields absorbed"], ["citadel_shields_remaining", "citadel shields remaining"]]:
			if _authoritative_int(data.get(field[0], null)):
				summary += " · %s: %d" % [field[1], int(data[field[0]])]
	elif event_type == "combat.hit.fighters":
		summary = "Defensive fighters engaged"
		if _authoritative_int(data.get("fighters_engaged", null)):
			summary += " · %d engaged" % int(data["fighters_engaged"])
	elif event_type == "combat.hit.quasar":
		var source: Variant = data.get("source", null)
		summary = source.strip_edges() if source is String and not source.strip_edges().is_empty() else "Atmospheric defense"
	elif event_type == "combat.limpet.attached":
		summary = "Limpet attached"
	else:
		var hazard: Variant = data.get("hazard_type", data.get("type", ""))
		summary = "Environmental hazard · %s" % str(hazard) if hazard is String and not str(hazard).strip_edges().is_empty() else "Environmental hazard"
	for field in [["damage", "damage"], ["shields_lost", "shields lost"], ["fighters_lost", "fighters lost"], ["hull_lost", "hull lost"]]:
		if _authoritative_int(data.get(field[0], null)):
			summary += " · %s: %d" % [field[1], int(data[field[0]])]
	if data.get("destroyed") == true:
		summary += " · ship destroyed"
	return "Combat · " + summary

static func _authoritative_int(value: Variant) -> bool:
	# JSON booleans and strings are not interchangeable with server counters.
	return typeof(value) == TYPE_INT and typeof(value) != TYPE_BOOL

static func _sector_notice(data: Dictionary) -> String:
	var subtype := str(data.get("subtype", ""))
	var labels := {
		"docked": "Ship docked at a port",
		"beacon_set": "Sector beacon set",
		"beacon_destroyed": "Sector beacon destroyed",
		"fighters_deployed": "Sector fighters deployed",
		"mine_swept": "Sector mines swept",
	}
	var summary := str(labels.get(subtype, "Sector notice"))
	var sector_id: Variant = data.get("sector_id")
	if sector_id is int and sector_id > 0:
		summary += " in sector %d" % sector_id
	var details: Variant = data.get("details", {})
	if details is Dictionary:
		match subtype:
			"docked":
				if details.get("port_id") is int:
					summary += " (port %s)" % str(details["port_id"])
			"beacon_set":
				if details.get("beacon") is String and not str(details["beacon"]).strip_edges().is_empty():
					summary += ": " + str(details["beacon"]).strip_edges()
			"fighters_deployed":
				if details.get("count") is int:
					summary += " (%s)" % str(details["count"])
			"mine_swept":
				if details.get("mines_removed") is int:
					summary += " (%s removed)" % str(details["mines_removed"])
	return summary
