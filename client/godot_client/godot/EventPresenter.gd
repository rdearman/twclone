extends RefCounted

static func notification_for(event: Dictionary) -> String:
	var event_type := str(event.get("type", ""))
	var data = event.get("data", {})
	if event_type.is_empty() or not data is Dictionary:
		return ""
	if event_type == "system.notice":
		var title := str(data.get("title", "Server notice")).strip_edges()
		var body := str(data.get("body", "")).strip_edges()
		return title + (" · " + body if not body.is_empty() else "")
	var category := str(event.get("category", "unknown"))
	if category not in ["combat", "navigation", "trade", "chat"]:
		return ""
	var readable_type := event_type.replace(".", " · ").replace("_", " ").capitalize()
	var summary := str(data.get("body", data.get("message", data.get("title", "")))).strip_edges()
	return readable_type + (" · " + summary if not summary.is_empty() else "")
