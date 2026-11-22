extends SceneTree

const MainScene = preload("res://Main.tscn")

var failures: Array[String] = []
var app
var registered_username := ""
var test_password := "Temporary-QA-Password-42"
var login_existing_account := false
var acceptance_port_id := 9
var acceptance_trade_port_id := 2
var acceptance_trade_sector := 3
var acceptance_trade_sell_port_id := 1
var acceptance_trade_sell_sector := 11
var acceptance_trade_commodity := "ORG"
var acceptance_planet_id := 700

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	if OS.get_environment("TWCLONE_GODOT_LIVE") != "1":
		print("SKIP: set TWCLONE_GODOT_LIVE=1 for a dedicated disposable server journey")
		quit(0)
		return
	root.size = Vector2i(1280, 720)
	login_existing_account = OS.get_environment("TWCLONE_GODOT_LOGIN") == "1"
	if not OS.get_environment("TWCLONE_GODOT_PORT_ID").is_empty():
		acceptance_port_id = int(OS.get_environment("TWCLONE_GODOT_PORT_ID"))
	if not OS.get_environment("TWCLONE_GODOT_TRADE_PORT_ID").is_empty():
		acceptance_trade_port_id = int(OS.get_environment("TWCLONE_GODOT_TRADE_PORT_ID"))
	if not OS.get_environment("TWCLONE_GODOT_TRADE_SECTOR").is_empty():
		acceptance_trade_sector = int(OS.get_environment("TWCLONE_GODOT_TRADE_SECTOR"))
	if not OS.get_environment("TWCLONE_GODOT_TRADE_SELL_PORT_ID").is_empty():
		acceptance_trade_sell_port_id = int(OS.get_environment("TWCLONE_GODOT_TRADE_SELL_PORT_ID"))
	if not OS.get_environment("TWCLONE_GODOT_TRADE_SELL_SECTOR").is_empty():
		acceptance_trade_sell_sector = int(OS.get_environment("TWCLONE_GODOT_TRADE_SELL_SECTOR"))
	if not OS.get_environment("TWCLONE_GODOT_TRADE_COMMODITY").is_empty():
		acceptance_trade_commodity = OS.get_environment("TWCLONE_GODOT_TRADE_COMMODITY")
	if not OS.get_environment("TWCLONE_GODOT_PLANET_ID").is_empty():
		acceptance_planet_id = int(OS.get_environment("TWCLONE_GODOT_PLANET_ID"))
	if login_existing_account:
		registered_username = OS.get_environment("TWCLONE_GODOT_USERNAME")
		test_password = OS.get_environment("TWCLONE_GODOT_PASSWORD")
		_check(not registered_username.is_empty() and not test_password.is_empty(), "live login acceptance requires a disposable username and password")
		if registered_username.is_empty() or test_password.is_empty():
			quit(1)
			return
	app = MainScene.instantiate()
	root.add_child(app)
	await process_frame
	var login = app.get_node("LoginView")
	randomize()
	if not login_existing_account:
		registered_username = "gd%d" % randi_range(10000000, 99999999)
	login.host_input.text = OS.get_environment("TWCLONE_GODOT_HOST") if not OS.get_environment("TWCLONE_GODOT_HOST").is_empty() else "127.0.0.1"
	login.port_input.text = OS.get_environment("TWCLONE_GODOT_PORT") if not OS.get_environment("TWCLONE_GODOT_PORT").is_empty() else "55499"
	login.tls_enabled_checkbox.button_pressed = OS.get_environment("TWCLONE_GODOT_TLS") == "1"
	login.tls_certificate_input.text = OS.get_environment("TWCLONE_GODOT_TLS_CA")
	login.username_input.text = registered_username
	login.password_input.text = test_password
	if not login_existing_account:
		login.mode_toggle_button.pressed.emit()
		login.confirm_password_input.text = test_password
		login.ship_name_input.text = "Disposable QA"
	login.connect_button.pressed.emit()
	var authenticated := await _wait_until(func() -> bool:
		var snapshot: Dictionary = app.client_state.snapshot()
		var freshness: Dictionary = snapshot.get("freshness", {})
		return app.auth_session.is_authenticated() and freshness.get("player") == "fresh" \
			and freshness.get("ship") == "fresh" and freshness.get("sector") == "fresh",
		240
	)
	_check(authenticated, "ordinary-player authentication through the Godot LoginView did not complete: " + str(login.status_label.text))
	if not authenticated:
		_finish()
		return
	var initial: Dictionary = app.client_state.snapshot()
	var hud: Dictionary = initial.get("hud", {})
	var initial_sector: int = int(hud.get("sector_id", 0))
	var initial_turns: int = int(hud.get("turns_remaining", -1))
	var ship_id: int = int(hud.get("ship_id", 0))
	_check(initial_sector > 0 and initial_turns >= 0 and ship_id > 0, "starting sector, turns or active ship were absent after authoritative refresh")
	if OS.get_environment("TWCLONE_GODOT_VERIFY_AFTER_RESTART") == "1":
		var expected_player_id := int(OS.get_environment("TWCLONE_GODOT_EXPECT_PLAYER_ID"))
		var expected_sector_id := int(OS.get_environment("TWCLONE_GODOT_EXPECT_SECTOR"))
		var expected_ship_id := int(OS.get_environment("TWCLONE_GODOT_EXPECT_SHIP_ID"))
		var expected_commodity := OS.get_environment("TWCLONE_GODOT_EXPECT_COMMODITY")
		var expected_quantity := int(OS.get_environment("TWCLONE_GODOT_EXPECT_QUANTITY"))
		_check(int(app.auth_session.player_id) == expected_player_id, "Godot login after restart restored a different player")
		_check(initial_sector == expected_sector_id, "Godot login after restart restored a different sector")
		_check(ship_id == expected_ship_id, "Godot login after restart restored a different active ship")
		_check(_cargo_quantity(initial.get("ship", {}), expected_commodity) == expected_quantity, "Godot login after restart restored a different cargo balance")
		if failures.is_empty():
			print("LIVE_SERVER_RESTART_PERSISTENCE_PASS player_id=%d ship_id=%d sector=%d %s=%d turns=%d tls=true" % [expected_player_id, ship_id, initial_sector, expected_commodity, expected_quantity, initial_turns])
		_finish()
		return
	var adjacent: Array = initial.get("sector", {}).get("adjacent_sector_ids", [])
	_check(not adjacent.is_empty(), "confirmed sector did not expose connected navigation targets")
	if adjacent.is_empty():
		_finish()
		return
	var destination := int(adjacent[0])
	var menu = app.gameplay_view.command_menu
	menu.select_category("NAVIGATION")
	await process_frame
	var warp_button := _find_button(menu._action_list, "Warp to sector number")
	_check(warp_button != null and not warp_button.disabled, "navigation menu did not expose a usable direct warp action")
	if warp_button == null or warp_button.disabled:
		_finish()
		return
	warp_button.pressed.emit()
	if not menu._form_fields.has("to_sector_id"):
		_check(false, "warp menu action did not open its destination form")
		_finish()
		return
	(menu._form_fields["to_sector_id"] as LineEdit).text = str(destination)
	menu._inline_submit.pressed.emit()
	var toll_was_offered := await _wait_until(func() -> bool:
		return app._toll_offer_dialog.visible or int(app.client_state.snapshot().get("hud", {}).get("sector_id", 0)) == destination,
		240
	)
	_check(toll_was_offered, "warp did not complete or return the expected server-side toll decision")
	if toll_was_offered and app._toll_offer_dialog.visible:
		_check(app._toll_offer_dialog.dialog_text.contains("toll fighters") and app._toll_offer_dialog.dialog_text.contains("credits"), "toll offer did not explain the amount and hostile-refusal consequence")
		app._toll_offer_dialog.get_ok_button().pressed.emit()
	var moved := await _wait_until(func() -> bool:
		return (int(app.client_state.snapshot().get("hud", {}).get("sector_id", 0)) == destination
			and not app.gameplay_view.command_menu._busy),
		240
	)
	_check(moved, "Godot warp action did not settle at the selected connected sector")
	var after_move: Dictionary = app.client_state.snapshot()
	_check(int(after_move.get("hud", {}).get("turns_remaining", initial_turns)) == initial_turns - 1, "Godot warp did not consume exactly one turn")
	# The acceptance harness selects its target port and planet from the
	# disposable fixture. Existing-account runs can start in another connected
	# sector, so return to the sector confirmed at login before those workflows.
	var fixture_sector := int(OS.get_environment("TWCLONE_GODOT_FIXTURE_SECTOR"))
	if fixture_sector <= 0:
		fixture_sector = initial_sector
	if int(after_move.get("hud", {}).get("sector_id", 0)) != fixture_sector:
		menu.select_category("NAVIGATION")
		await process_frame
		var fixture_warp_button := _find_button(menu._action_list, "Warp to sector number")
		_check(fixture_warp_button != null and not fixture_warp_button.disabled, "navigation menu could not return to the disposable gameplay fixture")
		if fixture_warp_button != null and not fixture_warp_button.disabled:
			fixture_warp_button.pressed.emit()
			if menu._form_fields.has("to_sector_id"):
				(menu._form_fields["to_sector_id"] as LineEdit).text = str(fixture_sector)
				menu._inline_submit.pressed.emit()
				var reached_fixture_sector := await _wait_until(func() -> bool:
					return int(app.client_state.snapshot().get("hud", {}).get("sector_id", 0)) == fixture_sector and not menu._busy,
					240
				)
				_check(reached_fixture_sector, "ordinary-player warp did not return to the disposable port and planet sector")
			else:
				_check(false, "fixture-sector warp did not open its destination form")
	menu.select_category("NAVIGATION")
	await process_frame
	var scan_button := _find_button(menu._action_list, "Detailed current-sector scan")
	_check(scan_button != null and not scan_button.disabled, "navigation menu did not expose the current-sector scan")
	if scan_button != null and not scan_button.disabled:
		var before_info: String = app.gameplay_view.information_text.text
		scan_button.pressed.emit()
		var scanned := await _wait_until(func() -> bool:
			return (not app.gameplay_view.command_menu._busy
				and app.gameplay_view.information_text.text != before_info),
			180
		)
		_check(scanned, "Godot scan did not return a client-visible server result")
	else:
		_finish()
		return
	# Commodity exchange and StarDock services use distinct port classes in the
	# published V3 rules. Complete a buy and sale at the ordinary trading port
	# through its UI, then return to the StarDock for hardware and shipyard work.
	var trade_journey := await _perform_port_trade(
		acceptance_trade_port_id,
		acceptance_trade_sector,
		acceptance_trade_sell_port_id,
		acceptance_trade_sell_sector,
		fixture_sector
	)
	_check(trade_journey, "ordinary-player commodity buy/sell journey did not complete across the configured trading ports")
	if OS.get_environment("TWCLONE_GODOT_RELEASE_SMOKE") == "1":
		await _complete_release_smoke_reconnect(trade_journey)
		_finish()
		return
	var starport_journey := await _perform_starport_services(acceptance_port_id)
	_check(starport_journey, "StarDock hardware or shipyard workflow did not complete through the Godot UI")
	var planet_journey := await _perform_planet_journey(acceptance_planet_id)
	_check(planet_journey, "ordinary-player planet landing, harvest or launch journey did not complete through the Godot UI")
	if OS.get_environment("TWCLONE_GODOT_CORPORATION") == "1":
		# Exercise corporation creation and treasury access through the visible
		# Computer and Corporation menu workflows. Deposit into the player's bank
		# first when a new corporation is needed; corp.create charges that balance.
		if int(app.client_state.snapshot().get("player", {}).get("corp_id", 0)) <= 0:
			menu.select_category("TRADING")
			await process_frame
			var bank_deposit_button := _find_button(menu._action_list, "Deposit credits")
			_check(bank_deposit_button != null and not bank_deposit_button.disabled, "Trading menu did not expose player bank deposit")
			if bank_deposit_button != null and not bank_deposit_button.disabled:
				bank_deposit_button.pressed.emit()
				if menu._form_fields.has("amount"):
					(menu._form_fields["amount"] as LineEdit).text = "5000"
					menu._inline_submit.pressed.emit()
				var bank_deposit_settled := await _wait_until(func() -> bool:
					return app._pending_game_commands.is_empty() and not menu._busy,
					240
				)
				_check(bank_deposit_settled, "bank.deposit did not complete through the player-facing workflow")
			menu.select_category("COMPUTER")
			await process_frame
			var create_corp_button := _find_button(menu._action_list, "Create corporation")
			_check(create_corp_button != null and not create_corp_button.disabled, "Computer menu did not expose corporation creation")
			if create_corp_button != null and not create_corp_button.disabled:
				create_corp_button.pressed.emit()
				if menu._form_fields.has("name"):
					(menu._form_fields["name"] as LineEdit).text = "Disposable QA Corp %d" % int(app.auth_session.player_id)
					menu._inline_submit.pressed.emit()
				var created_corp := await _wait_until(func() -> bool:
					return (int(app.client_state.snapshot().get("player", {}).get("corp_id", 0)) > 0
						and app._pending_game_commands.is_empty() and not app._command_refresh_pending
						and not menu._busy),
					240
				)
				_check(created_corp, "corp.create did not persist and refresh the player's corporation membership")
		var corp_menu_refreshed := await _wait_until(func() -> bool:
			return (int(menu._snapshot.get("player", {}).get("corp_id", 0)) > 0
				and not app._command_refresh_pending and not menu._busy),
			120
		)
		_check(corp_menu_refreshed, "corporation category did not refresh from authoritative player state")
		if corp_menu_refreshed:
			menu.select_category("CORPORATION")
			await process_frame
			var deposit_button := _find_button(menu._action_list, "Deposit to treasury")
			_check(deposit_button != null and not deposit_button.disabled, "Corporation menu did not expose authorised treasury deposit")
			if deposit_button != null and not deposit_button.disabled:
				deposit_button.pressed.emit()
				if menu._form_fields.has("amount"):
					(menu._form_fields["amount"] as LineEdit).text = "100"
					menu._inline_submit.pressed.emit()
			var deposited := await _wait_until(func() -> bool:
				return app._pending_game_commands.is_empty() and not menu._busy,
				240
			)
			_check(deposited, "corp.deposit did not complete through the authorised Corporation menu")
	if OS.get_environment("TWCLONE_GODOT_FEEDBACK") == "1":
		# Use the actual Computer menu and player-facing feedback dialogs, including
		# the server-generated public preview and final submit confirmation.
		menu.select_category("COMPUTER")
		await process_frame
		var feedback_button := _find_button(menu._action_list, "Report Bug / Suggest Improvement")
		_check(feedback_button != null, "Computer menu did not expose player feedback")
		if feedback_button != null:
			feedback_button.pressed.emit()
			var fields: Dictionary = app._feedback_fields
			(fields["kind"] as OptionButton).select(1)
			(fields["subject"] as LineEdit).text = "Disposable Godot feedback acceptance"
			(fields["description"] as TextEdit).text = "Test-only suggestion submitted from the Godot UI."
			(app._feedback_form.get_ok_button() as Button).pressed.emit()
			var preview_ready := await _wait_until(func() -> bool: return app._feedback_preview.visible, 120)
			_check(preview_ready and app._feedback_preview_text.text.contains("Disposable Godot feedback acceptance"), "feedback preview was not returned to the actual UI")
			var report_id := str(app._feedback_payload.get("report_id", ""))
			_check(not report_id.is_empty(), "feedback preview did not supply an idempotent report reference")
			if preview_ready:
				_check(not app._feedback_preview_text.text.contains(test_password), "public feedback preview contains the player password")
				(app._feedback_preview.get_ok_button() as Button).pressed.emit()
				var submitted := await _wait_until(func() -> bool: return app._feedback_result.visible, 120)
				_check(submitted and app._feedback_result.dialog_text.contains(report_id), "feedback result did not display its local report reference")
				app._feedback_result.hide()
				var status_button := _find_button(menu._action_list, "Check feedback report status")
				_check(status_button != null, "Computer menu did not expose feedback status lookup")
				var status_ready := false
				if status_button != null:
					status_button.pressed.emit()
					app._feedback_status_input.text = report_id
					(app._feedback_status_form.get_ok_button() as Button).pressed.emit()
					status_ready = await _wait_until(func() -> bool: return app._feedback_result.visible, 120)
				_check(status_ready and app._feedback_result.dialog_text.contains(report_id), "feedback status was not returned through the Godot UI")
	var persisted_player_id := int(app.auth_session.player_id)
	var persisted_sector_id := int(app.client_state.snapshot().get("hud", {}).get("sector_id", 0))
	var persisted_turns := int(app.client_state.snapshot().get("hud", {}).get("turns_remaining", -1))
	app._feedback_result.hide()
	menu.select_category("COMPUTER")
	await process_frame
	var logout_button := _find_button(menu._action_list, "Log out to login screen")
	_check(logout_button != null, "Session menu did not expose logout")
	if logout_button != null:
		logout_button.pressed.emit()
		_check(menu._logout_dialog.visible, "logout action did not request explicit confirmation")
		(menu._logout_dialog.get_ok_button() as Button).pressed.emit()
		var logged_out := await _wait_until(func() -> bool:
			return not app.auth_session.is_authenticated() and app.login_view.visible,
			120
		)
		_check(logged_out, "logout did not return the player to the login view")
		if not login_existing_account:
			app.login_view.mode_toggle_button.pressed.emit() # registration -> login
		app.login_view.host_input.text = "127.0.0.1"
		app.login_view.port_input.text = OS.get_environment("TWCLONE_GODOT_PORT")
		app.login_view.username_input.text = registered_username
		app.login_view.password_input.text = test_password
		app.login_view.connect_button.pressed.emit()
		var reconnected := await _wait_until(func() -> bool:
			var snapshot: Dictionary = app.client_state.snapshot()
			var freshness: Dictionary = snapshot.get("freshness", {})
			return (app.auth_session.is_authenticated() and int(app.auth_session.player_id) == persisted_player_id
				and freshness.get("player") == "fresh" and freshness.get("ship") == "fresh" and freshness.get("sector") == "fresh"),
			240
		)
		_check(reconnected, "ordinary-player login did not reconnect to the persisted account")
		if reconnected:
			var resumed: Dictionary = app.client_state.snapshot()
			_check(int(resumed.get("hud", {}).get("sector_id", 0)) == persisted_sector_id, "reconnect did not restore the persisted sector")
			_check(int(resumed.get("hud", {}).get("turns_remaining", -1)) == persisted_turns, "reconnect changed the persisted turn balance")
		# End the accepted journey through the player-facing logout confirmation
		# as well, so the harness leaves no live socket or pending UI state behind.
		menu.select_category("COMPUTER")
		await process_frame
		var final_logout_button := _find_button(menu._action_list, "Log out to login screen")
		if final_logout_button != null:
			final_logout_button.pressed.emit()
			if menu._logout_dialog.visible:
				(menu._logout_dialog.get_ok_button() as Button).pressed.emit()
			await _wait_until(func() -> bool: return not app.auth_session.is_authenticated(), 120)
	var auth_journey := "existing-login" if login_existing_account else "registration"
	var feedback_journey := ",feedback" if OS.get_environment("TWCLONE_GODOT_FEEDBACK") == "1" else ""
	if failures.is_empty():
		print("LIVE_DISPOSABLE_PASS journey=%s,navigation,turns,scan,docking,port-workflow,commodity-buy,commodity-sale,hardware-purchase,shipyard-upgrade,planet-landing,harvest,launch,corporation,logout,reconnect%s player_id=%d origin=%d destination=%d ship_id=%d" % [auth_journey, feedback_journey, persisted_player_id, initial_sector, destination, ship_id])
	_finish()

