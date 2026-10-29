class_name TwFerengiPresenter
extends RefCounted

## Presents only the current-sector trader roster and open server offers.
## The server remains authoritative for trader location and deal terms.

static func traders(data: Dictionary) -> String:
	var sector_id: Variant = data.get("sector_id", "unavailable")
	var lines: Array[String] = ["FERENGI TRADERS · SECTOR %s" % str(sector_id)]
	var traders: Variant = data.get("traders", [])
	if traders is Array and not traders.is_empty():
		for value in traders:
			if not value is Dictionary:
				continue
			lines.append("%s · %s · trader %s · reputation %s" % [
				str(value.get("display_name", "Unnamed trader")),
				str(value.get("trader_code", "unknown")),
				str(value.get("trader_id", "unknown")),
				str(value.get("reputation", "unknown")),
			])
	else:
		lines.append("No active Ferengi traders are present in this sector.")
	var offers: Variant = data.get("offers", [])
	lines.append("")
	lines.append("OPEN OFFERS")
	if not offers is Array or offers.is_empty():
		lines.append("No open offers.")
	else:
		for offer in offers:
			if not offer is Dictionary:
				continue
			var side := "direction unavailable"
			if str(offer.get("side", "")) == "trader_sells":
				side = "you buy"
			elif str(offer.get("side", "")) == "trader_buys":
				side = "you sell"
			lines.append("Deal %s · %s: %s %s %s at %s CR each · trader sector %s · expires %s" % [
				str(offer.get("deal_id", "unknown")),
				str(offer.get("display_name", "Ferengi trader")),
				side,
				str(offer.get("quantity", "?")),
				str(offer.get("commodity", "unknown")),
				str(offer.get("unit_price", "?")),
				str(offer.get("trader_sector_id", "unknown")),
				str(offer.get("expires_at", "unknown")),
			])
	return "\n".join(lines)

static func deal_result(data: Dictionary) -> String:
	return "Deal %s · %s %s %s at %s CR each · reputation %s" % [
		str(data.get("deal_id", "unknown")),
		str(data.get("status", "updated")),
		str(data.get("quantity", "?")),
		str(data.get("commodity", "unknown")),
		str(data.get("unit_price", "?")),
		str(data.get("reputation", "unknown")),
	]
