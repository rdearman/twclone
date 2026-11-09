extends SceneTree

const Presenter = preload("res://PlanetCombatPresenter.gd")

func _init() -> void:
	var report := Presenter.landing_refusal({
		"status": "refused",
		"error": {"data": {"combat": [{
			"defense": "atmosphere_quasar",
			"damage": "25",
			"shields_lost": true,
			"hull_lost": 15,
			"shields_remaining": 0,
			"hull_remaining": 85,
			"destroyed": 1,
		}]}}
	})
	_check(report.get("has_combat", false) == true, "authoritative combat row was retained")
	var message := str(report.get("message", ""))
	_check(message.contains("25 damage") == false, "string damage was not presented as authoritative")
	_check(message.contains("shields lost unknown") == true, "boolean shield loss was not presented as an integer")
	_check(message.contains("hull lost 15") == true, "integer hull loss was presented")
	_check(message.contains("ship destroyed") == false, "non-boolean destroyed value was not treated as destruction")
	_check(message.contains("Landing was refused") == true, "refusal remained explicit")
	print("PASS: planetary defense presenter preserves only typed server facts")
	quit()

func _check(condition: bool, failure: String) -> void:
	if not condition:
		push_error("FAIL: " + failure)
		quit(1)