func _warp_via_menu(destination: int) -> bool:
	var menu = app.gameplay_view.command_menu
	if int(app.client_state.snapshot().get("hud", {}).get("sector_id", 0)) == destination:
		return true
	menu.select_category("NAVIGATION")
	await process_frame
	var warp_button := _find_button(menu._action_list, "Warp to sector number")
	if warp_button == null or warp_button.disabled:
		return false
	warp_button.pressed.emit()
	if not menu._form_fields.has("to_sector_id"):
		return false
	(menu._form_fields["to_sector_id"] as LineEdit).text = str(destination)
	menu._inline_submit.pressed.emit()
	var response_ready := await _wait_until(func() -> bool:
		return app._toll_offer_dialog.visible or int(app.client_state.snapshot().get("hud", {}).get("sector_id", 0)) == destination,
		240
	)
	if not response_ready:
		return false
	if app._toll_offer_dialog.visible:
		app._toll_offer_dialog.get_ok_button().pressed.emit()
	return await _wait_until(func() -> bool:
		return (int(app.client_state.snapshot().get("hud", {}).get("sector_id", 0)) == destination
			and app._pending_game_commands.is_empty() and not app._command_refresh_pending),
		240
	)

func _perform_port_trade(buy_port_id: int, buy_sector: int, sell_port_id: int, sell_sector: int, return_sector: int) -> bool:
	if not await _warp_via_menu(buy_sector):
		return false
	if not await _dock_and_open_port(buy_port_id):
		return false
	var buy_controls: Dictionary = app.gameplay_view.port_workflow._row_controls.get(acceptance_trade_commodity, {})
	if not buy_controls.has("buy") or (buy_controls["buy"] as Button).disabled:
		return false
	var initial_quantity := _cargo_quantity(app.client_state.snapshot().get("ship", {}), acceptance_trade_commodity)
	(buy_controls["quantity"] as LineEdit).text = "2"
	(buy_controls["buy"] as Button).pressed.emit()
	var bought := await _wait_until(func() -> bool:
		return (_cargo_quantity(app.client_state.snapshot().get("ship", {}), acceptance_trade_commodity) == initial_quantity + 2
			and app._pending_game_commands.is_empty() and not app._command_refresh_pending),
		240
	)
	print("LIVE_STAGE trade_buy=%s commodity=%s port=%d sector=%d" % [bought, acceptance_trade_commodity, buy_port_id, buy_sector])
	if not bought or not await _undock_from_port(buy_port_id):
		return false
	if not await _warp_via_menu(sell_sector):
		return false
	if not await _dock_and_open_port(sell_port_id):
		return false
	var sell_controls: Dictionary = app.gameplay_view.port_workflow._row_controls.get(acceptance_trade_commodity, {})
	if not sell_controls.has("sell") or (sell_controls["sell"] as Button).disabled:
		return false
	(sell_controls["quantity"] as LineEdit).text = "1"
	(sell_controls["sell"] as Button).pressed.emit()
	var sold := await _wait_until(func() -> bool:
		return (_cargo_quantity(app.client_state.snapshot().get("ship", {}), acceptance_trade_commodity) == initial_quantity + 1
			and app._pending_game_commands.is_empty() and not app._command_refresh_pending),
		240
	)
	print("LIVE_STAGE trade_sell=%s commodity=%s port=%d sector=%d" % [sold, acceptance_trade_commodity, sell_port_id, sell_sector])
	if not sold or not await _undock_from_port(sell_port_id):
		return false
	return await _warp_via_menu(return_sector)

