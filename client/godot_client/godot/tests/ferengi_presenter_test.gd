extends SceneTree

const Presenter = preload("res://FerengiPresenter.gd")

func _initialize() -> void:
	var data := {
		"sector_id": 27,
		"traders": [{"display_name": "Gaila", "trader_code": "gaila", "trader_id": 3, "reputation": 4}],
		"offers": [{"deal_id": 91, "display_name": "Gaila", "side": "trader_sells", "quantity": 5, "commodity": "ORE", "unit_price": 12, "trader_sector_id": 28, "expires_at": "later"}],
	}
	var text := Presenter.traders(data)
	assert(text.contains("SECTOR 27"))
	assert(text.contains("Gaila · gaila · trader 3 · reputation 4"))
	assert(text.contains("Deal 91"))
	assert(text.contains("you buy 5 ORE at 12 CR each"))
	assert(text.contains("trader sector 28"))
	assert(Presenter.traders({"sector_id": 27, "traders": [], "offers": []}).contains("No active Ferengi traders"))
	assert(Presenter.traders({"sector_id": 27, "traders": [], "offers": []}).contains("No open offers."))
	assert(Presenter.deal_result({"deal_id": 91, "status": "settled", "quantity": 5, "commodity": "ORE", "unit_price": 12, "reputation": 5}).contains("settled 5 ORE at 12 CR each"))
	print("Godot Ferengi presenter tests: 8 passed")
	quit(0)
