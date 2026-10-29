extends RefCounted

static func notification_for(event: Dictionary) -> String:
	var event_type := str(event.get("type", ""))
	var data = event.get("data", {})
	if event_type.is_empty() or not data is Dictionary:
		return ""
	if event_type in ["system.notice", "system.notice_v1"]:
		var title := str(data.get("title", "Server notice")).strip_edges()
		var body := str(data.get("body", "")).strip_edges()
		return title + (" · " + body if not body.is_empty() else "")
	if event_type == "sector.notice":
		return _sector_notice(data)
	var category := str(event.get("category", "unknown"))
	if category not in ["combat", "navigation", "trade", "chat"]:
		return ""
	var readable_type := event_type.replace(".", " · ").replace("_", " ").capitalize()
	var summary := str(data.get("body", data.get("message", data.get("title", "")))).strip_edges()
	return readable_type + (" · " + summary if not summary.is_empty() else "")

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
