extends SceneTree

const MainScene = preload("res://Main.tscn")

class FakeTransport extends Node:
	var calls: Array[Dictionary] = []

	func poll(_delta: float) -> void:
		pass

	func drain_events() -> Array:
		return []

	func close(_reason: String = "") -> void:
		pass

	func request(command: String, data: Dictionary, token: String) -> String:
		calls.append({"command": command, "data": data.duplicate(true), "token": token})
		return "toll-resolution-%d" % calls.size()

var failures: Array[String] = []

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	root.size = Vector2i(1280, 720)
	var app = MainScene.instantiate()
	root.add_child(app)
	await process_frame
	_check(app._toll_offer_dialog.get_parent() == app, "toll confirmation dialog was attached outside the client scene and could leak at shutdown")
	var fake := FakeTransport.new()
	app.transport = fake
	app.auth_session.state = 2 # TwAuthSession.State.AUTHENTICATED
	app.auth_session.session_token = "test-session"

	await app._finish_warp_reply({
		"status": "ok",
		"type": "move.toll_offer_v1",
		"data": {"offer_id": "offer-accepted", "sector_id": 42, "fighter_count": 30, "amount": 30, "credits_per_fighter": 1},
	}, 42)
	_check(app._toll_offer_dialog.visible, "server toll offer did not open the payment/refusal prompt")
	_check(app._toll_offer_dialog.dialog_text.contains("30 toll fighters") and app._toll_offer_dialog.dialog_text.contains("30 credits"), "prompt omitted the authoritative fighter count or payment amount")
	app._toll_offer_dialog.get_ok_button().pressed.emit()
	_check(fake.calls.size() == 1, "accepting the offer did not send exactly one resolution request")
	if fake.calls.size() == 1:
		_check(fake.calls[0]["command"] == "move.warp", "toll acceptance used an undocumented command")
		_check(fake.calls[0]["data"] == {"to_sector_id": 42, "toll_offer_id": "offer-accepted", "toll_accept": true}, "accepted offer did not preserve server-issued identity and choice")

	app._pending_game_commands.clear()
	await app._finish_warp_reply({
		"status": "ok",
		"type": "move.toll_offer_v1",
		"data": {"offer_id": "offer-refused", "sector_id": 43, "fighter_count": 10, "amount": 10, "credits_per_fighter": 1},
	}, 43)
	app._toll_offer_dialog.get_cancel_button().pressed.emit()
	_check(fake.calls.size() == 2, "refusing the offer did not send exactly one resolution request")
	if fake.calls.size() == 2:
		_check(fake.calls[1]["data"] == {"to_sector_id": 43, "toll_offer_id": "offer-refused", "toll_accept": false}, "refusal did not preserve the offer ID and explicit false choice")
	_check(app._pending_toll_offer.is_empty(), "resolved toll offer remained locally actionable")

	app.queue_free()
	await process_frame
	fake.free()
	if failures.is_empty():
		print("Godot toll offer workflow test: 9 checks passed")
		quit(0)
	else:
		for failure in failures:
			push_error(failure)
		quit(1)

func _check(condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)
