extends SceneTree

const MainScene = preload("res://Main.tscn")

class FakeRefresh extends RefCounted:
	var calls: Array = []
	func refresh(_force: bool = false, domains: Array = []) -> bool:
		calls.append(domains.duplicate())
		return true

class FakeTransport extends RefCounted:
	var next_id := 0
	func poll(_delta: float) -> void:
		pass
	func drain_events() -> Array:
		return []
	func request(_command: String, _data: Dictionary, _token: String = "") -> String:
		next_id += 1
		return "mail-refresh-%d" % next_id
	func close(_reason: String = "") -> void:
		pass

var failures: Array[String] = []

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	var app = MainScene.instantiate()
	root.add_child(app)
	await process_frame
	var fake_refresh := FakeRefresh.new()
	app.refresh_coordinator = fake_refresh
	app.transport = FakeTransport.new()
	# Regression: a server refusal must reach the player as a readable message,
	# rather than being treated as a successful mutation or silently discarded.
	await app._finish_command_reply(
		{"status": "refused", "error": {"message": "Insufficient credits"}},
		{"label": "Ship repair", "command": "ship.repair", "mutating": true}
	)
	_check(app.gameplay_view.notification_label.visible, "server refusal did not display a notification")
	_check(app.gameplay_view.notification_label.text == "Ship repair refused · Insufficient credits", "server refusal was not shown with command context and server reason")
	_check(fake_refresh.calls.size() == 1 and fake_refresh.calls[0] == ["player", "ship"], "ship repair refusal did not refresh authoritative credits and hull")
	fake_refresh.calls.clear()
	await app._finish_command_reply(
		{"status": "ok", "type": "ship.repair", "data": {"repaired": true, "cost": 120, "hull": 100}},
		{"label": "Ship repair", "command": "ship.repair", "mutating": true}
	)
	_check(fake_refresh.calls.size() > 0 and fake_refresh.calls.back() == ["player", "ship"], "confirmed ship repair did not refresh credits and hull")
	fake_refresh.calls.clear()
	await app._finish_command_reply(
		{"status": "ok", "type": "shipyard.upgraded_v1", "data": {"ship_id": 12, "new_type_id": 7, "new_ship_name": "Scout One", "credits_spent": 500}},
		{"label": "Ship hull upgrade", "command": "shipyard.upgrade", "data": {"new_type_id": 7, "new_ship_name": "Scout One"}, "mutating": true}
	)
	_check(fake_refresh.calls.size() == 1 and fake_refresh.calls[0] == ["player", "ship", "sector"], "confirmed shipyard upgrade did not refresh affected domains")
	# SHIP-02: only the server's typed ownership receipt may confirm a transfer;
	# both success and refusal reconcile the ownership snapshot before another
	# ship action is offered.
	app.auth_session.player_id = 7
	fake_refresh.calls.clear()
	await app._finish_command_reply(
		{"status": "ok", "type": "ship.transfer.response", "data": {
			"ship_id": 44, "from_player_id": 7, "target_player_id": 12,
		}},
		{"label": "Transfer this ship", "command": "ship.transfer", "data": {
			"ship_id": 44, "target_player_id": 12,
		}, "mutating": true}
	)
	_check(app.gameplay_view.notification_label.text.contains("Ship transfer receipt accepted"), "typed ship transfer receipt was not acknowledged")
	_check(not app.gameplay_view.notification_label.text.contains("is now owned by player 12"), "transfer presented final ownership before roster refresh")
	_check(fake_refresh.calls.size() == 1 and fake_refresh.calls[0] == ["player", "ship", "sector"], "confirmed ship transfer did not refresh affected ownership domains")
	_check(app._ship_roster_refresh_after_command, "confirmed ship transfer did not schedule an authoritative roster refresh")
	fake_refresh.calls.clear()
	await app._finish_command_reply(
		{"status": "refused", "error": {"message": "Ship is not transferable"}},
		{"label": "Transfer this ship", "command": "ship.transfer", "data": {
			"ship_id": 44, "target_player_id": 12,
		}, "mutating": true}
	)
	_check(app.gameplay_view.notification_label.text.contains("Ship is not transferable"), "ship transfer refusal was not surfaced")
	_check(fake_refresh.calls.size() == 1 and fake_refresh.calls[0] == ["player", "ship", "sector"], "ship transfer refusal did not refresh ownership state")
	fake_refresh.calls.clear()
	await app._finish_command_reply(
		{"status": "ok", "type": "ship.transfer.response", "data": {
			"ship_id": 45, "from_player_id": 7, "target_player_id": 12,
		}},
		{"label": "Transfer this ship", "command": "ship.transfer", "data": {
			"ship_id": 44, "target_player_id": 12,
		}, "mutating": true}
	)
	_check(app.gameplay_view.notification_label.text.contains("not confirmed by the authoritative response"), "mismatched ship transfer receipt was presented as confirmed")
	_check(fake_refresh.calls.size() == 1 and fake_refresh.calls[0] == ["player", "ship", "sector"], "mismatched ship transfer receipt did not refresh ownership state")
	fake_refresh.calls.clear()
	# A lost transfer reply is ambiguous: the server may have committed the
	# ownership handoff before transport failed. Reconcile all affected domains
	# and schedule a typed owned-roster refresh instead of claiming either side.
	app._pending_game_commands["transfer-timeout"] = {
		"label": "Transfer this ship", "command": "ship.transfer", "mutating": true,
		"data": {"ship_id": 44, "target_player_id": 12}}
	app._on_request_timed_out("transfer-timeout", "ship.transfer")
	_check(app.gameplay_view.notification_label.text.contains("outcome is uncertain"), "timed-out ship transfer was not surfaced as uncertain")
	_check(fake_refresh.calls.size() == 1 and fake_refresh.calls[0] == ["player", "ship", "sector"], "timed-out ship transfer did not refresh affected ownership domains")
	_check(app._ship_roster_refresh_after_command, "timed-out ship transfer did not schedule an authoritative roster refresh")
	fake_refresh.calls.clear()
	await app._finish_command_reply(
		{"status": "ok", "data": {"ship_id": 12, "new_type_id": 7, "new_ship_name": "Scout One", "credits_spent": 500}},
		{"label": "Ship hull upgrade", "command": "shipyard.upgrade", "data": {"new_type_id": 7, "new_ship_name": "Scout One"}, "mutating": true}
	)
	_check(app.gameplay_view.notification_label.text.contains("invalid shipyard receipt"), "untyped shipyard upgrade was presented as confirmed")
	_check(fake_refresh.calls.size() == 1 and fake_refresh.calls[0] == ["player", "ship", "sector"], "invalid shipyard upgrade did not refresh affected domains")
	fake_refresh.calls.clear()
	await app._finish_command_reply(
		{"status": "ok", "type": "hardware.purchase_v1", "data": {
			"code": "TRANSWARP", "quantity": 1, "credits_spent": 500,
			"ship": {"has_transwarp": true},
		}},
		{"label": "Buy TRANSWARP", "command": "hardware.buy", "data": {"code": "TRANSWARP", "quantity": 1}, "mutating": true}
	)
	_check(fake_refresh.calls.size() == 1 and fake_refresh.calls[0] == ["player", "ship"], "confirmed hardware purchase did not refresh affected domains")
	fake_refresh.calls.clear()
	await app._finish_command_reply(
		{"status": "ok", "data": {"code": "TRANSWARP", "quantity": 1, "credits_spent": 500, "ship": {}}},
		{"label": "Buy TRANSWARP", "command": "hardware.buy", "data": {"code": "TRANSWARP", "quantity": 1}, "mutating": true}
	)
	_check(app.gameplay_view.notification_label.text.contains("invalid purchase receipt"), "untyped hardware purchase was presented as confirmed")
	_check(fake_refresh.calls.size() == 1 and fake_refresh.calls[0] == ["player", "ship"], "invalid hardware purchase did not refresh affected domains")
	await app._finish_command_reply(
		{"status": "ok", "data": {"success": true}},
		{"label": "Change password", "command": "auth.change_password", "mutating": true}
	)
	_check(app.gameplay_view.notification_label.text.contains("Password changed"), "password change success was not acknowledged")
	_check(not app.gameplay_view.notification_label.text.contains("qa-new-ephemeral"), "password change success notification exposed a credential")
	app.gameplay_view.set_snapshot({
		"authenticated": true,
		"disconnected": false,
		"hud": {"sector_id": 3},
		"freshness": {"sector": "fresh", "overall": "fresh"},
		"ship": {},
		"sector": {},
	})
	app.gameplay_view.enter_port_workflow({"port": {"id": 99, "name": "Previously inspected", "type": 9, "commodities": []}})
	_check(app.gameplay_view.port_workflow.visible, "port refusal fixture did not open the prior port screen")
	await app._finish_command_reply(
		{"status": "refused", "error": {"message": "Port information is available only in its sector"}},
		{"label": "Open port", "command": "port.info", "mutating": false}
	)
	_check(not app.gameplay_view.port_workflow.visible, "refused remote port.info left stale port inventory visible")
	_check(app.gameplay_view.notification_label.text.contains("Port information refused") and app.gameplay_view.notification_label.text.contains("only in its sector"), "port proximity refusal was not explained")
	app.gameplay_view.information_text.text = ""
	fake_refresh.calls.clear()
	await app._finish_command_reply(
		{"status": "refused", "error": {"message": "Detailed scan requires the current sector"}, "data": {
			"sector_id": 999, "name": "Untrusted remote sector", "ports": [{"id": 77}],
		}},
		{"label": "Detailed current-sector scan", "command": "sector.scan", "mutating": false}
	)
	_check(app.gameplay_view.notification_label.text.contains("Detailed current-sector scan refused"), "refused scan was not explained")
	_check(not app.gameplay_view.notification_label.text.contains("Untrusted remote sector") and not app.gameplay_view.information_text.text.contains("Untrusted remote sector"), "refused scan rendered an untrusted response payload")
	_check(fake_refresh.calls.size() == 1 and fake_refresh.calls[0] == ["sector"], "refused scan did not refresh the confirmed sector")
	await app._finish_command_reply(
		{"status": "error", "error": {"code": 1101, "message": "Invalid old password"}},
		{"label": "Change password", "command": "auth.change_password", "mutating": true}
	)
	_check(app.gameplay_view.notification_label.text.contains("current password was not accepted"), "wrong-current-password response was not explained")
	_check(not app.gameplay_view.notification_label.text.contains("Invalid old password"), "password refusal echoed the server's raw credential-related message")
	fake_refresh.calls.clear()
	await app._finish_command_reply(
		{"status": "ok", "data": {"ship": {"id": 99, "name": "Other ship"}}},
		{"label": "Inspect ship details", "command": "ship.inspect", "data": {"ship_id": 61}, "mutating": false}
	)
	_check(app.gameplay_view.notification_label.text.contains("returned ship 99 instead of selected ship 61"), "mismatched ship inspection response was not rejected")
	_check(fake_refresh.calls.size() == 1 and fake_refresh.calls[0] == ["sector"], "mismatched ship inspection did not refresh the confirmed sector")
	await app._finish_command_reply(
		{"status": "ok", "data": {"ship": {"name": "Unidentified ship"}}},
		{"label": "Inspect ship details", "command": "ship.inspect", "data": {"ship_id": 61}, "mutating": false}
	)
	_check(app.gameplay_view.notification_label.text.contains("omitted a valid ship ID"), "ship inspection without an authoritative ID was rendered")
	_check(fake_refresh.calls.size() == 2, "missing ship inspection ID did not refresh the confirmed sector")
	await app._finish_warp_reply(
		{"status": "ok", "type": "move.result", "data": {"current_sector": 44}}, 44
	)
	_check(app.gameplay_view.notification_label.text.contains("Warp confirmed · Sector 44"), "authoritative warp destination was not presented")
	await app._finish_warp_reply(
		{"status": "ok", "type": "move.result", "data": {"current_sector": 45}}, 44
	)
	_check(app.gameplay_view.notification_label.text.contains("reported sector 45 instead of requested sector 44"), "mismatched warp destination was presented as confirmed")
	await app._finish_warp_reply(
		{"status": "ok", "type": "move.result", "data": {}}, 44
	)
	_check(app.gameplay_view.notification_label.text.contains("omitted the authoritative current sector"), "warp without authoritative destination was presented as confirmed")
	fake_refresh.calls.clear()
	await app._finish_command_reply(
		{"status": "ok", "type": "move.transwarp", "data": {"sector_id": 44, "hazards": []}},
		{"label": "Transwarp to sector", "command": "move.transwarp", "data": {"to_sector_id": 44}, "mutating": true}
	)
	_check(app.gameplay_view.notification_label.text.contains("Transwarp confirmed · Sector 44"), "canonical transwarp destination was not presented")
	_check(fake_refresh.calls.size() == 1 and fake_refresh.calls[0] == ["player", "ship", "sector"], "confirmed transwarp did not refresh all movement-owned domains")
	fake_refresh.calls.clear()
	await app._finish_command_reply(
		{"status": "ok", "type": "move.transwarp", "data": {"current_sector": 44}},
		{"label": "Transwarp to sector", "command": "move.transwarp", "data": {"to_sector_id": 44}, "mutating": true}
	)
	_check(app.gameplay_view.notification_label.text.contains("Transwarp was not confirmed"), "transwarp alias destination was presented as authoritative")
	_check(fake_refresh.calls.size() == 1 and fake_refresh.calls[0] == ["player", "ship", "sector"], "mismatched transwarp destination did not refresh all movement-owned domains")
	fake_refresh.calls.clear()
	await app._finish_command_reply(
		{"status": "error", "type": "error", "error": {"message": "Entry hazard resolution failed", "data": {"ship_destroyed": true, "hazards": [{"hazard_type": "minefield", "hull_lost": 100}]}}},
		{"label": "Transwarp to sector", "command": "move.transwarp", "data": {"to_sector_id": 44}, "mutating": true}
	)
	_check(app.gameplay_view.notification_label.text.contains("Transwarp outcome is terminal"), "terminal transwarp hazard was reduced to a generic refusal")
	_check(fake_refresh.calls.size() == 1 and fake_refresh.calls[0] == ["player", "ship", "sector"], "terminal transwarp hazard did not refresh all movement-owned domains")
	app.gameplay_view.information_text.text = ""
	fake_refresh.calls.clear()
	await app._finish_command_reply(
		{"status": "ok", "type": "ship.tow.engaged", "data": {
			"towee_ship_id": 20,
			"hazards": [{"hazard_type": "minefield", "hull_lost": 6}]
		}},
		{"label": "Tow ship", "command": "ship.tow", "mutating": true}
	)
	_check(app.gameplay_view.information_text.text.contains("minefield") and app.gameplay_view.information_text.text.contains("hull lost: 6"), "tow hazard report was not presented")
	_check(fake_refresh.calls.size() == 1, "tow hazard response did not refresh authoritative state")
	app._finish_communication_history_refresh({
		"status": "ok",
		"type": "chat.history_v1",
		"data": {"messages": [{"sender_name": "Nova", "message": "Confirmed"}]},
	})
	_check(app.gameplay_view.information_text.text.contains("Nova") and app.gameplay_view.information_text.text.contains("Confirmed"), "successful chat history refresh did not display the authoritative message")
	app._finish_communication_history_refresh({
		"status": "ok",
		"type": "mail.inbox_v1",
		"data": {"messages": [{"sender_name": "Spoof", "message": "wrong response"}]},
	})
	_check(app.gameplay_view.notification_label.text.contains("chat history refresh returned an unexpected response"), "mismatched chat refresh response was displayed")
	app._finish_communication_history_refresh({
		"status": "refused",
		"error": {"message": "chat unavailable"},
	})
	_check(app.gameplay_view.notification_label.text.contains("chat history refresh was refused"), "chat history refusal was not surfaced without fabricating a message")
	app.gameplay_view.information_text.text = ""
	fake_refresh.calls.clear()
	await app._finish_command_reply(
		{"status": "ok", "type": "mail.read", "data": {"id": 44, "sender_name": "Nova", "subject": "Welcome", "body": "Meet at the dock"}},
		{"label": "Read mail by ID", "command": "mail.read", "mutating": true}
	)
	_check(app.gameplay_view.information_text.text.contains("Meet at the dock"), "mail read did not display the authoritative message body")
	_check(app.gameplay_view.notification_label.text.contains("refreshing mail inbox"), "mail read did not request an authoritative inbox refresh")
	_check(app._pending_game_commands.values().any(func(operation): return operation.get("kind") == "mail_inbox_refresh"), "mail read refresh was not tracked as a pending operation")
	app._finish_mail_inbox_refresh({
		"status": "ok",
		"type": "mail.inbox_v1",
		"data": {"items": [{"id": 44, "sender_name": "Nova", "subject": "Welcome", "read_at": "now"}]},
	})
	_check(app.gameplay_view.information_text.text.contains("#44") and app.gameplay_view.information_text.text.contains("READ"), "mail inbox refresh did not display the authoritative read state")
	app._finish_mail_inbox_refresh({
		"status": "ok",
		"type": "chat.history_v1",
		"data": {"items": [{"id": 45, "sender_name": "Spoof"}]},
	})
	_check(app.gameplay_view.notification_label.text.contains("inbox refresh returned an unexpected response"), "mismatched mail refresh response was displayed")
	app._finish_mail_inbox_refresh({
		"status": "refused",
		"error": {"message": "mail unavailable"},
	})
	_check(app.gameplay_view.notification_label.text.contains("inbox refresh was refused"), "mail inbox refresh refusal was not surfaced")
	app.queue_free()
	await process_frame
	if failures.is_empty():
		print("Godot command/password tests: 12 passed")
		quit(0)
	else:
		for failure in failures:
			push_error(failure)
		quit(1)

func _check(condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)