func _complete_release_smoke_reconnect(trade_passed: bool) -> void:
	var player_id := int(app.auth_session.player_id)
	var ship: Dictionary = app.client_state.snapshot().get("ship", {})
	var hud: Dictionary = app.client_state.snapshot().get("hud", {})
	var ship_id := int(hud.get("ship_id", 0))
	var sector_id := int(hud.get("sector_id", 0))
	var turns := int(hud.get("turns_remaining", -1))
	var traded_quantity := _cargo_quantity(ship, acceptance_trade_commodity)
	_check(trade_passed and traded_quantity > 0, "TLS release smoke did not complete a server-confirmed commodity purchase and sale")
	var menu = app.gameplay_view.command_menu
	menu.select_category("COMPUTER")
	await process_frame
	var logout_button := _find_button(menu._action_list, "Log out to login screen")
	_check(logout_button != null, "release smoke could not access the UI logout action")
	if logout_button == null:
		return
	logout_button.pressed.emit()
	if menu._logout_dialog.visible:
		menu._logout_dialog.get_ok_button().pressed.emit()
	var logged_out := await _wait_until(func() -> bool: return not app.auth_session.is_authenticated() and app.login_view.visible, 120)
	_check(logged_out, "release smoke logout did not return to LoginView")
	if not logged_out:
		return
	if not login_existing_account:
		app.login_view.mode_toggle_button.pressed.emit()
	app.login_view.host_input.text = OS.get_environment("TWCLONE_GODOT_HOST")
	app.login_view.port_input.text = OS.get_environment("TWCLONE_GODOT_PORT")
	app.login_view.username_input.text = registered_username
	app.login_view.password_input.text = test_password
	app.login_view.connect_button.pressed.emit()
	var reconnected := await _wait_until(func() -> bool:
		var snapshot: Dictionary = app.client_state.snapshot()
		var freshness: Dictionary = snapshot.get("freshness", {})
		return (app.auth_session.is_authenticated() and int(app.auth_session.player_id) == player_id
			and freshness.get("player") == "fresh" and freshness.get("ship") == "fresh" and freshness.get("sector") == "fresh"),
		240
	)
	_check(reconnected, "TLS release smoke did not reauthenticate the same account")
	if reconnected:
		var resumed: Dictionary = app.client_state.snapshot()
		_check(int(resumed.get("hud", {}).get("sector_id", 0)) == sector_id, "TLS release reconnect did not restore sector state")
		_check(int(resumed.get("hud", {}).get("turns_remaining", -1)) == turns, "TLS release reconnect changed turns")
		_check(int(resumed.get("hud", {}).get("ship_id", 0)) == ship_id, "TLS release reconnect changed active ship")
		_check(_cargo_quantity(resumed.get("ship", {}), acceptance_trade_commodity) == traded_quantity, "TLS release reconnect did not restore traded cargo")
	if failures.is_empty():
		print("LIVE_RELEASE_TLS_PASS registration,navigation,turns,scan,port-buy,port-sale,logout,reconnect player_id=%d ship_id=%d sector=%d %s=%d turns=%d tls=true username=%s" % [player_id, ship_id, sector_id, acceptance_trade_commodity, traded_quantity, turns, registered_username])

