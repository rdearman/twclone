extends SceneTree

const MainScene = preload("res://Main.tscn")

var failures: Array[String] = []
var server := TCPServer.new()
var server_peer: StreamPeerTCP
var received := ""
var replied := false
var handled_commands: Dictionary = {}
var main_instance

func _init() -> void:
	call_deferred("_run")

func _run() -> void:
	var listen_error: Error = server.listen(18766, "127.0.0.1")
	_check(listen_error == OK, "UI dispatch test server did not listen")
	if listen_error != OK:
		_finish()
		return
	main_instance = MainScene.instantiate()
	root.add_child(main_instance)
	await process_frame
	var login_view = main_instance.get_node("LoginView")
	login_view.host_input.text = "127.0.0.1"
	login_view.port_input.text = "18766"
	login_view.username_input.text = "newguy"
	login_view.password_input.text = "test-only"
	login_view._submit()
	for _i in range(120):
		if server.is_connection_available():
			server_peer = server.take_connection()
		if server_peer:
			server_peer.poll()
			if server_peer.get_available_bytes() > 0:
				received += server_peer.get_utf8_string(server_peer.get_available_bytes())
				while "\n" in received:
					var line := received.split("\n", false)[0]
					received = received.substr(line.length() + 1)
					var frame: Dictionary = JSON.parse_string(line)
					var command := str(frame.get("command", ""))
					if handled_commands.has(str(frame.get("id", ""))):
						continue
					handled_commands[str(frame.get("id", ""))] = true
					var response := _response_for(command, frame)
					if command == "auth.login":
						_check(frame.get("data", {}).has("passwd"), "Main/LoginView omitted passwd")
						_check(not frame.get("data", {}).has("password"), "Main/LoginView sent password instead of passwd")
						replied = true
					server_peer.put_data((JSON.stringify(response) + "\n").to_utf8_buffer())
		main_instance._process(0.016)
		await process_frame
	_check(replied, "UI dispatch test server did not receive the login request")
	_check(main_instance.auth_session.is_authenticated(), "Main did not dispatch the auth.session response into AuthSession")
	_check(not login_view.visible, "successful login did not hide LoginView")
	var snapshot: Dictionary = main_instance.client_state.snapshot()
	_check(snapshot["freshness"]["player"] == "fresh" and snapshot["freshness"]["ship"] == "fresh" and snapshot["freshness"]["sector"] == "fresh", "authoritative refresh did not complete all three domains")
	_check(snapshot["hud"]["sector_id"] == 1, "HUD discarded the confirmed sector from JSON numeric data")
	_check(snapshot["hud"]["credits"] == 5000 and snapshot["hud"]["turns_remaining"] == 999999, "HUD discarded confirmed player credits or turns")
	_check(snapshot["hud"]["cargo_total"] == 10 and snapshot["hud"]["fighters"] == 1 and snapshot["hud"]["shields"] == 1 and snapshot["hud"]["hull"] == 100, "HUD discarded confirmed active-ship statistics")
	_check(main_instance.gameplay_view.sector_title.text.begins_with("SECTOR 1"), "main HUD did not display the confirmed sector")
	_check(main_instance.gameplay_view.credits_label.text.replace(",", "").contains("5000") and main_instance.gameplay_view.turns_label.text.replace(",", "").contains("999999"), "main HUD did not display confirmed credits and turns")
	_check(main_instance.gameplay_view.cargo_label.text.contains("10") and main_instance.gameplay_view.shields_label.text.contains("1") and main_instance.gameplay_view.hull_label.text.contains("100") and main_instance.gameplay_view.fighters_label.text.contains("1"), "main HUD did not display confirmed ship state")
	_finish()

func _response_for(command: String, frame: Dictionary) -> Dictionary:
	var base := {"reply_to": frame.get("id", ""), "status": "ok", "type": "", "data": {}}
	match command:
		"auth.login":
			base["type"] = "auth.session"
			base["data"] = {"session": "test-token", "player_id": 1.0}
		"player.my_info":
			base["type"] = "player.info"
			base["data"] = {"player": {"id": 1.0, "username": "newguy", "credits": 5000.0, "turns_remaining": 999999.0, "sector": 1.0, "ship_id": 1.0}}
		"ship.status":
			base["type"] = "ship.status"
			base["data"] = {"ship": {"id": 1.0, "name": "Hells Bells", "holds": 10.0, "fighters": 1.0, "shields": 1.0, "hull": 100.0}}
		"sector.info":
			base["type"] = "sector.info"
			base["data"] = {"sector_id": 1.0, "name": "Fedspace 1", "adjacent_sectors": [{"to_sector": 2.0}], "ports": [], "planets": [], "ships": []}
		"subscribe.add":
			base["type"] = "subscribe.ack_v1"
		"move.autopilot.status":
			base["type"] = "move.autopilot.status"
		_:
			base["type"] = command
	return base

func _check(condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)

func _finish() -> void:
	if is_instance_valid(main_instance):
		main_instance.queue_free()
	server.stop()
	if failures.is_empty():
		print("Godot Main auth dispatch test: PASS")
		quit(0)
	else:
		for failure in failures:
			push_error(failure)
		print("Godot Main auth dispatch test: FAIL")
		quit(1)
