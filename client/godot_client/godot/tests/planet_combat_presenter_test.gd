extends SceneTree

const Presenter = preload("res://PlanetCombatPresenter.gd")

func _initialize() -> void:
	var response := {
		"status": "error",
		"error": {"message": "Planetary defenses engaged your ship; landing refused.", "data": {
			"planet_id": 42,
			"landing_allowed": false,
			"ship_remains_in_sector": true,
			"combat": [
				{"defense": "atmosphere_quasar", "damage": 80, "shields_lost": 50, "hull_lost": 30, "shields_remaining": 0, "hull_remaining": 70, "destroyed": false},
				{"result": "repelled", "attacker_fighters_lost": 10, "attacker_fighters_remaining": 5, "planet_fighters_lost": 5, "planet_fighters_remaining": 15, "citadel_shields_absorbed": 20, "citadel_shields_remaining": 0},
			],
		}},
	}
	var refusal: Dictionary = Presenter.landing_refusal(response)
	assert(refusal["has_combat"])
	assert(str(refusal["message"]).contains("shields lost 50"))
	assert(str(refusal["message"]).contains("hull lost 30"))
	assert(str(refusal["message"]).contains("Planetary fighters repelled"))
	assert(str(refusal["message"]).contains("10 fighters (5 remain)"))
	assert(str(refusal["message"]).contains("citadel shields absorbed 20"))
	assert(str(refusal["message"]).contains("Landing was refused"))
	var destroyed := response.duplicate(true)
	destroyed["error"]["data"]["ship_remains_in_sector"] = false
	destroyed["error"]["data"]["combat"][0]["destroyed"] = true
	var destroyed_report: Dictionary = Presenter.landing_refusal(destroyed)
	assert(str(destroyed_report["message"]).contains("ship destroyed"))
	assert(str(destroyed_report["message"]).contains("did not remain in the sector"))
	assert(not Presenter.landing_refusal({"error": {"message": "Permission denied"}})["has_combat"])
	assert(Presenter.attack_result({"planet_id": 42, "captured": true, "attacker_remaining_fighters": 12, "defender_remaining_fighters": 0}).contains("captured: true"))
	print("Godot planetary combat presenter tests: 10 passed")
	quit(0)