func _dock_and_open_port(port_id: int) -> bool:
	var menu = app.gameplay_view.command_menu
	var port_key := "port:%d" % port_id
	var visible := false
	for row in app.client_state.snapshot().get("sector", {}).get("ports", []):
		if row is Dictionary and int(row.get("id", row.get("port_id", 0))) == port_id:
			visible = true
	if not visible:
		return false
	app.gameplay_view.select_object_key(port_key)
	menu.open_for(app.client_state.snapshot(), app.gameplay_view.sector_view.selected_object())
	await process_frame
	var dock_button := _find_button(menu._action_list, "Dock at selected port")
	if dock_button == null or dock_button.disabled:
		return false
	dock_button.pressed.emit()
	if menu._form_dialog.visible:
		menu._form_dialog.get_ok_button().pressed.emit()
	if not await _wait_until(func() -> bool:
		return int(app.client_state.snapshot().get("ship", {}).get("ported", 0)) == port_id and not menu._busy,
		240
	):
		return false
	app.gameplay_view.select_object_key(port_key)
	menu.open_for(app.client_state.snapshot(), app.gameplay_view.sector_view.selected_object())
	await process_frame
	var open_button := _find_button(menu._action_list, "Dock · open port screen")
	if open_button == null or open_button.disabled:
		return false
	open_button.pressed.emit()
	return await _wait_until(func() -> bool:
		return app.gameplay_view.port_workflow.visible and not menu._busy,
		180
	)

