extends SceneTree

const Protocol = preload("res://Protocol.gd")

func _initialize() -> void:
	var destroyed := Protocol.move_hazard_report({
		"error": {"data": {"ship_destroyed": true}}
	})
	assert(destroyed == "Movement outcome · ship destroyed: true")

	var damage := Protocol.move_hazard_report({
		"data": {"hazards": [{"hazard_type": "nebula", "hull_lost": 4}]}
	})
	assert(damage == "Sector hazard · nebula · hull lost: 4")

	var partial := Protocol.move_hazard_report({
		"data": {"hazards": [{"hazard_type": "minefield", "hull_lost": 12}]},
		"error": {"data": {"ship_destroyed": true}}
	})
	assert(partial == "Sector hazard · minefield · hull lost: 12\nMovement outcome · ship destroyed: true")

	var split_payload := Protocol.move_hazard_report({
		"data": {"hazards": [], "ship_destroyed": true},
		"error": {"data": {"hazards": [{"hazard_type": "minefield", "hull_lost": 12}]}}
	})
	assert(split_payload == "Sector hazard · minefield · hull lost: 12\nMovement outcome · ship destroyed: true")

	var malformed_success := Protocol.move_hazard_report({
		"data": {"hazards": {"unexpected": "shape"}},
		"error": {"data": {"hazards": [{"hazard_type": "limpet", "damage": 6}]}}
	})
	assert(malformed_success == "Sector hazard · limpet · damage: 6")

	# Schema-mismatched values must not be presented as authoritative hazard
	# facts. Terminal decisions use the same strict bool contract.
	var malformed_fields := Protocol.move_hazard_report({
		"data": {
			"hazards": [{"hazard_type": "minefield", "damage": "12", "destroyed": 1}],
			"ship_destroyed": "false",
			"sector_id": 17.0,
		}
	})
	assert(malformed_fields == "Sector hazard · minefield")

	var fractional_counter := Protocol.move_hazard_report({
		"data": {"hazards": [{"hazard_type": "minefield", "hull_lost": 12.0}]}
	})
	assert(fractional_counter == "Sector hazard · minefield")
	var negative_counter := Protocol.move_hazard_report({
		"data": {"hazards": [{"hazard_type": "minefield", "hull_lost": -1}]}
	})
	assert(negative_counter == "Sector hazard · minefield")

	var malformed_identity := Protocol.move_hazard_report({
		"data": {"hazards": [
			{"hazard_type": true, "message": 12, "damage": 7},
			{"type": "", "message": "hidden", "damage": 4},
		]}
	})
	assert(malformed_identity == "")
	quit(0)