func _undock_from_port(port_id: int) -> bool:
	var menu = app.gameplay_view.command_menu
	app.gameplay_view.port_workflow._back_button.pressed.emit()
	await process_frame
	app.gameplay_view.select_object_key("port:%d" % port_id)
	menu.open_for(app.client_state.snapshot(), app.gameplay_view.sector_view.selected_object())
	await process_frame
	var undock_button := _find_button(menu._action_list, "Undock from selected port")
	if undock_button == null or undock_button.disabled:
		return false
	undock_button.pressed.emit()
	if menu._form_dialog.visible:
		menu._form_dialog.get_ok_button().pressed.emit()
	return await _wait_until(func() -> bool:
		return (int(app.client_state.snapshot().get("ship", {}).get("ported", 0)) == 0
			and app._pending_game_commands.is_empty() and not app._command_refresh_pending),
		240
	)

func _perform_starport_services(port_id: int) -> bool:
	var menu = app.gameplay_view.command_menu
	var port_key := "port:%d" % port_id
	var visible := false
	for row in app.client_state.snapshot().get("sector", {}).get("ports", []):
		if row is Dictionary and int(row.get("id", row.get("port_id", 0))) == port_id:
			visible = true
	if not visible:
		return false
	app.gameplay_view.select_object_key(port_key)
	menu.open_for(app.client_state.snapshot(), app.gameplay_view.sector_view.selected_object())
	await process_frame
	var dock_button := _find_button(menu._action_list, "Dock at selected port")
	if dock_button == null or dock_button.disabled:
		return false
	dock_button.pressed.emit()
	if menu._form_dialog.visible:
		menu._form_dialog.get_ok_button().pressed.emit()
	var docked := await _wait_until(func() -> bool:
		return int(app.client_state.snapshot().get("ship", {}).get("ported", 0)) == port_id and not menu._busy,
		240
	)
	if not docked:
		return false
	app.gameplay_view.select_object_key(port_key)
	menu.open_for(app.client_state.snapshot(), app.gameplay_view.sector_view.selected_object())
	await process_frame
	var port_button := _find_button(menu._action_list, "Dock · open port screen")
	if port_button == null or port_button.disabled:
		return false
	port_button.pressed.emit()
	var port_open := await _wait_until(func() -> bool: return app.gameplay_view.port_workflow.visible and not menu._busy, 180)
	if not port_open:
		return false
	app.gameplay_view.port_workflow._back_button.pressed.emit()
	menu.select_category("TRADING")
	await process_frame
	var hardware_button := _find_button(menu._action_list, "Hardware catalogue")
	if hardware_button == null or hardware_button.disabled:
		return false
	hardware_button.pressed.emit()
	var hardware_ready := await _wait_until(func() -> bool: return menu._shipyard_dialog.visible and not menu._busy, 180)
	if not hardware_ready:
		return false
	var shield_button: Button
	for row in menu._shipyard_rows.get_children():
		if row is Button and str((row as Button).text).contains("[SHIELDS]"):
			shield_button = row as Button
	if shield_button == null or shield_button.disabled:
		return false
	var shields_before := int(app.client_state.snapshot().get("ship", {}).get("shields", 0))
	shield_button.pressed.emit()
	if not menu._form_fields.has("quantity"):
		return false
	(menu._form_fields["quantity"] as LineEdit).text = "1"
	menu._inline_submit.pressed.emit()
	var hardware_bought := await _wait_until(func() -> bool:
		return (int(app.client_state.snapshot().get("ship", {}).get("shields", 0)) > shields_before
			and app._pending_game_commands.is_empty() and not app._command_refresh_pending),
		240
	)
	if not hardware_bought:
		return false
	await process_frame
	app.gameplay_view.select_object_key(port_key)
	menu.open_for(app.client_state.snapshot(), app.gameplay_view.sector_view.selected_object())
	await process_frame
	var shipyard_button := _find_button(menu._action_list, "Shipyard · choose eligible hull")
	if shipyard_button == null or shipyard_button.disabled:
		return false
	shipyard_button.pressed.emit()
	var shipyard_ready := await _wait_until(func() -> bool: return menu._shipyard_dialog.visible and not menu._busy, 180)
	await process_frame
	if not shipyard_ready:
		return false
	var hull_button: Button
	for row in menu._shipyard_rows.get_children():
		if row is Button and not (row as Button).disabled:
			hull_button = row as Button
			break
	if hull_button == null:
		return false
	var old_ship_type := int(app.client_state.snapshot().get("ship", {}).get("type_id", 0))
	hull_button.pressed.emit()
	var selected_type := int(menu._shipyard_type_id)
	menu._shipyard_name_input.text = "Disposable QA Upgrade"
	menu._shipyard_name_dialog.get_ok_button().pressed.emit()
	var upgraded := await _wait_until(func() -> bool:
		var ship: Dictionary = app.client_state.snapshot().get("ship", {})
		return (selected_type > 0 and selected_type != old_ship_type
			and int(ship.get("type_id", 0)) == selected_type
			and str(ship.get("name", "")) == "Disposable QA Upgrade"
			and app._pending_game_commands.is_empty() and not app._command_refresh_pending),
		240
	)
	if not upgraded:
		return false
	app.gameplay_view.select_object_key(port_key)
	menu.open_for(app.client_state.snapshot(), app.gameplay_view.sector_view.selected_object())
	await process_frame
	var undock_button := _find_button(menu._action_list, "Undock from selected port")
	print("LIVE_STAGE stardock_undock_check ported=%s active_category=%s button_present=%s disabled=%s" % [
		str(app.client_state.snapshot().get("ship", {}).get("ported", "missing")),
		menu._active_category,
		str(undock_button != null),
		str(undock_button.disabled if undock_button != null else true),
	])
	if undock_button == null or undock_button.disabled:
		return false
	undock_button.pressed.emit()
	if menu._form_dialog.visible:
		menu._form_dialog.get_ok_button().pressed.emit()
	return await _wait_until(func() -> bool:
		return (int(app.client_state.snapshot().get("ship", {}).get("ported", 0)) == 0
			and app._pending_game_commands.is_empty() and not app._command_refresh_pending),
		240
	)

func _perform_planet_journey(planet_id: int) -> bool:
	var menu = app.gameplay_view.command_menu
	var planet_key := "planet:%d" % planet_id
	app.gameplay_view.select_object_key(planet_key)
	menu.open_for(app.client_state.snapshot(), app.gameplay_view.sector_view.selected_object())
	await process_frame
	var land_button := _find_button(menu._action_list, "Land on planet")
	if land_button == null or land_button.disabled:
		return false
	land_button.pressed.emit()
	var landed := await _wait_until(func() -> bool:
		return app._landing_surface_ready and app._landed_planet_id == planet_id and app.gameplay_view.planet_workflow.visible,
		240
	)
	if not landed:
		return false
	var surface_ready := await _wait_until(func() -> bool:
		return app._pending_game_commands.is_empty() and app._planet_pending_count == 0 and not menu._busy,
		240
	)
	if not surface_ready:
		return false
	app.gameplay_view.select_object_key(planet_key)
	menu.open_for(app.client_state.snapshot(), app.gameplay_view.sector_view.selected_object())
	await process_frame
	var harvest_button := _find_button(menu._action_list, "Harvest planet production")
	if harvest_button == null or harvest_button.disabled:
		return false
	var ore_before := _cargo_quantity(app.client_state.snapshot().get("ship", {}), "ORE")
	harvest_button.pressed.emit()
	if menu._form_dialog.visible:
		menu._form_dialog.get_ok_button().pressed.emit()
	var harvested := await _wait_until(func() -> bool:
		return (_cargo_quantity(app.client_state.snapshot().get("ship", {}), "ORE") == ore_before + 3
			and app._pending_game_commands.is_empty() and not app._command_refresh_pending),
		240
	)
	print("LIVE_STAGE planet_harvest=%s ore_before=%d ore_now=%d message=%s" % [harvested, ore_before, _cargo_quantity(app.client_state.snapshot().get("ship", {}), "ORE"), app.gameplay_view.notification_label.text])
	if not harvested or not app.gameplay_view.notification_label.text.contains("Planet harvest confirmed"):
		return false
	var mutation_settled := await _wait_until(func() -> bool:
		return app._pending_game_commands.is_empty() and app._planet_pending_count == 0 and not menu._busy,
		240
	)
	if not mutation_settled:
		return false
	app.gameplay_view.select_object_key(planet_key)
	menu.open_for(app.client_state.snapshot(), app.gameplay_view.sector_view.selected_object())
	await process_frame
	var launch_button := _find_button(menu._action_list, "Launch from planet")
	if launch_button == null or launch_button.disabled:
		return false
	launch_button.pressed.emit()
	if menu._form_dialog.visible:
		menu._form_dialog.get_ok_button().pressed.emit()
	return await _wait_until(func() -> bool:
		var snapshot: Dictionary = app.client_state.snapshot()
		return (int(snapshot.get("ship", {}).get("onplanet", 1)) == 0
			and app._landed_planet_id == 0 and not app.gameplay_view.planet_workflow.visible
			and app._pending_game_commands.is_empty() and not app._command_refresh_pending),
		240
	)

func _wait_until(predicate: Callable, frames: int) -> bool:
	for _frame in frames:
		if predicate.call():
			return true
		await process_frame
		# Headless tests can process hundreds of frames before the TCP poller or
		# PostgreSQL-backed server gets a scheduling slice. Keep real network time
		# in the acceptance harness instead of exhausting the frame budget early.
		await create_timer(0.02).timeout
	return predicate.call()

func _find_button(parent: Node, label: String) -> Button:
	for child in parent.get_children():
		if child is Button and (child as Button).text == label:
			return child
	return null

func _cargo_quantity(ship: Variant, commodity: String) -> int:
	if not ship is Dictionary:
		return -1
	var cargo: Variant = ship.get("cargo", [])
	if not cargo is Array:
		return -1
	var aliases := {"EQUIPMENT": "EQU", "ORGANICS": "ORG", "SLAVES": "SLV", "DRUGS": "DRG", "WEAPONS": "WPN"}
	for item in cargo:
		if item is Dictionary:
			var raw_code := str(item.get("commodity", item.get("code", item.get("commodity_code", "")))).to_upper()
			if str(aliases.get(raw_code, raw_code)) == commodity:
				return int(item.get("quantity", -1))
	return 0

func _check(condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)

func _finish() -> void:
	# Let deferred UI row removals and server-reply handlers settle before
	# tearing down the root scene. The live journey rebuilds several dynamic
	# menus while their prior action labels are queued for deletion.
	for _frame in 3:
		await process_frame
	if is_instance_valid(app):
		# Window nodes are top-level subwindows. Close and release them explicitly
		# so headless Godot does not leave their internal labels detached when the
		# parent scene is destroyed.
		for window in app.find_children("*", "Window", true, true):
			(window as Window).hide()
			window.queue_free()
		await process_frame
		var finished_app = app
		finished_app.queue_free()
		await finished_app.tree_exited
	for _frame in 6:
		await process_frame
	for child in root.get_children():
		if child != self:
			print("LIVE_CLEANUP_ROOT_CHILD type=%s name=%s path=%s queued=%s" % [child.get_class(), child.name, str(child.get_path()), str(child.is_queued_for_deletion())])
	await create_timer(0.1).timeout
	if failures.is_empty():
		quit(0)
	else:
		for failure in failures:
			push_error(failure)
		quit(1)
