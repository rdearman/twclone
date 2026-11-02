extends Control

const Protocol = preload("res://Protocol.gd")
const ProtocolTransport = preload("res://ProtocolTransport.gd")
const AuthSession = preload("res://AuthSession.gd")
const ClientState = preload("res://ClientState.gd")
const AuthoritativeRefresh = preload("res://AuthoritativeRefresh.gd")
const EventPresenter = preload("res://EventPresenter.gd")
const PlanetCombatPresenter = preload("res://PlanetCombatPresenter.gd")
const FerengiPresenter = preload("res://FerengiPresenter.gd")
const CONFIG_PATH := "user://client_config.cfg"

@onready var gameplay_view = $GameplayView
@onready var login_view = $LoginView

var transport
var auth_session
var client_state
var refresh_coordinator
var network_log_file: FileAccess
var server_ip := ""
var server_port := 0
var auth_username := ""
var server_profiles: Array[Dictionary] = []
var selected_server_profile := -1
var _login_password := ""
var _connection_started := false
var _authenticated_once := false
var _pending_game_commands: Dictionary = {}
var _pending_event_subscriptions: Dictionary = {}
var _registration_payload: Dictionary = {}
var _pending_registration_request_id := ""
var _warp_refresh_pending := false
var _command_refresh_pending := false
var _ship_roster_refresh_after_command := false
var _pending_trade: Dictionary = {}
var _landed_planet_id := 0
var _pending_landing_surface_id := 0
var _pending_launch_departure := false
var _landing_surface_ready := false
var _planet_pending_count := 0
var _planet_read_refused := false
var _activity_history: Array[String] = []
var _planned_route: Array[int] = []
var _route_destination := 0
var _route_running := false
var _route_stop_requested := false
var _route_stop_after_next := false
var _autopilot_mode := "continue"
var _route_total_hops := 0
var _route_completed_hops := 0
var _route_current_sector := 0

func _ready() -> void:
	transport = ProtocolTransport.new(15.0, 200)
	auth_session = AuthSession.new()
	client_state = ClientState.new()
	refresh_coordinator = AuthoritativeRefresh.new(transport, auth_session, client_state)
	transport.transport_connected.connect(_on_transport_connected)
	transport.transport_disconnected.connect(_on_transport_disconnected)
	transport.reply_received.connect(_on_transport_reply)
	transport.request_timed_out.connect(_on_request_timed_out)
	transport.request_failed.connect(_on_request_failed)
	transport.protocol_error.connect(_on_protocol_error)
	transport.protocol_warning.connect(_on_protocol_warning)
	transport.diagnostic.connect(_on_transport_diagnostic)
	auth_session.authentication_failed.connect(_on_authentication_failed)
	auth_session.session_invalidated.connect(_on_session_invalidated)
	client_state.changed.connect(_on_client_state_changed)
	refresh_coordinator.refresh_failed.connect(_on_refresh_failed)
	refresh_coordinator.refresh_finished.connect(_on_refresh_finished)
	login_view.login_requested.connect(_on_login_requested)
	login_view.registration_requested.connect(_on_registration_requested)
	login_view.connection_cancelled.connect(_cancel_connection_attempt)
	login_view.profiles_changed.connect(_on_profiles_changed)
	gameplay_view.reconnect_requested.connect(_on_reconnect_requested)
	gameplay_view.warp_requested.connect(_on_warp_requested)
	gameplay_view.warp_activated.connect(_on_warp_requested)
	gameplay_view.command_requested.connect(_on_command_requested)
	gameplay_view.trade_requested.connect(_on_trade_requested)
	gameplay_view.trade_confirmation.connect(_on_trade_confirmation)
	gameplay_view.tavern_enter_requested.connect(_on_tavern_enter_requested)
	gameplay_view.route_engage_requested.connect(_engage_planned_route)
	gameplay_view.route_cancel_requested.connect(_cancel_planned_route)
	_load_config()
	login_view.set_profiles(server_profiles, selected_server_profile)
	login_view.set_connection_values(server_ip if not server_ip.is_empty() else "127.0.0.1", server_port if server_port > 0 else 1234, auth_username)
	login_view.set_status("Enter your captain credentials to begin.")
	gameplay_view.visible = false
	login_view.visible = true
	# Temporary playtest log. Keep it in /tmp so it can be inspected directly
	# while the client is running without asking the player to locate Godot's
	# per-user application directory.
	network_log_file = FileAccess.open("/tmp/twclone-godot-client.log", FileAccess.WRITE)
	if network_log_file:
		log_network("CLIENT_START", "Illustrated client session started.")

func _process(delta: float) -> void:
	transport.poll(delta)
	for event in transport.drain_events():
		_present_server_event(event)

func _exit_tree() -> void:
	if transport:
		transport.close("Client closed.")
	if network_log_file:
		log_network("CLIENT_STOP", "Client session ended.")
		network_log_file.close()

func _on_login_requested(host: String, port: int, username: String, password: String) -> void:
	print("[AUTH_TRACE] Main received login signal host=%s port=%d username_present=%s password_present=%s" % [host, port, not username.strip_edges().is_empty(), not password.is_empty()])
	server_ip = host
	server_port = port
	auth_username = username
	selected_server_profile = login_view.get_selected_profile_index()
	_login_password = password
	_save_config()
	login_view.set_status("Connecting to %s:%d…" % [server_ip, server_port], true)
	if transport.is_transport_connected():
		_submit_login()
		return
	_connection_started = true
	var result: Error = transport.connect_to_server(server_ip, server_port)
	if result != OK:
		_connection_started = false
		login_view.set_status("Could not start the connection. Check the address and try again.")
		login_view.set_connection_values(server_ip, server_port, auth_username)

func _on_transport_connected() -> void:
	print("[AUTH_TRACE] Main transport_connected registration_pending=%s" % [not _registration_payload.is_empty()])
	_connection_started = false
	if not _registration_payload.is_empty():
		_submit_registration()
		return
	_submit_login()

func _submit_login() -> void:
	var payload: Dictionary = auth_session.begin_login(auth_username, _login_password)
	_login_password = ""
	if payload.is_empty():
		login_view.set_status("Enter your captain name and password.")
		return
	login_view.set_status("Authenticating captain…", true)
	print("[AUTH_TRACE] Main submitting auth.login payload_ready=%s" % [not payload.is_empty()])
	transport.request("auth.login", payload)

func _on_registration_requested(host: String, port: int, username: String, password: String, ship_name: String, ui_locale: String, ui_timezone: String) -> void:
	if _connection_started or not _pending_registration_request_id.is_empty():
		login_view.registration_result("A connection or registration request is already in progress.")
		return
	server_ip = host
	server_port = port
	auth_username = username
	selected_server_profile = login_view.get_selected_profile_index()
	_save_config()
	_registration_payload = {"username": username, "passwd": password}
	if not ship_name.is_empty():
		_registration_payload["ship_name"] = ship_name
	if not ui_locale.is_empty():
		_registration_payload["ui_locale"] = ui_locale
	if not ui_timezone.is_empty():
		_registration_payload["ui_timezone"] = ui_timezone
	login_view.registration_result("Connecting to create your account…", true)
	if transport.is_transport_connected():
		_submit_registration()
		return
	_connection_started = true
	var result: Error = transport.connect_to_server(server_ip, server_port)
	if result != OK:
		_connection_started = false
		_registration_payload.clear()
		login_view.registration_result("Could not start the connection. Check the address and try again.")

func _cancel_connection_attempt() -> void:
	if not _connection_started and _pending_registration_request_id.is_empty() and not auth_session.login_in_flight():
		return
	_registration_payload.clear()
	_pending_registration_request_id = ""
	_login_password = ""
	_connection_started = false
	transport.close("Connection attempt cancelled.")
	login_view.registration_result("Connection attempt cancelled. Correct the address or port and try again.")

func _submit_registration() -> void:
	if _registration_payload.is_empty():
		return
	var payload := _registration_payload.duplicate(true)
	_registration_payload.clear()
	var request_id: String = transport.request("auth.register", payload)
	if request_id.is_empty():
		login_view.registration_result("Registration was not sent. Check the connection and try again.")
		return
	_pending_registration_request_id = request_id
	login_view.registration_result("Creating account securely…", true)

func _on_transport_disconnected(reason: String) -> void:
	_connection_started = false
	_registration_payload.clear()
	_pending_registration_request_id = ""
	_pending_game_commands.clear()
	_pending_event_subscriptions.clear()
	_pending_trade.clear()
	gameplay_view.dismiss_trade_quote()
	gameplay_view.port_workflow.close_for_location_change()
	gameplay_view.command_menu.set_tavern_access(false)
	_route_running = false
	_planned_route.clear()
	_route_destination = 0
	gameplay_view.clear_planned_route()
	_command_refresh_pending = false
	_warp_refresh_pending = false
	_planet_pending_count = 0
	if _pending_landing_surface_id > 0:
		# A successful landing envelope is not sufficient to claim a surface:
		# player/ship reconciliation may still be in flight.  If transport is
		# lost at this point, discard only that unconfirmed transition so a later
		# reconnect refresh cannot open stale planet actions.
		_pending_landing_surface_id = 0
		_landed_planet_id = 0
		_landing_surface_ready = false
		gameplay_view.leave_planet_workflow()
	_pending_launch_departure = false
	gameplay_view.set_command_busy(false)
	refresh_coordinator.handle_disconnect()
	auth_session.invalidate("The connection was lost.")
	if _authenticated_once:
		client_state.mark_disconnected()
		gameplay_view.set_connection_notice(true, "DISCONNECTED · %s · showing last confirmed state" % reason)
		gameplay_view.visible = true
		login_view.set_overlay_mode(true)
		login_view.visible = true
		login_view.set_connection_values(server_ip, server_port, auth_username)
		login_view.set_status("Reconnect to refresh the displayed state.")
	else:
		login_view.set_overlay_mode(false)
		login_view.visible = true
		login_view.set_status("Connection failed: %s Check the server address or port and retry." % reason)
	if network_log_file:
		log_network("DISCONNECTED", reason)

func _on_transport_reply(request_id: String, response: Dictionary, command: String) -> void:
	print("[AUTH_TRACE] Main received reply command=%s request_id_present=%s response_type=%s" % [command, not request_id.is_empty(), str(response.get("type", ""))])
	var reply_summary := "command=%s type=%s status=%s request_id_present=%s" % [command, str(response.get("type", "")), str(response.get("status", "")), not request_id.is_empty()]
	if command in ["move.scan", "sector.info"]:
		log_network("REPLY", "%s response=%s" % [reply_summary, Protocol.diagnostic_json(response)])
	else:
		log_network("REPLY", reply_summary)
	if request_id == _pending_registration_request_id:
		_pending_registration_request_id = ""
		var registration_result: Dictionary = Protocol.result_from_response(response)
		if registration_result.get("kind") != "ok":
			var error_value = response.get("error", {})
			var error_code := int(error_value.get("code", 0)) if error_value is Dictionary else 0
			if error_code == 1105:
				login_view.registration_result("Registration refused · that captain name is already in use.")
			else:
				login_view.registration_result("Registration refused. Check the captain name and required fields.")
			return
		if not auth_session.handle_response(response):
			login_view.registration_result("The server did not return a usable account session.")
			return
		_on_authenticated()
		gameplay_view.show_notification("Welcome, captain · your account is ready.")
		return
	if _pending_event_subscriptions.has(request_id):
		var topic: String = str(_pending_event_subscriptions[request_id])
		_pending_event_subscriptions.erase(request_id)
		var subscription_result: Dictionary = Protocol.result_from_response(response)
		if subscription_result.get("kind") != "ok":
			gameplay_view.show_notification("Live event feed unavailable · %s" % str(subscription_result.get("message", "subscription refused")))
		return
	if _pending_game_commands.has(request_id):
		var operation: Dictionary = _pending_game_commands[request_id]
		_pending_game_commands.erase(request_id)
		if operation.get("kind") == "autopilot_status":
			_finish_autopilot_status(response)
		elif operation.get("kind") == "tavern_probe":
			var tavern_result: Dictionary = Protocol.result_from_response(response)
			gameplay_view.set_command_busy(false)
			if tavern_result.get("kind") == "ok":
				gameplay_view.port_workflow.confirm_tavern_entered()
			else:
				gameplay_view.show_notification("No tavern is available here · remaining at the port.")
		elif operation.get("kind") == "planet_info" or operation.get("kind") == "planet_colonists":
			var planet_result: Dictionary = Protocol.result_from_response(response)
			if planet_result.get("kind") == "ok":
				var planet_data := _response_data_dictionary(planet_result.get("data", {}))
				var reported_planet_id := _response_planet_id(planet_data)
				if reported_planet_id <= 0 or reported_planet_id != _landed_planet_id:
					_planet_read_refused = true
					_invalidate_planet_surface_context()
					var record_kind := "information" if operation.get("kind") == "planet_info" else "colony"
					gameplay_view.show_notification("Planet %s was not displayed because the response omitted or changed the confirmed planet ID." % record_kind)
				else:
					if operation.get("kind") == "planet_info":
						gameplay_view.set_planet_information(planet_data)
					else:
						gameplay_view.set_planet_colonist_state(planet_data)
			else:
				_planet_read_refused = true
				_invalidate_planet_surface_context()
				gameplay_view.show_notification("Planet records unavailable · %s" % str(planet_result.get("message", "The server refused the request.")))
			_planet_pending_count = maxi(0, _planet_pending_count - 1)
			if _planet_pending_count == 0 and not _command_refresh_pending:
				if not _planet_read_refused:
					gameplay_view.set_command_busy(false)
				else:
					# A refused planet read can mean the landing/location changed
					# while the surface was open. Reconcile server-owned state before
					# allowing another surface action; never treat the failed payload
					# as a local landing or planet record.
					_refresh_after_command(["player", "ship", "sector"])
		elif operation.get("kind") == "warp":
			await _finish_warp_reply(response, int(operation["destination"]))
		elif operation.get("kind") == "trade_quote":
			_finish_trade_quote(response)
		elif operation.get("kind") == "port_refresh":
			var port_result: Dictionary = Protocol.result_from_response(response)
			gameplay_view.set_command_busy(false)
			if port_result.get("kind") == "ok" and _port_response_type_matches(response):
				var port_data := _response_data_dictionary(port_result.get("data", {}))
				var refresh_sector_id := _authoritative_positive_id(operation.get("sector_id", 0))
				if _accept_authoritative_port(port_data, "Port refresh", 0, refresh_sector_id):
					gameplay_view.enter_port_workflow(port_data)
				else:
					_refresh_after_command(["sector"])
			else:
				gameplay_view.port_workflow.close_for_location_change()
				var reason := str(port_result.get("message", "No current port data."))
				if port_result.get("kind") == "ok":
					reason = "the server returned an unexpected port response type"
				gameplay_view.show_notification("Port refresh refused · %s" % reason)
				# A refused port read may mean the local location is stale. Reconcile
				# only the authoritative sector record before allowing another port
				# action; never retain or merge the refused response payload.
				_refresh_after_command(["sector"])
		elif operation.get("kind") == "route_warp":
			_finish_route_warp(response, operation)
		elif operation.get("kind") == "route_stop_confirmation":
			var stop_result: Dictionary = Protocol.result_from_response(response)
			gameplay_view.set_command_busy(false)
			if stop_result.get("kind") == "ok":
				gameplay_view.show_notification("Saved route stopped after the last confirmed warp.")
			else:
				gameplay_view.show_notification("Local route stopped, but the server could not confirm the saved-route stop · %s" % str(stop_result.get("message", "request failed")))
			_refresh_after_warp_attempt()
		elif operation.get("kind") == "communication_history":
			_finish_communication_history_refresh(response)
		elif operation.get("kind") == "mail_inbox_refresh":
			_finish_mail_inbox_refresh(response)
		elif operation.get("kind") == "ship_roster_refresh":
			await _finish_command_reply(response, operation)
		else:
			await _finish_command_reply(response, operation)
		return
	if refresh_coordinator.accepts_request(request_id):
		refresh_coordinator.handle_reply(request_id, response, command)
		return
	if command == "auth.login" or str(response.get("type", "")) == "auth.session":
		var auth_accepted: bool = auth_session.handle_response(response)
		print("[AUTH_TRACE] Main auth.session validation accepted=%s" % [auth_accepted])
		if not auth_accepted:
			return
		_on_authenticated()
		return
	var result: Dictionary = Protocol.result_from_response(response)
	if result.get("kind") != "ok":
		gameplay_view.show_notification(str(result.get("message", "The command was refused.")))

func _on_authenticated() -> void:
	_authenticated_once = true
	login_view.visible = false
	gameplay_view.visible = true
	gameplay_view.set_connection_notice(false)
	client_state.mark_authenticated()
	_save_config()
	_subscribe_to_player_events()
	_request_autopilot_status()
	if _landed_planet_id > 0:
		if not refresh_coordinator.refresh(false, ["player", "ship"]):
			gameplay_view.show_notification("Unable to refresh player and ship state.")
		_request_planet_surface_records()
	elif not refresh_coordinator.refresh():
		gameplay_view.show_notification("Unable to refresh the current state.")

func _on_authentication_failed(message: String) -> void:
	login_view.set_overlay_mode(_authenticated_once)
	login_view.visible = true
	login_view.set_connection_values(server_ip, server_port, auth_username)
	login_view.set_status("Login refused: %s" % message)
	if _authenticated_once:
		gameplay_view.set_connection_notice(true, "DISCONNECTED · showing last confirmed state")

func _on_request_timed_out(request_id: String, command: String) -> void:
	if command == "auth.login":
		auth_session.fail_request("Login timed out. Try again.")
	elif command == "auth.register":
		if request_id == _pending_registration_request_id:
			_pending_registration_request_id = ""
			login_view.registration_result("Registration timed out after 15 seconds. Check the server address or port and retry.")
	elif command == "auth.logout":
		_pending_game_commands.erase(request_id)
		gameplay_view.set_command_busy(false)
		gameplay_view.show_notification("Logout was not confirmed. Your session is still shown as active; retry or reconnect before closing the client.")
	elif _pending_game_commands.has(request_id) and _pending_game_commands[request_id].get("kind") == "tavern_probe":
		_pending_game_commands.erase(request_id)
		gameplay_view.set_command_busy(false)
		gameplay_view.show_notification("Tavern access could not be verified · remaining at the port.")
	elif _pending_game_commands.has(request_id) and _pending_game_commands[request_id].get("kind") == "route_warp":
		_pending_game_commands.erase(request_id)
		_route_running = false
		_route_stop_requested = false
		_planned_route.clear()
		gameplay_view.clear_planned_route()
		gameplay_view.show_notification("Autonav stopped · warp timed out, so the current sector is uncertain. Reconnecting will refresh it.")
		_refresh_after_warp_attempt()
	elif command == "move.warp":
		_pending_game_commands.erase(request_id)
		gameplay_view.show_notification("Warp response timed out. Checking the server's current state…")
		_refresh_after_warp_attempt()
	elif command == "move.transwarp":
		_pending_game_commands.erase(request_id)
		# A timeout does not establish whether transwarp relocated the ship or
		# applied entry hazards. Reconcile every authoritative domain affected by
		# either outcome before allowing another command; never infer the result
		# from the requested destination.
		gameplay_view.set_command_busy(false)
		gameplay_view.show_notification("Transwarp response timed out. Refreshing authoritative player, ship and sector state…")
		_refresh_after_command(["player", "ship", "sector"])
	elif command == "planet.launch":
		_pending_game_commands.erase(request_id)
		# Launch may commit entry hazards before its reply is lost. Keep the
		# confirmed surface context until the server refresh identifies the
		# resulting player/ship/sector state.
		gameplay_view.set_command_busy(false)
		gameplay_view.show_notification("Launch response timed out. Refreshing authoritative player, ship and sector state…")
		_refresh_after_command(["player", "ship", "sector"])
	elif _pending_game_commands.has(request_id):
		var timed_out: Dictionary = _pending_game_commands[request_id]
		_pending_game_commands.erase(request_id)
		if timed_out.get("kind") in ["planet_info", "planet_colonists"]:
			_planet_pending_count = maxi(0, _planet_pending_count - 1)
			_invalidate_planet_surface_context()
			gameplay_view.show_notification("Planet record request timed out. Refreshing confirmed player, ship and sector state.")
			if _planet_pending_count == 0 and not _command_refresh_pending:
				_refresh_after_command(["player", "ship", "sector"])
			return
		if str(timed_out.get("command", "")) == "planet.colonists.set":
			gameplay_view.show_notification("Colonist-transfer reply timed out; checking the confirmed planet and ship state.")
			_request_planet_surface_records()
			_refresh_after_command(["player", "ship", "sector"])
			return
		if _is_planet_surface_mutation(str(timed_out.get("command", ""))):
			gameplay_view.show_notification("%s timed out; checking the confirmed planet and player/ship state." % str(timed_out.get("label", command)))
			_reconcile_planet_surface_mutation()
			return
		if str(timed_out.get("command", "")) == "planet.land":
			# A timeout does not establish whether the server consumed the landing
			# request. Reconcile every affected domain without opening a surface.
			# The previous surface is no longer safe either: the request may have
			# replaced it on the server before the reply was lost.
			_invalidate_planet_surface_context()
			gameplay_view.show_notification("Landing outcome is uncertain · refreshing confirmed player, ship and sector state.")
			_refresh_after_command(["player", "ship", "sector"])
			return
		if str(timed_out.get("command", "")) in ["move.scan", "move.describe_sector", "sector.info", "sector.scan", "sector.scan.density"]:
			# A scan is read-only, but a lost reply must not be treated as a
			# successful disclosure. Reconcile only the current sector and keep
			# player/ship state untouched by this uncertain read.
			gameplay_view.show_notification("%s timed out · refreshing confirmed sector state." % str(timed_out.get("label", command)))
			_refresh_after_command(["sector"])
			return
		if str(timed_out.get("command", "")) == "combat.attack_planet":
			# Capture/loss and fighter depletion may have committed before the
			# response was lost. Do not leave the previous ownership snapshot
			# actionable or infer the result from the request alone.
			gameplay_view.set_command_busy(false)
			gameplay_view.show_notification("Planet attack outcome is uncertain; refreshing authoritative player, ship and sector state…")
			_refresh_after_command(["player", "ship", "sector"])
			return
		if str(timed_out.get("command", "")) == "ship.transfer":
			# Ownership may have committed before the reply was lost. A timeout is
			# not a client-side refusal or success variant: reconcile every affected
			# domain and request the authoritative owned-roster response before
			# offering another lifecycle action.
			gameplay_view.set_command_busy(false)
			_ship_roster_refresh_after_command = true
			gameplay_view.show_notification("Ship transfer outcome is uncertain; refreshing authoritative ownership state…")
			_refresh_after_command(["player", "ship", "sector"])
			return
		if timed_out.get("kind") == "trade_quote":
			_pending_trade.clear()
			gameplay_view.set_command_busy(false)
			# A quote timeout leaves the cached port snapshot untrusted: the
			# server may have changed stock or fixed buy/sell mode while the
			# request was in flight. Reconcile the port before offering another
			# trade, just as refusal and malformed-quote paths do.
			gameplay_view.show_notification("Trade quote timed out. No trade was made; refreshing port stock.")
			_request_port_refresh()
			return
		if timed_out.get("kind") == "port_refresh":
			_pending_trade.clear()
			gameplay_view.set_command_busy(false)
			gameplay_view.port_workflow.close_for_location_change()
			gameplay_view.show_notification("Port inventory refresh timed out; reopen the port to obtain current data.")
			return
		if timed_out.get("kind") == "communication_history":
			gameplay_view.set_command_busy(false)
			gameplay_view.show_notification("Message delivery was confirmed, but chat history could not be refreshed. Reopen Chat history to reconcile it.")
			return
		if timed_out.get("kind") == "mail_inbox_refresh":
			gameplay_view.set_command_busy(false)
			gameplay_view.show_notification("Mail change was confirmed, but the inbox could not be refreshed. Reopen Mail inbox to reconcile it.")
			return
		if str(timed_out.get("command", "")) in ["trade.buy", "trade.sell"]:
			_pending_trade["refresh_port_after"] = true
		else:
			gameplay_view.set_command_busy(false)
		gameplay_view.show_notification("%s timed out. The outcome is uncertain; refreshing authoritative state." % str(timed_out.get("label", command)))
		_refresh_after_command()
	elif refresh_coordinator.is_active():
		gameplay_view.show_notification("Refresh timed out. Showing the last confirmed state.")

func _on_request_failed(request_id: String, command: String, result: Dictionary) -> void:
	if refresh_coordinator.accepts_request(request_id):
		refresh_coordinator.handle_failure(request_id, command, result)
		return
	if command == "auth.login":
		auth_session.fail_request(str(result.get("message", "Login failed.")))
	elif command == "auth.register":
		if request_id == _pending_registration_request_id:
			_pending_registration_request_id = ""
			login_view.registration_result("Registration could not complete: %s Correct the address or port and try again." % str(result.get("message", "the server did not respond.")))
	elif _pending_game_commands.has(request_id) and _pending_game_commands[request_id].get("kind") == "route_warp":
		var interrupted_destination := int(_pending_game_commands[request_id].get("destination", 0))
		_pending_game_commands.erase(request_id)
		_route_running = false
		_route_stop_requested = false
		if interrupted_destination > 0:
			_planned_route.push_front(interrupted_destination)
			_route_destination = int(_planned_route.back())
			gameplay_view.show_restored_autopilot_route(_planned_route, _route_destination, "interrupted", _autopilot_mode)
		else:
			gameplay_view.clear_planned_route()
		gameplay_view.show_notification("Autonav paused · warp could not be confirmed. The remaining route is preserved; refresh location, then resume.")
		_refresh_after_warp_attempt()
	elif command == "move.warp":
		if result.get("kind") == "timeout":
			return # Timeout path already started an authoritative location refresh.
		_pending_game_commands.erase(request_id)
		# A transport failure can happen after the server has committed movement
		# or entry-hazard effects but before the reply reaches the client.  Do not
		# leave the pre-warp player/ship/sector snapshot actionable.
		gameplay_view.show_notification("Warp could not be confirmed. Refreshing authoritative state.")
		_refresh_after_warp_attempt()
	elif command == "move.transwarp":
		if result.get("kind") == "timeout":
			return # Timeout path already started an authoritative state refresh.
		_pending_game_commands.erase(request_id)
		# A transport failure may arrive after transwarp relocation or entry
		# hazards committed. Never leave the pre-move HUD actionable without a
		# server refresh, and never infer a destination from the failed request.
		gameplay_view.set_action_pending(false)
		gameplay_view.show_notification("Transwarp could not be confirmed. Refreshing authoritative player, ship and sector state.")
		_refresh_after_command(["player", "ship", "sector"])
	elif command == "planet.launch":
		if result.get("kind") == "timeout":
			return # Timeout path already started an authoritative state refresh.
		_pending_game_commands.erase(request_id)
		# Launch can apply entry hazards before its reply is lost. Keep the
		# confirmed surface context until the server tells us where the ship is.
		gameplay_view.set_command_busy(false)
		gameplay_view.show_notification("Launch could not be confirmed. Refreshing authoritative player, ship and sector state.")
		_refresh_after_command(["player", "ship", "sector"])
	elif _pending_game_commands.has(request_id):
		var failed_operation: Dictionary = _pending_game_commands[request_id]
		_pending_game_commands.erase(request_id)
		if failed_operation.get("kind") == "tavern_probe":
			gameplay_view.set_command_busy(false)
			gameplay_view.show_notification("Tavern access could not be verified · remaining at the port.")
			return
		if failed_operation.get("kind") in ["planet_info", "planet_colonists"]:
			_planet_read_refused = true
			_planet_pending_count = maxi(0, _planet_pending_count - 1)
			_invalidate_planet_surface_context()
			gameplay_view.show_notification("Planet record request failed · %s" % str(result.get("message", "request failed")))
			if _planet_pending_count == 0 and not _command_refresh_pending:
				_refresh_after_command(["player", "ship", "sector"])
			return
		gameplay_view.set_command_busy(false)
		if failed_operation.get("kind") == "port_refresh":
			_pending_trade.clear()
			gameplay_view.port_workflow.close_for_location_change()
			gameplay_view.show_notification("Port inventory could not be refreshed; reopen the port to obtain current data.")
			return
		if failed_operation.get("kind") == "trade_quote":
			_pending_trade.clear()
			gameplay_view.show_notification("Trade quote failed. No trade was made; refreshing port stock.")
			_request_port_refresh()
			return
		if failed_operation.get("kind") == "communication_history":
			gameplay_view.set_command_busy(false)
			gameplay_view.show_notification("Message delivery was confirmed, but chat history refresh failed. Reopen Chat history to reconcile it.")
			return
		if failed_operation.get("kind") == "mail_inbox_refresh":
			gameplay_view.set_command_busy(false)
			gameplay_view.show_notification("Mail change was confirmed, but the inbox refresh failed. Reopen Mail inbox to reconcile it.")
			return
		if str(failed_operation.get("command", "")) in ["ship.repair", "shipyard.upgrade", "hardware.buy"]:
			# A refusal is authoritative for this request, but the pre-request
			# credits/hull snapshot may already be stale. Reconcile the affected
			# shipyard domains before offering another lifecycle action.
			gameplay_view.show_notification("%s refused · refreshing confirmed credits and ship state." % str(failed_operation.get("label", command)))
			_refresh_after_command(["player", "ship"])
			return
		if str(failed_operation.get("command", "")) in ["move.scan", "move.describe_sector", "sector.info", "sector.scan", "sector.scan.density"]:
			gameplay_view.show_notification("%s failed · %s · refreshing confirmed sector state." % [str(failed_operation.get("label", command)), str(result.get("message", "request failed"))])
			_refresh_after_command(["sector"])
			return
		if str(failed_operation.get("command", "")) == "planet.land":
			# A lost reply does not prove whether the server consumed the landing
			# request. Reconcile every state domain that landing can affect and do
			# not open a planet surface from the uncertain local operation. Clear
			# any previously confirmed surface first; it may now describe the
			# pre-request location and must not remain actionable during refresh.
			_invalidate_planet_surface_context()
			gameplay_view.show_notification("Landing outcome is uncertain · %s · refreshing confirmed player, ship and sector state." % str(result.get("message", "request failed")))
			_refresh_after_command(["player", "ship", "sector"])
			return
		if str(failed_operation.get("command", "")) == "combat.attack_planet":
			# A transport failure does not prove that the server refused the
			# attack. Reconcile the domains changed by either capture or loss.
			gameplay_view.show_notification("Planet attack outcome is uncertain · refreshing authoritative player, ship and sector state.")
			_refresh_after_command(["player", "ship", "sector"])
			return
		if _is_planet_surface_mutation(str(failed_operation.get("command", ""))):
			gameplay_view.show_notification("%s could not be confirmed; refreshing the confirmed planet and player/ship state." % str(failed_operation.get("label", command)))
			_reconcile_planet_surface_mutation()
			return
		if str(failed_operation.get("command", "")) in ["trade.buy", "trade.sell"]:
			_pending_trade.clear()
			if result.get("kind") == "disconnected":
				gameplay_view.show_notification("Trade outcome is uncertain after disconnect. Reconnect to refresh your balances and cargo before trading again.")
				return
			# A request failure is not proof that the server rejected the
			# mutation: the command may have committed before the transport
			# reported its failure. Reconcile the affected authoritative domains
			# before presenting another trade, then refresh the selected port from
			# the confirmed location. Never synthesize a receipt or retry locally.
			_pending_trade["refresh_port_after"] = true
			_refresh_after_command(["player", "ship", "sector"])
			gameplay_view.show_notification("Trade outcome is uncertain · refreshing confirmed balances, cargo and port stock.")
			return
		gameplay_view.show_notification("%s failed · %s" % [str(failed_operation.get("label", command)), str(result.get("message", "request failed"))])

func _on_refresh_failed(snapshot: Dictionary) -> void:
	var freshness: Dictionary = snapshot.get("freshness", {})
	gameplay_view.show_notification("State is %s. Some sector information may be unavailable." % str(freshness.get("overall", "partially available")))
	if _warp_refresh_pending:
		_warp_refresh_pending = false
		gameplay_view.set_action_pending(false)
	if _command_refresh_pending:
		_command_refresh_pending = false
		if _ship_roster_refresh_after_command:
			_ship_roster_refresh_after_command = false
			# The previous roster is no longer safe to use after an ownership
			# mutation whose reconciliation failed.  Do not leave its IDs
			# actionable while the server state is unknown.
			gameplay_view.command_menu.set_owned_ship_roster({})
			gameplay_view.set_command_busy(false)
			gameplay_view.show_notification("Ownership state was refreshed, but the owned-ship roster could not be confirmed.")
		if _pending_launch_departure:
			_pending_launch_departure = false
			_landing_surface_ready = false
			gameplay_view.set_command_busy(false)
			gameplay_view.show_notification("Launch confirmed, but the authoritative refresh was incomplete; keeping planet actions unavailable until state is refreshed.")
		if _pending_landing_surface_id > 0:
			_pending_landing_surface_id = 0
			# The landing reply was not enough to make the surface actionable:
			# player/ship refresh did not complete. Do not retain the newly
			# claimed planet ID as reconnectable local state.
			_landed_planet_id = 0
			_landing_surface_ready = false
			# A prior surface may still be visible when a new landing was
			# attempted from a stale context. Once reconciliation fails, that
			# surface is no longer safe to use either; fail closed instead of
			# leaving stale planet controls actionable.
			gameplay_view.leave_planet_workflow()
			gameplay_view.show_notification("Landing confirmed, but player and ship refresh was incomplete; planet actions remain unavailable until state is refreshed.")
		if _pending_trade.get("refresh_port_after") == true:
			_pending_trade.erase("refresh_port_after")
			_request_port_refresh()
		else:
			gameplay_view.set_command_busy(false)
		gameplay_view.show_notification("Command completed, but the refreshed state could not be confirmed.")

func _on_refresh_finished(snapshot: Dictionary) -> void:
	if _warp_refresh_pending:
		_warp_refresh_pending = false
		gameplay_view.set_action_pending(false)
	if _command_refresh_pending:
		_command_refresh_pending = false
		if _ship_roster_refresh_after_command:
			_ship_roster_refresh_after_command = false
			gameplay_view.command_menu.set_owned_ship_roster({})
			_request_ship_roster_refresh()
			return
		if _pending_launch_departure:
			_finish_pending_launch_departure(snapshot)
			return
		if _pending_landing_surface_id > 0:
			# A partial or schema-mismatched refresh must fail closed.  Do not let
			# typed locals turn an unusable server payload into a client error or
			# expose planet actions before player/ship state is authoritative.
			var freshness_value: Variant = snapshot.get("freshness", {})
			var freshness: Dictionary = freshness_value if freshness_value is Dictionary else {}
			var player_fresh := str(freshness.get("player", "")) == ClientState.FRESH
			var ship_fresh := str(freshness.get("ship", "")) == ClientState.FRESH
			var sector_fresh := str(freshness.get("sector", "")) == ClientState.FRESH
			var player_value: Variant = snapshot.get("player", {})
			var player_snapshot: Dictionary = player_value if player_value is Dictionary else {}
			# Freshness alone does not bind the refresh to this authenticated
			# session. A success-shaped record for another captain must not
			# authorize a planetary surface.
			var refreshed_player_id := _authoritative_positive_id(player_snapshot.get("id", 0))
			var session_player_id := _authoritative_positive_id(auth_session.player_id)
			var player_identity_matches := refreshed_player_id > 0 and session_player_id > 0 and refreshed_player_id == session_player_id
			var ship_value: Variant = snapshot.get("ship", {})
			var ship_snapshot: Dictionary = ship_value if ship_value is Dictionary else {}
			# ship.status is authoritative for whether the active ship is actually
			# on a planet.  A successful landing envelope alone must not expose
			# surface actions if the subsequent state refresh still says the ship
			# remains in space (for example after a partial/contradictory update).
			var landing_id := _pending_landing_surface_id
			# The authoritative ship field stores the landed planet's ID, not a
			# boolean flag.  Require that exact ID so a stale/contradictory ship
			# refresh cannot open a different planet surface.
			var landed_planet_id := _authoritative_positive_id(ship_snapshot.get("onplanet", 0))
			var ship_on_planet := landed_planet_id == landing_id
			_pending_landing_surface_id = 0
			if player_fresh and player_identity_matches and ship_fresh and sector_fresh and ship_on_planet and _landed_planet_id == landing_id:
				_landing_surface_ready = true
				await gameplay_view.play_warp_transition()
				gameplay_view.enter_planet_workflow(landing_id)
				gameplay_view.show_notification("Landing confirmed · planet surface opened.")
				_request_planet_surface_records()
			else:
				_landing_surface_ready = false
				# Freshness alone does not establish that the landing completed. Clear
				# the locally pending identity when the active ship is elsewhere or
				# the required refresh domains are incomplete; retaining it could
				# make a later refresh appear to confirm a stale landing.
				_landed_planet_id = 0
				gameplay_view.leave_planet_workflow()
				gameplay_view.set_command_busy(false)
				gameplay_view.show_notification("Landing confirmed, but player and ship refresh was incomplete; planet actions remain unavailable until state is refreshed.")
			return
		if _pending_trade.get("refresh_port_after") == true:
			_pending_trade.erase("refresh_port_after")
			_request_port_refresh()
		else:
			gameplay_view.set_command_busy(false)

func _finish_pending_launch_departure(snapshot: Dictionary) -> void:
	_pending_launch_departure = false
	var freshness_value: Variant = snapshot.get("freshness", {})
	var freshness: Dictionary = freshness_value if freshness_value is Dictionary else {}
	var all_fresh := str(freshness.get("player", "")) == ClientState.FRESH and str(freshness.get("ship", "")) == ClientState.FRESH and str(freshness.get("sector", "")) == ClientState.FRESH
	if not all_fresh:
		_landing_surface_ready = false
		gameplay_view.set_command_busy(false)
		gameplay_view.show_notification("Launch confirmed, but the authoritative refresh was incomplete; keeping planet actions unavailable until state is refreshed.")
		return
	var ship_value: Variant = snapshot.get("ship", {})
	var ship_snapshot: Dictionary = ship_value if ship_value is Dictionary else {}
	# A fresh ship record must still identify the server-owned planet field
	# before a launch can close the surface.  Missing or malformed metadata is
	# not evidence that departure completed; keep the surface fail-closed.
	if not ship_snapshot.has("onplanet"):
		_landing_surface_ready = false
		gameplay_view.set_command_busy(false)
		gameplay_view.show_notification("Launch state was incomplete; keeping the confirmed planet surface until a fresh state is available.")
		return
	var onplanet: Variant = ship_snapshot.get("onplanet")
	var onplanet_valid_zero: bool = (typeof(onplanet) == TYPE_INT and int(onplanet) == 0) \
		or (typeof(onplanet) == TYPE_FLOAT and is_finite(onplanet) and onplanet == 0.0)
	if not onplanet_valid_zero:
		_landing_surface_ready = false
		gameplay_view.set_command_busy(false)
		gameplay_view.show_notification("Launch state was contradictory; keeping the confirmed planet surface until a fresh state is available.")
		return
	_landed_planet_id = 0
	_landing_surface_ready = false
	gameplay_view.leave_planet_workflow()
	gameplay_view.set_command_busy(false)
	gameplay_view.show_notification("Launch confirmed · returning to sector view.")

func _on_warp_requested(destination: Variant) -> void:
	var confirmed_destination := _authoritative_positive_id(destination)
	if confirmed_destination <= 0:
		gameplay_view.show_notification("Warp unavailable · the destination has no valid server sector ID.")
		return
	if not auth_session.is_authenticated() or _pending_game_commands.size() > 0 or _warp_refresh_pending or _command_refresh_pending or not _pending_trade.is_empty():
		return
	gameplay_view.set_action_pending(true)
	var request_id: String = transport.request("move.warp", {"to_sector_id": confirmed_destination}, auth_session.session_token)
	if request_id.is_empty():
		gameplay_view.set_action_pending(false)
		gameplay_view.show_notification("Warp was not sent. Check the connection and try again.")
		return
	_pending_game_commands[request_id] = {"kind": "warp", "destination": destination}
	gameplay_view.show_notification("Warp request sent · waiting for server confirmation.")

func _on_tavern_enter_requested() -> void:
	if not auth_session.is_authenticated() or not _pending_game_commands.is_empty() or _command_refresh_pending or _warp_refresh_pending or not _pending_trade.is_empty():
		gameplay_view.show_notification("Tavern access cannot be checked while another command is pending.")
		return
	var request_id: String = transport.request("tavern.lottery.status", {}, auth_session.session_token)
	if request_id.is_empty():
		gameplay_view.show_notification("Tavern access could not be checked · connection unavailable.")
		return
	_pending_game_commands[request_id] = {"kind": "tavern_probe", "command": "tavern.lottery.status", "label": "Verify StarDock tavern access", "mutating": false}
	gameplay_view.set_command_busy(true)
	gameplay_view.show_notification("Checking for an active tavern in this StarDock sector…")

func _finish_warp_reply(response: Dictionary, destination: int) -> void:
	var result: Dictionary = Protocol.result_from_response(response)
	if not _movement_response_type_matches(response, "move.result"):
		gameplay_view.show_notification("Warp response was not recognized · refreshing confirmed player, ship and sector state.")
		_refresh_after_warp_attempt()
		return
	var hazards := Protocol.move_hazard_report(response)
	if not hazards.is_empty():
		gameplay_view.show_information("Sector hazards", hazards)
	var terminal := Protocol.move_terminal_outcome(response)
	if terminal.get("ship_destroyed", false) == true or terminal.get("ship_remains_in_sector", true) == false:
		# A success-shaped envelope can still report a terminal entry hazard.
		# Do not present the requested destination or play a warp transition;
		# reconciliation must establish the server-owned post-hazard state.
		gameplay_view.show_notification("Warp outcome is terminal · refreshing confirmed player, ship and sector state.")
		_refresh_after_warp_attempt()
		return
	if result.get("kind") == "ok":
		var confirmed_sector := Protocol.confirmed_move_sector(result.get("data", {}))
		if confirmed_sector <= 0 or confirmed_sector != destination:
			var reason := "omitted the authoritative current sector" if confirmed_sector <= 0 else "reported sector %d instead of requested sector %d" % [confirmed_sector, destination]
			gameplay_view.show_notification("Warp was not confirmed: server %s · refreshing confirmed state." % reason)
		else:
			gameplay_view.show_notification("Warp confirmed · Sector %d." % confirmed_sector)
			await gameplay_view.play_warp_transition()
	else:
		gameplay_view.show_notification("Warp refused · %s" % str(result.get("message", "The server did not move your ship.")))
	_refresh_after_warp_attempt()

func _movement_response_type_matches(response: Dictionary, expected: String) -> bool:
	# Successful movement replies must identify their published response schema.
	# The canonical server uses type "error" for negative envelopes;
	# those may carry authoritative movement hazards in error.data and remain
	# processable. An omitted/empty type or type "refused" is not a second
	# success variant, and an explicit type from another command must not drive
	# the state UI.
	var raw_type: Variant = response.get("type", null)
	if typeof(raw_type) != TYPE_STRING:
		return false
	var actual := str(raw_type)
	if actual == expected:
		# The command-specific movement type is a success contract only. A
		# negative envelope must use the shared error type so malformed
		# responses cannot expose hazard data or authorize relocation.
		return str(response.get("status", "")) == "ok"
	return actual == "error" and str(response.get("status", "")) in ["refused", "error"]

func _tow_response_type_matches(response: Dictionary) -> bool:
	# Tow hazard fields are actionable only on the documented tow response
	# schemas. Refusals use the shared error/refused envelope rather than a
	# tow-specific response variant. An omitted/empty type is not a second
	# success variant and must not be rendered as a tow outcome.
	var raw_type: Variant = response.get("type", null)
	if typeof(raw_type) != TYPE_STRING:
		return false
	var actual := str(raw_type)
	if actual in ["ship.tow.engaged", "ship.tow.disengaged"]:
		return true
	return actual in ["error", "refused"] and str(response.get("status", "")) in ["error", "refused"]

func _refresh_after_warp_attempt() -> void:
	_warp_refresh_pending = true
	if not refresh_coordinator.refresh(true):
		_warp_refresh_pending = false
		gameplay_view.set_action_pending(false)
		gameplay_view.show_notification("Could not refresh your location. The displayed sector may be stale.")

func _on_command_requested(command: String, data: Dictionary, label: String, mutating: bool) -> void:
	if command == "move.warp":
		_on_warp_requested(data.get("to_sector_id", null))
		return
	if command == "move.transwarp":
		# `to_sector_id` is the published movement request identity. Keep the
		# transport boundary strict so malformed UI/test payloads cannot become a
		# second protocol variant or be mistaken for an authoritative destination.
		var transwarp_destination := _authoritative_positive_id(data.get("to_sector_id", null))
		if transwarp_destination <= 0:
			gameplay_view.show_notification("Transwarp unavailable · the destination has no valid server sector ID.")
			return
		# The menu normally enforces this gate, but all command paths converge here.
		# Do not send a long-range movement request from a stale/unknown location;
		# refresh is the only client action that can establish the server-owned
		# origin state needed before another movement attempt.
		if not _sector_snapshot_is_fresh():
			gameplay_view.show_notification("Transwarp unavailable · refresh the confirmed sector before moving.")
			return
		data = {"to_sector_id": transwarp_destination}
	if _route_running and command in ["move.autopilot.start", "move.autopilot.control", "move.autopilot.stop"]:
		gameplay_view.show_notification("Use the route bar to stop safely after the current confirmed warp; change saved-route controls when idle.")
		return
	var is_planet_read := _landed_planet_id > 0 and _landing_surface_ready and command in ["planet.info", "planet.colonists.get"]
	if not auth_session.is_authenticated() or (not is_planet_read and _pending_game_commands.size() > 0) or not _pending_trade.is_empty() or _warp_refresh_pending or (_command_refresh_pending and not is_planet_read):
		gameplay_view.show_notification("A command is already pending, or the connection is unavailable.")
		return
	var request_id: String = transport.request(command, data, auth_session.session_token)
	if command == "port.info":
		print("[PORT_TRACE] outbound command=port.info selected_port_id=%s data=%s" % [str(data.get("port_id", "<none>")), Protocol.diagnostic_json(data)])
	if request_id.is_empty():
		gameplay_view.show_notification("%s was not sent. Check the connection and try again." % label)
		return
	log_network("COMMAND", "command=%s label=%s mutating=%s data=%s" % [command, label, mutating, Protocol.diagnostic_json(data)])
	var kind := "command"
	if is_planet_read:
		kind = "planet_info" if command == "planet.info" else "planet_colonists"
		_planet_pending_count += 1
	var pending_data: Dictionary = {} if command == "auth.change_password" else data.duplicate(true)
	_pending_game_commands[request_id] = {"kind": kind, "command": command, "label": label, "data": pending_data, "mutating": mutating}
	gameplay_view.set_command_busy(true)
	if not is_planet_read:
		gameplay_view.show_notification("%s · waiting for server confirmation." % label)

func _finish_command_reply(response: Dictionary, operation: Dictionary) -> void:
	var result: Dictionary = Protocol.result_from_response(response)
	var label := str(operation.get("label", operation.get("command", "Command")))
	var movement_hazard_report := ""
	if result.get("kind") == "ok":
		var data := _response_data_dictionary(result.get("data", {}))
		var summary := _result_summary(data)
		var command_name := str(operation.get("command", ""))
		if command_name == "port.info":
			print("[PORT_TRACE] reply request_id_present=%s type=%s status=%s data=%s" % [not str(response.get("reply_to", "")).is_empty(), str(response.get("type", "")), str(response.get("status", "")), Protocol.diagnostic_json(data)])
		if command_name == "auth.change_password":
			gameplay_view.set_command_busy(false)
			gameplay_view.show_notification("Password changed. Sign in with the new password next time.")
			return
		if command_name in ["chat.send", "chat.broadcast"]:
			# The send responses carry confirmation, not a message list. Do not
			# fabricate a local copy; reconcile the canonical history after the
			# correlated send response is received.
			gameplay_view.show_notification("%s confirmed · refreshing chat history." % label)
			_request_communication_history_refresh()
			return
		if command_name in ["mail.read", "mail.delete"]:
			# mail.read marks the authoritative row read and mail.delete removes it;
			# reconcile the inbox before allowing another mail action. Do not mutate
			# or fabricate a local inbox from the command acknowledgement.
			if command_name == "mail.read":
				gameplay_view.show_command_result(label, data)
			gameplay_view.show_notification("%s confirmed · refreshing mail inbox." % label)
			_request_mail_inbox_refresh()
			return
		if command_name == "player.computer.recommend_routes":
			gameplay_view.set_command_busy(false)
			gameplay_view.show_trade_routes(data)
			return
		if command_name == "auth.logout":
			_finish_logout()
			return
		if command_name in ["trade.buy", "trade.sell"]:
			# The published V3 trade response contract requires a typed receipt.
			# Preserve a non-string value as invalid rather than coercing it into a
			# parallel response variant.
			var raw_trade_type: Variant = response.get("type", null)
			var trade_response_type: String = raw_trade_type if typeof(raw_trade_type) == TYPE_STRING else ""
			if not _valid_trade_receipt(data, operation.get("data", {}), trade_response_type, command_name.trim_prefix("trade."), auth_session.player_id):
				_pending_trade.clear()
				_pending_trade["refresh_port_after"] = true
				gameplay_view.set_command_busy(false)
				gameplay_view.show_notification("%s settlement was not displayed because the receipt did not identify the requested port and sector; refreshing confirmed balances, cargo and port stock." % label)
				_refresh_after_command(["player", "ship", "sector"])
				return
		if command_name == "ship.transfer_cargo":
			var requested_transfer: Variant = operation.get("data", {})
			var active_ship_id := _authoritative_positive_id(gameplay_view._snapshot.get("hud", {}).get("ship_id", 0))
			if not _valid_cargo_transfer_receipt(data, response, requested_transfer, active_ship_id):
				gameplay_view.set_command_busy(false)
				gameplay_view.show_notification("Cargo transfer was not displayed because the server receipt did not match the requested ships or quantity; refreshing confirmed player, ship and sector state.")
				_refresh_after_command(["player", "ship", "sector"])
				return
		if command_name == "hardware.buy":
			# `hardware.purchase_v1` is the sole purchase receipt. A generic
			# success envelope must not be presented as a hardware mutation because
			# the server-owned credit and ship state may still be unchanged.
			var requested_code := str(operation.get("data", {}).get("code", "")).strip_edges().to_upper()
			var requested_quantity: Variant = operation.get("data", {}).get("quantity", 0)
			var purchase_type_valid: bool = typeof(response.get("type", null)) == TYPE_STRING and response.get("type") == "hardware.purchase_v1"
			var purchase_code := str(data.get("code", "")).strip_edges().to_upper()
			var purchase_valid: bool = purchase_type_valid and not requested_code.is_empty() \
				and typeof(requested_quantity) == TYPE_INT and int(requested_quantity) > 0 \
				and purchase_code == requested_code \
				and typeof(data.get("quantity", null)) == TYPE_INT and int(data.get("quantity", -1)) == int(requested_quantity) \
				and typeof(data.get("credits_spent", null)) == TYPE_INT and int(data.get("credits_spent", -1)) >= 0 \
				and data.get("ship", null) is Dictionary
			if not purchase_valid:
				gameplay_view.set_command_busy(false)
				gameplay_view.show_notification("Hardware purchase was not displayed because the server returned an invalid purchase receipt; refreshing confirmed credits and ship state.")
				_refresh_after_command(["player", "ship"])
				return
		if command_name == "planet.land":
			# The authoritative C handler emits one typed success schema with the
			# identity nested under data. Do not accept root-level or untyped
			# success-shaped envelopes as parallel protocol variants.
			var response_planet_id: Variant = data.get("planet_id", 0)
			var planet_id := _authoritative_positive_id(response_planet_id)
			var requested_planet_id := _authoritative_positive_id(operation.get("data", {}).get("planet_id", 0))
			var response_type_value: Variant = response.get("type", null)
			var landing_type_valid: bool = response_type_value == "planet.land.success"
			if landing_type_valid and planet_id > 0 and planet_id == requested_planet_id:
				# A prior confirmed surface may still be visible when a new landing
				# is requested from stale context.  The landing envelope does not
				# authorize that surface or the new one until player/ship refresh
				# confirms the active ship is on this exact planet.
				gameplay_view.leave_planet_workflow()
				_landed_planet_id = planet_id
				_landing_surface_ready = false
				_pending_landing_surface_id = planet_id
				gameplay_view.show_notification("Landing confirmed · refreshing player and ship state before opening the surface.")
				# Landing changes the authoritative player/ship relationship and
				# the current sector's disclosed planet state.  Reconcile all three
				# domains before exposing the surface; the typed landing receipt
				# alone does not authorize a stale planet snapshot.
				_refresh_after_command(["player", "ship", "sector"])
			else:
				# A success-shaped landing reply with a mismatched type or object ID
				# is ambiguous: the server may have consumed a turn or changed the
				# ship even though this envelope cannot identify the result. Do not
				# leave an older planet surface actionable while reconciliation runs.
				_invalidate_planet_surface_context()
				gameplay_view.set_command_busy(false)
				var reason := "had an unexpected response type" if not landing_type_valid else ("omitted the planet ID" if planet_id <= 0 else "returned a different planet ID")
				gameplay_view.show_notification("Landing was not opened because the response %s. Refreshing confirmed state." % reason)
				_refresh_after_command(["player", "ship", "sector"])
			return
		if command_name == "port.info":
			if not _port_response_type_matches(response):
				gameplay_view.set_command_busy(false)
				gameplay_view.port_workflow.close_for_location_change()
				gameplay_view.show_notification("%s was not displayed because the server returned an unexpected port response type; refreshing current-sector state." % label)
				_refresh_after_command(["sector"])
				return
			var requested_port_id := _authoritative_positive_id(operation.get("data", {}).get("port_id", 0))
			if not _accept_authoritative_port(data, label, requested_port_id):
				gameplay_view.set_command_busy(false)
				_refresh_after_command(["sector"])
				return
			# A direct port.info request is the docking transition.  The response
			# has already been correlated with the selected/current port above; hand
			# that authoritative payload to the existing port workflow instead of
			# treating it as a generic command result.
			gameplay_view.enter_port_workflow(data)
		if command_name in ["move.scan", "move.describe_sector", "sector.info", "sector.scan"]:
			if not _scan_response_type_matches(command_name, response, operation.get("data", {})):
				gameplay_view.set_command_busy(false)
				gameplay_view.show_notification("%s was not displayed because the response type did not match the requested scan; refreshing current-sector state." % label)
				_refresh_after_command(["sector"])
				return
			var reported_sector_id := _response_sector_id(data)
			# The cached HUD identity is server-owned metadata too. Do not coerce a
			# malformed boolean/float into an integer and then accept a scan merely
			# because the response happens to contain the same numeric value.
			var confirmed_sector_id := _authoritative_positive_id(gameplay_view._snapshot.get("hud", {}).get("sector_id", 0))
			if confirmed_sector_id <= 0 or reported_sector_id <= 0 or reported_sector_id != confirmed_sector_id:
				# A successful-looking scan for another sector must not replace or
				# disclose state alongside the player's confirmed current location.
				# A missing ID is equally unsafe: the response cannot be tied to
				# the authoritative current-sector snapshot.
				# Reconcile only the authoritative current-sector domain; the server
				# remains the source of truth for any permitted scan result.
				gameplay_view.set_command_busy(false)
				var scan_reason := "omitted a valid sector ID" if reported_sector_id <= 0 else "returned sector %d, but the confirmed location is sector %d" % [reported_sector_id, confirmed_sector_id]
				gameplay_view.show_notification("%s %s; refreshing current-sector state." % [label, scan_reason])
				_refresh_after_command(["sector"])
				return
		if command_name == "sector.scan.density":
			if not _scan_response_type_matches(command_name, response):
				gameplay_view.set_command_busy(false)
				gameplay_view.show_notification("%s was not displayed because the response type did not match the requested density scan; refreshing current-sector state." % label)
				_refresh_after_command(["sector"])
				return
			# Density is a read-only report and may contain several sector rows;
			# preserve its compact array form instead of applying the single-
			# current-sector identity rule used by location-bearing scans.
			var density_data: Variant = result.get("data", {})
			if density_data is Array:
				density_data = {"sectors": density_data}
			if not (density_data is Dictionary) or not _density_has_authoritative_ids(density_data):
				gameplay_view.set_command_busy(false)
				gameplay_view.show_notification("%s returned no usable authoritative density rows; refreshing confirmed sector state." % label)
				_refresh_after_command(["sector"])
				return
			gameplay_view.set_command_busy(false)
			gameplay_view.show_command_result(label, density_data)
			return
		if command_name == "ship.list":
			# `ship.list.response` is the sole authoritative roster contract.
			# Missing/null/empty types are not compatible response variants: a
			# success envelope from another command must not become an ownership
			# or lifecycle choice in the client.
			var roster_type_valid: bool = typeof(response.get("type", null)) == TYPE_STRING and response.get("type") == "ship.list.response"
			if not roster_type_valid or not _ship_list_has_authoritative_ids(data):
				gameplay_view.command_menu.set_owned_ship_roster({})
				gameplay_view.set_command_busy(false)
				gameplay_view.show_notification("%s was not displayed because the server returned an incomplete ship roster; refreshing confirmed ship state." % label)
				_refresh_after_command(["player", "ship"])
				return
			gameplay_view.command_menu.set_owned_ship_roster(data)
		if command_name == "ship.inspect":
			# The server publishes one response schema for selected-ship
			# inspection.  A successful envelope with a missing or alternate type
			# is not a compatible response variant and must not render details.
			if not _ship_inspect_response_type_matches(response):
				gameplay_view.set_command_busy(false)
				gameplay_view.show_notification("%s was not displayed because the server returned an unexpected response type; refreshing confirmed sector state." % label)
				_refresh_after_command(["sector"])
				return
		if command_name == "ship.transfer":
			var transfer_type_valid: bool = typeof(response.get("type", null)) == TYPE_STRING and response.get("type") == "ship.transfer.response"
			# A typed success envelope is not usable unless its payload is an
			# object.  Keep malformed server data out of the ownership path and
			# reconcile the authoritative roster below instead of dereferencing a
			# scalar/array payload as if it were a transfer receipt.
			var transfer_data: Dictionary = data if data is Dictionary else {}
			var requested_transfer_ship := _authoritative_positive_id(operation.get("data", {}).get("ship_id", 0))
			var requested_transfer_player := _authoritative_positive_id(operation.get("data", {}).get("target_player_id", 0))
			var authenticated_transfer_player := _authoritative_positive_id(auth_session.player_id)
			var returned_transfer_ship := _authoritative_positive_id(transfer_data.get("ship_id", 0))
			var returned_transfer_from := _authoritative_positive_id(transfer_data.get("from_player_id", 0))
			var returned_transfer_player := _authoritative_positive_id(transfer_data.get("target_player_id", 0))
			if not transfer_type_valid or requested_transfer_ship <= 0 or requested_transfer_player <= 0 \
				or authenticated_transfer_player <= 0 or returned_transfer_ship != requested_transfer_ship \
				or returned_transfer_from != authenticated_transfer_player \
				or returned_transfer_player != requested_transfer_player:
				gameplay_view.set_command_busy(false)
				gameplay_view.show_notification("Ship transfer was not confirmed by the authoritative response; refreshing ownership state.")
				_ship_roster_refresh_after_command = true
				_refresh_after_command(["player", "ship", "sector"])
				return
			# The typed receipt confirms the server-side mutation, but the client
			# must not present the refreshed ownership view until the affected
			# domains and the canonical ship.list roster have been reconciled.
			gameplay_view.show_notification("Ship transfer receipt accepted · refreshing authoritative ownership state…")
			_ship_roster_refresh_after_command = true
			_refresh_after_command(["player", "ship", "sector"])
			return
		if command_name == "ship.rename":
			# The published rename success contract is `ship.renamed`; an
			# untyped success must not be rendered as confirmation of a local
			# name change. Reconcile the active ship on every ambiguity.
			var rename_type_valid: bool = typeof(response.get("type", null)) == TYPE_STRING and response.get("type") == "ship.renamed"
			var requested_rename_ship := _authoritative_positive_id(gameplay_view._snapshot.get("hud", {}).get("ship_id", 0))
			var returned_rename_ship := _authoritative_positive_id(data.get("ship_id", 0))
			var returned_rename_name: Variant = data.get("name", null)
			if not rename_type_valid or requested_rename_ship <= 0 \
				or returned_rename_ship != requested_rename_ship \
				or typeof(returned_rename_name) != TYPE_STRING \
				or str(returned_rename_name).strip_edges().is_empty():
				gameplay_view.set_command_busy(false)
				gameplay_view.show_notification("Ship rename was not confirmed by the authoritative response; refreshing ship state.")
				_refresh_after_command(["player", "ship"])
				return
			gameplay_view.show_notification("Ship rename confirmed · %s." % str(returned_rename_name).strip_edges())
			_refresh_after_command(["player", "ship"])
			return
		if command_name == "ship.repair":
			# Repair has one authoritative response schema. Do not present a
			# success-shaped payload unless the receipt has typed cost/hull fields;
			# credits and hull are then reconciled from fresh server reads.
			var repair_type_valid: bool = typeof(response.get("type", null)) == TYPE_STRING and response.get("type") == "ship.repair"
			var repair_valid: bool = repair_type_valid and data.get("repaired", null) is bool and data.get("cost", null) is int and int(data.get("cost", -1)) >= 0 and data.get("hull", null) is int and int(data.get("hull", -1)) >= 0 and int(data.get("hull", -1)) <= 100
			if not repair_valid:
				gameplay_view.set_command_busy(false)
				gameplay_view.show_notification("Ship repair was not displayed because the server returned an invalid repair receipt; refreshing confirmed state.")
				_refresh_after_command(["player", "ship"])
				return
			gameplay_view.show_command_result(label, data)
			gameplay_view.show_notification("Ship repair confirmed · refreshing credits and hull.")
			_refresh_after_command(["player", "ship"])
			return
		if command_name == "shipyard.upgrade":
			# `shipyard.upgraded_v1` is the sole exchange receipt. A generic
			# success envelope is not a compatible response variant: the client
			# must not claim a hull exchange without matching the requested type,
			# name and authoritative ship identity.
			var upgrade_data: Variant = data
			var requested_upgrade_type := _authoritative_positive_id(operation.get("data", {}).get("new_type_id", 0))
			var requested_upgrade_name := str(operation.get("data", {}).get("new_ship_name", ""))
			var upgrade_type_valid: bool = typeof(response.get("type", null)) == TYPE_STRING and response.get("type") == "shipyard.upgraded_v1"
			var upgrade_valid: bool = upgrade_type_valid and upgrade_data is Dictionary and _authoritative_positive_id(upgrade_data.get("ship_id", 0)) > 0 and _authoritative_positive_id(upgrade_data.get("new_type_id", 0)) == requested_upgrade_type and str(upgrade_data.get("new_ship_name", "")) == requested_upgrade_name and upgrade_data.get("credits_spent", null) is int and int(upgrade_data.get("credits_spent", -1)) >= 0
			if not upgrade_valid:
				gameplay_view.set_command_busy(false)
				gameplay_view.show_notification("%s was not displayed because the server returned an invalid shipyard receipt; refreshing confirmed state." % label)
				_refresh_after_command(["player", "ship", "sector"])
				return
			gameplay_view.show_command_result(label, upgrade_data)
			gameplay_view.show_notification("Ship hull exchange confirmed · refreshing credits, ship and sector.")
			_refresh_after_command(["player", "ship", "sector"])
			return
			var reported_ship_id := _response_ship_id(data)
			var requested_ship_id := _authoritative_positive_id(operation.get("data", {}).get("ship_id", 0))
			if requested_ship_id <= 0 or reported_ship_id <= 0 or reported_ship_id != requested_ship_id:
				gameplay_view.set_command_busy(false)
				var ship_reason := "omitted a valid ship ID" if reported_ship_id <= 0 else "returned ship %d instead of selected ship %d" % [reported_ship_id, requested_ship_id]
				gameplay_view.show_notification("%s was not displayed because the response %s; refreshing confirmed sector state." % [label, ship_reason])
				_refresh_after_command(["sector"])
				return
		if command_name == "combat.attack_planet":
			var requested_planet_id := _authoritative_positive_id(operation.get("data", {}).get("planet_id", 0))
			var attack_response_type: Variant = response.get("type", null)
			# A capture/loss result is displayable only from the canonical success
			# envelope; an omitted or alternate type is not a response variant.
			var attack_response_type_valid := typeof(attack_response_type) == TYPE_STRING \
				and str(attack_response_type) == "combat.attack_planet"
			var reported_planet_id := _authoritative_positive_id(data.get("planet_id", 0))
			var attack_result_valid := data.get("captured", null) is bool \
				and data.get("attacker_remaining_fighters", null) is int \
				and int(data.get("attacker_remaining_fighters", -1)) >= 0 \
				and data.get("defender_remaining_fighters", null) is int \
				and int(data.get("defender_remaining_fighters", -1)) >= 0
			if not attack_response_type_valid or requested_planet_id <= 0 or reported_planet_id != requested_planet_id or not attack_result_valid:
				gameplay_view.set_command_busy(false)
				var attack_reason := "an unexpected response type" if not attack_response_type_valid else ("no authoritative loss/capture result" if not attack_result_valid else "no matching planet ID")
				gameplay_view.show_notification("Planet attack result was not displayed because the server returned %s; refreshing confirmed state." % attack_reason)
				_refresh_after_command(["player", "ship", "sector"])
				return
			gameplay_view.show_notification(PlanetCombatPresenter.attack_result(data))
			gameplay_view.show_command_result("Planet attack", data)
			_refresh_after_command(["player", "ship", "sector"])
			return
		if command_name == "ferengi.traders":
			var ferengi_offers: Variant = data.get("offers", [])
			gameplay_view.command_menu.set_ferengi_offer_available(ferengi_offers is Array and not ferengi_offers.is_empty())
			gameplay_view.show_information("FERENGI TRADERS", FerengiPresenter.traders(data))
			gameplay_view.set_command_busy(false)
			return
		if command_name in ["ferengi.deal.accept", "ferengi.deal.reject"]:
			gameplay_view.command_menu.set_ferengi_offer_available(false)
			var deal_message := FerengiPresenter.deal_result(data)
			gameplay_view.show_notification(deal_message)
			gameplay_view.show_information("FERENGI DEAL", deal_message)
			_refresh_after_command(["player", "ship"])
			return
		if command_name == "move.autopilot.start":
			var path: Array = _route_from_response(data)
			if path.is_empty():
				gameplay_view.set_command_busy(false)
				gameplay_view.show_notification("Route planner returned no usable path. No movement was made.")
			else:
				_planned_route.clear()
				for sector_id in path:
					if typeof(sector_id) == TYPE_INT or typeof(sector_id) == TYPE_FLOAT:
						_planned_route.append(int(sector_id))
				var current_sector := int(gameplay_view._snapshot.get("hud", {}).get("sector_id", 0))
				if not _planned_route.is_empty() and _planned_route[0] == current_sector:
					_planned_route.pop_front()
				if _planned_route.is_empty():
					gameplay_view.set_command_busy(false)
					gameplay_view.show_notification("Route planner confirms you are already at the destination.")
					return
				_route_destination = int(_planned_route.back())
				_autopilot_mode = "continue"
				_route_stop_after_next = false
				gameplay_view.set_command_busy(false)
				gameplay_view.show_planned_route(_planned_route, _route_destination)
				gameplay_view.show_notification("Route plotted · review it, then engage Autonav to move.")
			return
		if command_name == "move.autopilot.control":
			_autopilot_mode = str(data.get("action", "continue"))
			if _autopilot_mode == "stop_at_next":
				_route_stop_after_next = true
				if _route_running:
					_route_stop_requested = true
					_planned_route.clear()
			elif _autopilot_mode == "continue":
				_route_stop_after_next = false
				if _route_running:
					_route_stop_requested = false
			gameplay_view.set_command_busy(false)
			gameplay_view.show_notification("Autopilot %s confirmed · next sector %s." % [_autopilot_mode.replace("_", " "), str(data.get("next_sector_id", "unavailable"))])
			return
		if command_name == "move.autopilot.stop":
			_autopilot_mode = "manual"
			_route_stop_after_next = false
			if _route_running:
				_route_stop_requested = true
				_planned_route.clear()
				gameplay_view.set_autonav_status("AUTONAV · stopping after the current confirmed warp.", true)
			else:
				_planned_route.clear()
				_route_destination = 0
				gameplay_view.clear_planned_route()
			gameplay_view.set_command_busy(false)
			gameplay_view.show_notification("Saved autopilot route stopped · no movement was made by this command.")
			return
		if command_name == "planet.colonists.set":
			gameplay_view.show_notification("Colonist transfer confirmed.%s" % (" · " + summary if not summary.is_empty() else ""))
			_request_planet_surface_records()
			_refresh_after_command(["player", "ship"])
			return
		if command_name == "planet.launch":
			if not _launch_response_type_matches(response):
				# A mismatched envelope cannot establish departure or authorize any
				# hazard details. Reconcile all affected domains before exposing a
				# new movement state.
				gameplay_view.set_command_busy(false)
				gameplay_view.show_notification("Launch response was not recognized; refreshing confirmed player, ship and sector state.")
				_refresh_after_command(["player", "ship", "sector"])
				return
			var launch_hazards := Protocol.move_hazard_report(response)
			if not launch_hazards.is_empty():
				gameplay_view.show_information("Sector hazards", launch_hazards)
			var launch_terminal := Protocol.move_terminal_outcome(response)
			if launch_terminal.get("ship_destroyed", false) == true or launch_terminal.get("ship_remains_in_sector", true) == false:
				# A terminal entry outcome cannot leave the previously confirmed
				# planet surface actionable while reconciliation is pending. There is
				# no departure transition to finalize in this branch, so invalidate
				# the local surface context immediately and keep the refresh as the
				# source of the replacement player/ship/sector state.
				_pending_launch_departure = false
				_landed_planet_id = 0
				_landing_surface_ready = false
				gameplay_view.leave_planet_workflow()
				gameplay_view.show_notification("Launch outcome is terminal · refreshing confirmed player, ship and sector state.")
				_refresh_after_command(["player", "ship", "sector"])
				return
			var launch_data: Variant = response.get("data", {})
			# The C server's authoritative success payload identifies the
			# destination sector. Do not invent an `in_space` response variant.
			var launch_sector_id := 0
			if launch_data is Dictionary:
				launch_sector_id = _authoritative_positive_id(launch_data.get("sector_id", 0))
			var launch_confirmed: bool = launch_sector_id > 0
			if not launch_confirmed:
				# A successful envelope without the server's destination sector
				# cannot establish that the ship left the planet. This can happen
				# when entry-hazard processing produces a partial response;
				# keep the surface context until authoritative state is refreshed.
				gameplay_view.show_notification("Launch outcome is uncertain · refreshing confirmed player, ship and sector state.")
				_refresh_after_command(["player", "ship", "sector"])
				return
			await gameplay_view.play_warp_transition()
			_pending_launch_departure = true
			_planet_pending_count = 0
			gameplay_view.show_notification("Launch confirmed · refreshing player, ship and sector state before leaving the surface.")
			_refresh_after_command()
			return
		if command_name == "move.transwarp":
			if not _movement_response_type_matches(response, "move.transwarp"):
				gameplay_view.set_command_busy(false)
				gameplay_view.show_notification("Transwarp response was not recognized · refreshing confirmed player, ship and sector state.")
				_refresh_after_command(["player", "ship", "sector"])
				return
			var transwarp_hazards := Protocol.move_hazard_report(response)
			if not transwarp_hazards.is_empty():
				gameplay_view.show_information("Sector hazards", transwarp_hazards)
			var transwarp_terminal := Protocol.move_terminal_outcome(response)
			if transwarp_terminal.get("ship_destroyed", false) == true or transwarp_terminal.get("ship_remains_in_sector", true) == false:
				# A terminal entry outcome is authoritative even when the response
				# omits the normal success destination (for example, a legacy
				# current_sector field). Surface the outcome and reconcile first;
				# never require a destination to establish that the ship is gone.
				gameplay_view.show_notification("Transwarp outcome is terminal · refreshing confirmed player, ship and sector state.")
				_refresh_after_command(["player", "ship", "sector"])
				return
			var requested_transwarp_data: Variant = operation.get("data", {})
			var requested_transwarp_sector := _authoritative_positive_id(
				requested_transwarp_data.get("to_sector_id", 0)
			) if requested_transwarp_data is Dictionary else 0
			var confirmed_transwarp_sector := Protocol.confirmed_transwarp_sector(response.get("data", {}))
			var transwarp_data: Variant = response.get("data", {})
			var transwarp_status := str(transwarp_data.get("status", "")) if transwarp_data is Dictionary else ""
			if (requested_transwarp_sector <= 0 or confirmed_transwarp_sector <= 0
					or confirmed_transwarp_sector != requested_transwarp_sector
					or transwarp_status != "transwarp_complete"):
				# A success envelope without a matching authoritative destination is
				# status/destination is not proof of relocation. Do not claim
				# completion or display the requested sector as current; reconcile all
				# movement-owned state.
				gameplay_view.set_command_busy(false)
				gameplay_view.show_notification("Transwarp was not confirmed · refreshing confirmed player, ship and sector state.")
				_refresh_after_command(["player", "ship", "sector"])
				return
			# The canonical destination has been validated above. Surface that
			# authoritative ID instead of falling through to the generic
			# completion message, which hides whether relocation was confirmed.
			# Keep confirmed transwarp visually consistent with ordinary warp; the
			# transition is presentation only and never establishes movement.
			await gameplay_view.play_warp_transition()
			gameplay_view.show_notification("Transwarp confirmed · Sector %d." % confirmed_transwarp_sector)
			_refresh_after_command(["player", "ship", "sector"])
			return
		if command_name == "ship.tow":
			if not _tow_response_type_matches(response):
				gameplay_view.set_command_busy(false)
				gameplay_view.show_notification("Tow response was not recognized · refreshing confirmed player, ship and sector state.")
				_refresh_after_command(["player", "ship", "sector"])
				return
			var tow_hazards := Protocol.move_hazard_report(response)
			movement_hazard_report = tow_hazards
			var tow_terminal := Protocol.move_terminal_outcome(response)
			if tow_terminal.get("ship_destroyed", false) == true or tow_terminal.get("ship_remains_in_sector", true) == false:
				# A terminal tow-entry outcome must not leave the previous tow
				# relationship actionable; refresh all server-owned aftermath first.
				gameplay_view.show_notification("Tow outcome is terminal · refreshing confirmed player, ship and sector state.")
				_refresh_after_command(["player", "ship", "sector"])
				return
		gameplay_view.show_notification("%s complete.%s" % [label, " · " + summary if not summary.is_empty() else ""])
		if str(operation.get("command", "")) == "shipyard.list":
			gameplay_view.show_shipyard(data)
		elif str(operation.get("command", "")) == "hardware.list":
			gameplay_view.show_hardware_catalogue(data)
		elif str(operation.get("command", "")) in ["deploy.fighters.list", "deploy.mines.list"]:
			var asset_kind := "fighters" if str(operation.get("command", "")) == "deploy.fighters.list" else "mines"
			# List requests use the canonical empty schema. Keep the confirmed
			# current sector as local display/recall context only; never echo it
			# into the server request contract.
			var confirmed_sector := _authoritative_positive_id(gameplay_view._snapshot.get("hud", {}).get("sector_id", 0))
			gameplay_view.show_deployed_assets(data, asset_kind, confirmed_sector)
		else:
			gameplay_view.show_command_result(label, data)
		if not movement_hazard_report.is_empty():
			gameplay_view.show_information("Sector hazards", movement_hazard_report)
		if bool(operation.get("mutating", false)):
			if str(operation.get("command", "")) in ["trade.buy", "trade.sell"]:
				_pending_trade["refresh_port_after"] = true
			if _landed_planet_id > 0:
				_request_planet_surface_records()
			_refresh_after_command()
		else:
			gameplay_view.set_command_busy(false)
	else:
		gameplay_view.set_command_busy(false)
		if str(operation.get("command", "")) == "ship.tow":
			if not _tow_response_type_matches(response):
				gameplay_view.show_notification("Tow response was not recognized · refreshing confirmed player, ship and sector state.")
				_refresh_after_command(["player", "ship", "sector"])
				return
			var refused_tow_hazards := Protocol.move_hazard_report(response)
			if not refused_tow_hazards.is_empty():
				gameplay_view.show_information("Sector hazards", refused_tow_hazards)
			gameplay_view.show_notification("Tow refused · %s · refreshing confirmed player, ship and sector state." % str(result.get("message", "The server did not accept the tow.")))
			_refresh_after_command(["player", "ship", "sector"])
			return
		if str(operation.get("command", "")) == "move.transwarp" and result.get("kind") in ["error", "refused"]:
			# A refusal/error still has to identify the canonical movement envelope.
			# Do not render hazard data from an unrelated command response: that
			# would create a client-side transwarp variant and could expose stale or
			# untrusted entry state. Reconcile the affected domains instead.
			if not _movement_response_type_matches(response, "move.transwarp"):
				gameplay_view.show_notification("Transwarp response was not recognized · refreshing confirmed player, ship and sector state.")
				_refresh_after_command(["player", "ship", "sector"])
				return
			var transwarp_hazards := Protocol.move_hazard_report(response)
			if not transwarp_hazards.is_empty():
				gameplay_view.show_information("Sector hazards", transwarp_hazards)
			var transwarp_terminal := Protocol.move_terminal_outcome(response)
			if transwarp_terminal.get("ship_destroyed", false) == true or transwarp_terminal.get("ship_remains_in_sector", true) == false:
				# An error envelope can still carry an authoritative terminal entry
				# outcome in error.data. Do not reduce destruction to a generic
				# refusal; reconcile the server-owned aftermath before another act.
				gameplay_view.show_notification("Transwarp outcome is terminal · refreshing confirmed player, ship and sector state.")
				_refresh_after_command(["player", "ship", "sector"])
				return
			var outcome := "refused" if result.get("kind") == "refused" else "returned an error"
			var reason := str(result.get("message", "The server did not accept the transwarp."))
			gameplay_view.show_notification("Transwarp %s · %s · refreshing confirmed player, ship and sector state." % [outcome, reason])
			_refresh_after_command(["player", "ship", "sector"])
			return
		if str(operation.get("command", "")) == "planet.land":
			var landing_report := PlanetCombatPresenter.landing_refusal(response)
			if bool(landing_report.get("has_combat", false)):
				gameplay_view.show_notification(str(landing_report.get("message", "Landing refused after planetary combat.")))
			else:
				gameplay_view.show_notification("%s refused · %s" % [label, str(result.get("message", "The server did not permit landing."))])
			var terminal_data: Variant = landing_report.get("data", {})
			if _planet_landing_terminal(terminal_data):
				# The server explicitly reported destruction or non-arrival. Clear
				# any previously visible surface before reconciliation.
				_invalidate_planet_surface_context()
				gameplay_view.show_notification("Landing outcome was terminal · planet actions are unavailable until state is refreshed.")
			# A refusal cannot complete an earlier success-shaped transition. Clear
			# only that pending target; preserve a different, already confirmed
			# surface until the authoritative refresh completes.
			var pending_landing_id := _pending_landing_surface_id
			_pending_landing_surface_id = 0
			if pending_landing_id > 0 and _landed_planet_id == pending_landing_id:
				_landed_planet_id = 0
				_landing_surface_ready = false
				gameplay_view.leave_planet_workflow()
			# A refusal is not a landing, but the local snapshot may be stale.
			_refresh_after_command(["player", "ship", "sector"])
			return
		if str(operation.get("command", "")) == "planet.launch":
			var launch_hazards := Protocol.move_hazard_report(response)
			if not launch_hazards.is_empty():
				gameplay_view.show_information("Sector hazards", launch_hazards)
			var launch_terminal := Protocol.move_terminal_outcome(response)
			if launch_terminal.get("ship_destroyed", false) == true or launch_terminal.get("ship_remains_in_sector", true) == false:
				# A refusal with an explicit terminal entry outcome invalidates the
				# previously confirmed planet surface.  Preserve non-terminal refusals
				# below, but never leave destroyed/non-arriving ship actions visible.
				_invalidate_planet_surface_context()
				gameplay_view.show_notification("Launch outcome is terminal · planet actions are unavailable until state is refreshed.")
			else:
				gameplay_view.show_notification("Launch refused · %s · refreshing confirmed player, ship and sector state." % str(result.get("message", "The server did not accept the launch.")))
			# A launch refusal can follow entry-hazard processing. Keep the
			# confirmed state only for non-terminal refusals, but reconcile every
			# server-owned domain before presenting another action.
			_refresh_after_command(["player", "ship", "sector"])
			return
		if str(operation.get("command", "")) in ["move.scan", "move.describe_sector", "sector.info", "sector.scan", "sector.scan.density"]:
			# A refused visibility request may reflect a stale location/session,
			# but its diagnostic payload is not authoritative sector state. Reconcile
			# only the confirmed sector domain before allowing another scan; never
			# merge or render the refused payload as a successful disclosure.
			gameplay_view.show_notification("%s refused · refreshing confirmed sector state." % label)
			_refresh_after_command(["sector"])
			return
		if str(operation.get("command", "")) == "ship.list":
			# A roster refusal may be caused by stale session/ownership state. Do
			# not leave the previous active-ship snapshot as the apparent answer;
			# reconcile the authoritative player and ship domains before another
			# lifecycle action is offered.
			gameplay_view.show_notification("%s refused · %s · refreshing confirmed ship state." % [label, str(result.get("message", "The server did not return a ship roster."))])
			_refresh_after_command(["player", "ship"])
			return
		if str(operation.get("command", "")) == "ship.transfer":
			# A refused ownership mutation must not leave a stale roster or active
			# ship snapshot actionable. The refusal is not a client-side transfer
			# variant; reconcile the same server-owned domains as a confirmed
			# transfer before offering another lifecycle action.
			gameplay_view.show_notification("%s refused · %s · refreshing authoritative ownership state." % [label, str(result.get("message", "The server did not accept the ship transfer."))])
			_ship_roster_refresh_after_command = true
			_refresh_after_command(["player", "ship", "sector"])
			return
		if str(operation.get("command", "")) in ["ship.repair", "shipyard.upgrade", "hardware.buy"]:
			# The refusal itself is authoritative, but the pre-request credits or
			# hull snapshot may be stale. Reconcile before another shipyard action.
			gameplay_view.show_notification("%s refused · %s" % [label, str(result.get("message", "The server did not accept the command."))])
			_refresh_after_command(["player", "ship"])
			return
		if _is_planet_surface_mutation(str(operation.get("command", ""))):
			gameplay_view.show_notification("%s refused · %s · refreshing the confirmed planet and player/ship state." % [label, str(result.get("message", "The server did not accept the planet action."))])
			_reconcile_planet_surface_mutation()
			return
		if str(operation.get("command", "")) in ["move.scan", "move.describe_sector", "sector.info", "sector.scan", "sector.scan.density"]:
			gameplay_view.show_notification("%s refused · %s · refreshing confirmed sector state." % [label, str(result.get("message", "The server did not return scan information."))])
			_refresh_after_command(["sector"])
			return
		if str(operation.get("command", "")) == "combat.attack_planet":
			# Planetary combat is resolved by the server even when the attack is
			# refused. Preserve and present its authoritative defense/depletion
			# report, then reconcile every affected client domain before another
			# action is offered.
			var attack_report := PlanetCombatPresenter.attack_refusal(response)
			if bool(attack_report.get("has_combat", false)):
				gameplay_view.show_notification(str(attack_report.get("message", "Planet attack refused after planetary combat.")))
			else:
				gameplay_view.show_notification("Planet attack refused · %s" % str(result.get("message", "The server did not accept the attack.")))
			_refresh_after_command(["player", "ship", "sector"])
			return
		if str(operation.get("command", "")) == "port.info":
			gameplay_view.port_workflow.close_for_location_change()
			gameplay_view.show_notification("Port information refused · %s" % str(result.get("message", "Details are not available from this location.")))
			_refresh_after_command(["sector"])
			return
		if str(operation.get("command", "")) == "auth.change_password":
			var error_value = response.get("error", {})
			if error_value is Dictionary and int(error_value.get("code", 0)) == 1101:
				gameplay_view.show_notification("Password change refused · current password was not accepted.")
			else:
				gameplay_view.show_notification("Password change refused · no change was confirmed.")
			return
		if str(operation.get("command", "")) in ["trade.buy", "trade.sell"]:
			_pending_trade.clear()
			_pending_trade["refresh_port_after"] = true
			_refresh_after_command(["player", "ship", "sector"])
			gameplay_view.show_notification("%s refused · %s · refreshing confirmed balances, cargo and port stock." % [label, str(result.get("message", "The server did not accept the command."))])
			return
		gameplay_view.show_notification("%s refused · %s" % [label, str(result.get("message", "The server did not accept the command."))])

func _is_planet_surface_mutation(command: String) -> bool:
	return command in [
		"planet.deposit", "planet.withdraw", "planet.harvest", "planet.fighters.transfer",
		"planet.colonists.allocate", "planet.colonists.set",
		"citadel.build", "citadel.upgrade", "planet.rename"
	]

func _reconcile_planet_surface_mutation() -> void:
	gameplay_view.set_command_busy(false)
	if _landed_planet_id <= 0:
		return
	# A refused or lost mutation is not proof that no server-side state changed.
	# Re-read the selected planet and affected HUD domains; never resend the
	# mutation and never retain stale controls as authoritative.
	_request_planet_surface_records()
	_refresh_after_command(["player", "ship"])

func _invalidate_planet_surface_context() -> void:
	# A refused/lost planet read cannot support stale surface actions. Keep the
	# last confirmed sector visible while requiring a fresh landing before any
	# planet mutation is exposed again.
	_landed_planet_id = 0
	_pending_landing_surface_id = 0
	_landing_surface_ready = false
	gameplay_view.leave_planet_workflow()

func _finish_logout() -> void:
	_pending_game_commands.clear()
	_command_refresh_pending = false
	_warp_refresh_pending = false
	_pending_launch_departure = false
	_pending_trade.clear()
	_landed_planet_id = 0
	_planet_pending_count = 0
	gameplay_view.set_command_busy(false)
	client_state.clear_session_state()
	gameplay_view.command_menu.set_tavern_access(false)
	gameplay_view.port_workflow.visible = false
	gameplay_view.leave_planet_workflow()
	gameplay_view.set_connection_notice(false)
	gameplay_view.visible = false
	auth_session.invalidate("Logged out by the player.")
	_authenticated_once = false
	transport.close("Logged out.")
	login_view.set_overlay_mode(false)
	login_view.visible = true
	login_view.set_connection_values(server_ip, server_port, auth_username)
	login_view.set_status("Logged out. You can connect again whenever you are ready.")

func _refresh_after_command(domains: Array = []) -> void:
	_command_refresh_pending = true
	if not refresh_coordinator.refresh(true, domains):
		_command_refresh_pending = false
		if _pending_landing_surface_id > 0:
			# A confirmed landing is not actionable until its player/ship refresh
			# completes.  If refresh cannot even start, do not leave a pending
			# transition that a later unrelated refresh could consume.
			_pending_landing_surface_id = 0
			_landed_planet_id = 0
			_landing_surface_ready = false
			gameplay_view.leave_planet_workflow()
			gameplay_view.show_notification("Landing was confirmed, but state refresh could not start; reconnect before using planet actions.")
		gameplay_view.set_command_busy(false)
		if _pending_trade.get("refresh_port_after") == true:
			_pending_trade.erase("refresh_port_after")
			_request_port_refresh()
		if _ship_roster_refresh_after_command:
			_ship_roster_refresh_after_command = false
			gameplay_view.command_menu.set_owned_ship_roster({})
			_request_ship_roster_refresh()

func _request_ship_roster_refresh() -> void:
	var request_id: String = transport.request("ship.list", {}, auth_session.session_token)
	if request_id.is_empty():
		gameplay_view.command_menu.set_owned_ship_roster({})
		gameplay_view.set_command_busy(false)
		gameplay_view.show_notification("Ownership changed, but the owned-ship roster could not be refreshed.")
		return
	_pending_game_commands[request_id] = {
		"kind": "ship_roster_refresh",
		"command": "ship.list",
		"label": "Owned ship roster",
		"mutating": false,
	}

func _request_communication_history_refresh() -> void:
	var request_id: String = transport.request("chat.history", {"limit": 20}, auth_session.session_token)
	if request_id.is_empty():
		gameplay_view.set_command_busy(false)
		gameplay_view.show_notification("Message delivery was confirmed, but chat history could not be refreshed. Reopen Chat history to reconcile it.")
		return
	_pending_game_commands[request_id] = {
		"kind": "communication_history",
		"command": "chat.history",
		"label": "Chat history",
		"mutating": false,
	}

func _finish_communication_history_refresh(response: Dictionary) -> void:
	var result: Dictionary = Protocol.result_from_response(response)
	if result.get("kind") != "ok":
		gameplay_view.set_command_busy(false)
		gameplay_view.show_notification("Message delivery was confirmed, but chat history refresh was refused · %s" % str(result.get("message", "server refusal")))
		return
	var response_type: Variant = response.get("type", null)
	if response_type != null and (typeof(response_type) != TYPE_STRING or (not str(response_type).is_empty() and str(response_type) != "chat.history_v1")):
		gameplay_view.set_command_busy(false)
		gameplay_view.show_notification("Message delivery was confirmed, but chat history refresh returned an unexpected response; reopen Chat history to reconcile it.")
		return
	var data := _response_data_dictionary(result.get("data", {}))
	var messages: Variant = data.get("messages", null)
	if not messages is Array:
		gameplay_view.set_command_busy(false)
		gameplay_view.show_notification("Message delivery was confirmed, but the server returned no readable chat history.")
		return
	gameplay_view.set_command_busy(false)
	gameplay_view.show_command_result("Chat history", data)

func _request_mail_inbox_refresh() -> void:
	var request_id: String = transport.request("mail.inbox", {}, auth_session.session_token)
	if request_id.is_empty():
		gameplay_view.set_command_busy(false)
		gameplay_view.show_notification("Mail change was confirmed, but the inbox could not be refreshed. Reopen Mail inbox to reconcile it.")
		return
	_pending_game_commands[request_id] = {
		"kind": "mail_inbox_refresh",
		"command": "mail.inbox",
		"label": "Mail inbox",
		"mutating": false,
	}

func _finish_mail_inbox_refresh(response: Dictionary) -> void:
	var result: Dictionary = Protocol.result_from_response(response)
	if result.get("kind") != "ok":
		gameplay_view.set_command_busy(false)
		gameplay_view.show_notification("Mail change was confirmed, but the inbox refresh was refused · %s" % str(result.get("message", "server refusal")))
		return
	var response_type: Variant = response.get("type", null)
	if response_type != null and (typeof(response_type) != TYPE_STRING or (not str(response_type).is_empty() and str(response_type) != "mail.inbox_v1")):
		gameplay_view.set_command_busy(false)
		gameplay_view.show_notification("Mail change was confirmed, but the inbox refresh returned an unexpected response; reopen Mail inbox to reconcile it.")
		return
	var data := _response_data_dictionary(result.get("data", {}))
	var items: Variant = data.get("items", null)
	if not items is Array:
		gameplay_view.set_command_busy(false)
		gameplay_view.show_notification("Mail change was confirmed, but the server returned no readable inbox.")
		return
	gameplay_view.set_command_busy(false)
	gameplay_view.show_command_result("Mail inbox", data)

func _request_planet_surface_records() -> void:
	if _landed_planet_id <= 0:
		return
	_planet_read_refused = false
	gameplay_view.set_command_busy(true)
	gameplay_view.planet_workflow.refresh_details()

func _result_summary(data: Dictionary) -> String:
	for key in ["message", "status", "name", "sector_id", "amount", "balance"]:
		if data.has(key) and not str(data[key]).is_empty():
			return "%s: %s" % [key.replace("_", " ").capitalize(), str(data[key])]
	return ""

func _response_data_dictionary(value: Variant) -> Dictionary:
	if value is Dictionary:
		return value
	if value is Array:
		# Older server builds return planet.info as a one-row JSON array, while
		# newer builds return the row object directly.
		if value.size() == 1 and value[0] is Dictionary:
			return value[0]
		return {"items": value}
	return {}

func _response_planet_id(data: Dictionary) -> int:
	# `schema_planet_info` requires the top-level `planet_id`. Do not accept
	# legacy/nested aliases here: a read for one planet must never be correlated
	# with an unrelated object merely because a response-shaped record contains
	# an `id` field.
	if not data.has("planet_id"):
		return 0
	return _authoritative_positive_id(data.get("planet_id"))

func _response_sector_id(data: Dictionary) -> int:
	# Prefer the schema's explicit identity field. If it is present but
	# malformed, do not fall through to a legacy alias: doing so could make a
	# contradictory response appear correlated with the confirmed sector.
	var identity_keys := ["sector_id", "id", "current_sector"]
	for key in identity_keys:
		if data.has(key):
			var value: Variant = data.get(key)
			# Godot decodes JSON numbers as integral floats in some transport paths.
			# Accept only positive integral numeric values; reject strings, booleans
			# and fractional values without falling through to an alias.
			return _authoritative_positive_id(value)
	return 0

func _density_has_authoritative_ids(payload: Dictionary) -> bool:
	var rows: Variant = payload.get("sectors", payload.get("data", payload))
	if rows is Dictionary and rows.has("sectors"):
		rows = rows["sectors"]
	if rows is Dictionary:
		if rows.has("density"):
			return (_authoritative_positive_id(rows.get("sector_id", rows.get("id", 0))) > 0
				and _authoritative_density_value(rows.get("density")))
		return false
	if not rows is Array:
		return false
	# An explicitly returned empty row set is an authoritative no-result scan,
	# not a malformed payload. Preserve the response and let the presenter say
	# that no density values were returned; do not trigger an unrelated sector
	# refresh.
	if rows.is_empty():
		return true
	if rows[0] is Dictionary:
		for row in rows:
			if (not row is Dictionary
					or _authoritative_positive_id(row.get("sector_id", row.get("id", 0))) <= 0
					or not _authoritative_density_value(row.get("density"))):
				return false
		return true
	# Compact density responses are [sector_id, density, ...]. Every pair must
	# carry a positive server-provided sector ID; never let an array index become
	# an invented location.
	if rows.size() % 2 != 0:
		return false
	for index in range(0, rows.size(), 2):
		if (_authoritative_positive_id(rows[index]) <= 0
				or not _authoritative_density_value(rows[index + 1])):
			return false
	return true

func _scan_response_type_matches(command: String, response: Dictionary, request_data: Dictionary = {}) -> bool:
	# Visibility reads have one published response schema per operation. An
	# omitted or empty type is not a compatibility variant: accepting it could
	# render a success-shaped payload from an unrelated or unavailable command.
	var raw_type: Variant = response.get("type", null)
	if typeof(raw_type) != TYPE_STRING:
		return false
	var actual := str(raw_type)
	var expected := ""
	if command == "sector.scan" and request_data.get("mode", "") == "holographic":
		return actual == "sector.holographic.scan"
	match command:
		"move.describe_sector", "sector.info":
			expected = "sector.info"
		"move.scan", "sector.scan":
			expected = "sector.scan"
		"sector.scan.density":
			expected = "sector.density.scan"
		_:
			return false
	# The server's published response schemas are authoritative: detailed scans
	# use sector.scan and density scans use sector.density.scan.  Do not accept a
	# fabricated generic ".result" suffix, because that could render a
	# success-shaped response from a different or unavailable operation.
	return actual == expected

func _port_response_type_matches(response: Dictionary) -> bool:
	# `cmd_trade_port_info` publishes one authoritative response schema for the
	# `port.info` command. Missing/empty type is not a second success variant:
	# accepting it could let an unrelated delayed payload populate the trading
	# screen and authorize a quote against stale context.
	return typeof(response.get("type", null)) == TYPE_STRING and response.get("type") == "trade.port_info"

func _ship_inspect_response_type_matches(response: Dictionary) -> bool:
	# `ship.inspect` is the canonical response schema; do not accept a
	# command-name alias, omitted type, or an invented suffix.
	return typeof(response.get("type", null)) == TYPE_STRING and response.get("type") == "ship.inspect"

func _launch_response_type_matches(response: Dictionary) -> bool:
	# Launch success must identify the published response schema. Negative
	# outcomes use the shared error/refused envelope and may carry authoritative
	# hazard data; an omitted/empty type must not drive the state machine.
	var raw_type: Variant = response.get("type", null)
	if typeof(raw_type) != TYPE_STRING:
		return false
	var actual := str(raw_type)
	if actual == "planet.launch.success":
		return true
	return actual in ["error", "refused"] and str(response.get("status", "")) in ["error", "refused"]

func _authoritative_density_value(value: Variant) -> bool:
	# Godot may decode JSON integers as integral floats. Accept those values,
	# while still rejecting booleans, strings, fractional values, and negatives.
	return (typeof(value) == TYPE_INT and int(value) >= 0) or (typeof(value) == TYPE_FLOAT and is_finite(value) and value >= 0.0 and value == floor(value))

func _response_ship_id(data: Dictionary) -> int:
	var ship_value: Variant = data.get("ship", data)
	if not (ship_value is Dictionary):
		return 0
	for key in ["id", "ship_id"]:
		var value: Variant = ship_value.get(key, 0)
		# Ship identity is a JSON integer in the command schema. Do not coerce
		# integral floats into IDs: a schema-mismatched inspection response must
		# not be rendered as the selected authoritative ship.
		if typeof(value) == TYPE_INT and int(value) > 0:
			return int(value)
	return 0

func _valid_cargo_transfer_receipt(data: Dictionary, response: Dictionary, requested: Variant, active_ship_id: int) -> bool:
	# A successful transfer must be tied to the exact server-owned source and
	# target IDs submitted by the client.  Do not present a generic success as a
	# completed cargo mutation when a proxy or stale response is mismatched.
	if not requested is Dictionary or active_ship_id <= 0:
		return false
	# The published cargo-transfer receipt is the sole success contract. An
	# omitted/empty type is not a compatible response variant: accepting it
	# could present a success-shaped payload from another command as a cargo
	# mutation.
	if typeof(response.get("type", null)) != TYPE_STRING or response.get("type") != "ship.cargo_transferred_v1":
		return false
	var source_ship_id := _authoritative_positive_id(data.get("source_ship_id", 0))
	var target_ship_id := _authoritative_positive_id(data.get("target_ship_id", 0))
	var requested_target_id := _authoritative_positive_id(requested.get("target_ship_id", 0))
	var commodity_id := _authoritative_positive_id(data.get("commodity_id", 0))
	var requested_commodity_id := _authoritative_positive_id(requested.get("commodity_id", 0))
	var quantity := _authoritative_positive_id(data.get("quantity", 0))
	var requested_quantity := _authoritative_positive_id(requested.get("quantity", 0))
	return source_ship_id == active_ship_id and target_ship_id == requested_target_id and target_ship_id > 0 \
		and commodity_id == requested_commodity_id and quantity == requested_quantity \
		and typeof(data.get("source_remaining", null)) == TYPE_INT and int(data.get("source_remaining")) >= 0 \
		and typeof(data.get("target_quantity", null)) == TYPE_INT and int(data.get("target_quantity")) >= 0

func _ship_list_has_authoritative_ids(data: Dictionary) -> bool:
	var rows: Variant = data.get("ships", null)
	if not rows is Array:
		return false
	var ids := {}
	for row in rows:
		if not row is Dictionary:
			return false
		var ship_id := _authoritative_positive_id(row.get("ship_id", 0))
		if ship_id <= 0 or ids.has(ship_id):
			return false
		ids[ship_id] = true
		# `active` is a JSON boolean in ship.list.response.  A string or
		# numeric substitute cannot establish the active-ship relationship.
		if typeof(row.get("active", null)) != TYPE_BOOL:
			return false
	# The server owns lifecycle invariants such as the number of active ships.
	# An empty array and multiple active rows are still authoritative response
	# data; do not invent a client-side ownership rule or hide those rows.
	return true

func _response_port_id(data: Dictionary) -> int:
	var port_value: Variant = data.get("port", data)
	if not (port_value is Dictionary):
		return 0
	# An explicit canonical ID takes precedence over the compatibility alias.
	# If `id` is present but malformed, do not fall through to `port_id` and
	# turn a contradictory response into an actionable trade context.
	if port_value.has("id"):
		return _authoritative_positive_id(port_value.get("id"))
	if port_value.has("port_id"):
		return _authoritative_positive_id(port_value.get("port_id"))
	return 0

func _confirmed_port_id() -> int:
	var sector: Variant = gameplay_view._snapshot.get("sector", {})
	if not (sector is Dictionary):
		return 0
	var ports: Variant = sector.get("ports", [])
	if ports is Array:
		for candidate in ports:
			if candidate is Dictionary:
				var listed_id := _response_port_id(candidate)
				if listed_id > 0:
					return listed_id
	var port: Variant = sector.get("port", {})
	if not (port is Dictionary):
		return 0
	return _response_port_id(port)

func _accept_authoritative_port(data: Dictionary, label: String, expected_port_id: int = 0, expected_sector_id: int = 0) -> bool:
	var reported_port_id := _response_port_id(data)
	var confirmed_port_id := expected_port_id if expected_port_id > 0 else _confirmed_port_id()
	if confirmed_port_id <= 0 or reported_port_id <= 0 or reported_port_id != confirmed_port_id:
		var reason := "omitted a valid port ID" if reported_port_id <= 0 else "returned port %d, but the confirmed port is %d" % [reported_port_id, confirmed_port_id]
		print("[PORT_TRACE] rejected label=%s reported_port_id=%d expected_port_id=%d reason=%s" % [label, reported_port_id, confirmed_port_id, reason])
		gameplay_view.port_workflow.close_for_location_change()
		gameplay_view.show_notification("%s was not displayed because the response %s; refreshing current-sector state." % [label, reason])
		return false
	# Some server responses include the port's authoritative sector identity.
	# When present, correlate it with the sector captured when this read was
	# issued. Older responses may omit the optional field; omission is not a
	# second response variant and remains acceptable here.
	var port_value: Variant = data.get("port", data)
	if expected_sector_id > 0 and port_value is Dictionary:
		# The canonical port-info payload has appeared with both documented
		# location keys (`sector_id` and the legacy wire name `sector`).  If either
		# key is present, it is authoritative and must match the sector captured
		# when the request was issued. Never let an explicit remote/malformed
		# location fall through just because the other alias is absent.
		var sector_key := "sector_id" if port_value.has("sector_id") else ("sector" if port_value.has("sector") else "")
		if not sector_key.is_empty():
			var reported_sector_id := _authoritative_positive_id(port_value.get(sector_key))
			if reported_sector_id <= 0 or reported_sector_id != expected_sector_id:
				gameplay_view.port_workflow.close_for_location_change()
				gameplay_view.show_notification("%s was not displayed because the response did not identify the confirmed current sector; refreshing current-sector state." % label)
				return false
	return true

func _authoritative_positive_id(value: Variant) -> int:
	# Godot may decode a JSON integer as an integral float. Preserve the
	# authoritative numeric identity while rejecting strings, booleans and
	# fractional values.
	var valid: bool = (typeof(value) == TYPE_INT and int(value) > 0) or (typeof(value) == TYPE_FLOAT and is_finite(value) and value > 0.0 and value == floor(value))
	if not valid:
		return 0
	return int(value)

func _sector_snapshot_is_fresh() -> bool:
	# Treat malformed refresh metadata exactly like an unknown/stale snapshot.
	# The server remains authoritative for movement; a client-side shape error
	# must not turn into a dispatcher crash or an unbound transwarp request.
	var raw_freshness: Variant = gameplay_view._snapshot.get("freshness", {})
	if not raw_freshness is Dictionary:
		return false
	if str(raw_freshness.get("sector", "")) != "fresh":
		return false
	# Freshness is metadata, not the location identity. Require the HUD's
	# server-provided integer sector ID as well, matching the Python client's
	# transwarp origin gate. Never send movement from a snapshot that merely
	# claims to be fresh but cannot identify the confirmed origin.
	var hud_value: Variant = gameplay_view._snapshot.get("hud", {})
	if not hud_value is Dictionary:
		return false
	return _authoritative_positive_id(hud_value.get("sector_id", null)) > 0

func _planet_landing_terminal(value: Variant) -> bool:
	if not value is Dictionary:
		return false
	if value.get("ship_destroyed", false) == true or value.get("ship_remains_in_sector", true) == false:
		return true
	# Planetary defense reports may carry the authoritative terminal flag on
	# the individual combat row.  Inspect only documented boolean fields; do
	# not infer destruction from damage, hull, or fighter counters.
	for key in ["combat", "hazards"]:
		var rows: Variant = value.get(key, [])
		if not rows is Array:
			continue
		for row in rows:
			if not row is Dictionary:
				continue
			if row.get("destroyed", false) == true or row.get("ship_remains_in_sector", true) == false:
				return true
	return false

func _on_trade_requested(direction: String, port_id: int, sector_id: int, commodity: String, quantity: int, account: int) -> void:
	if not auth_session.is_authenticated() or not _pending_trade.is_empty() or _pending_game_commands.size() > 0 or _command_refresh_pending:
		gameplay_view.show_notification("Another server command is pending. Wait for it to finish before trading.")
		return
	var commodity_code := commodity.strip_edges()
	if (not ["buy", "sell"].has(direction)
			or commodity_code.is_empty()
			or quantity <= 0
			or not [-1, 0, 1].has(account)):
		gameplay_view.show_notification("Trade was not sent because its commodity, quantity, or account was invalid.")
		return
	var confirmed_sector_id := _authoritative_positive_id(gameplay_view._snapshot.get("hud", {}).get("sector_id", 0))
	var confirmed_port_id := _confirmed_port_id()
	var sector_is_fresh := str(gameplay_view._snapshot.get("freshness", {}).get("sector", "")) == "fresh"
	if (_authoritative_positive_id(port_id) <= 0
			or _authoritative_positive_id(sector_id) <= 0
			or confirmed_sector_id <= 0
			or confirmed_port_id <= 0
			or not sector_is_fresh
			or port_id != confirmed_port_id
			or sector_id != confirmed_sector_id):
		# The port workflow can outlive a sector snapshot update. Do not turn a
		# stale UI selection into a quote for a different location; reconcile the
		# current sector before allowing another trade attempt.
		gameplay_view.port_workflow.close_for_location_change()
		gameplay_view.set_command_busy(false)
		var reason := "the current sector is stale" if not sector_is_fresh else "the selected port is not the confirmed current port"
		gameplay_view.show_notification("Trade was not sent because %s; refreshing sector state." % reason)
		_refresh_after_command(["sector"])
		return
	_pending_trade = {"direction": direction, "port_id": port_id, "sector_id": sector_id, "commodity": commodity_code, "quantity": quantity, "account": account}
	gameplay_view.set_command_busy(true)
	# Send the validated command value.  The workflow may receive presentation
	# input with surrounding whitespace; the server owns commodity identity, but
	# the quote must use the exact validated string that is correlated with the
	# pending trade and subsequent authoritative receipt.
	var request_id: String = transport.request("trade.quote", {"port_id": port_id, "commodity": commodity_code, "quantity": quantity}, auth_session.session_token)
	if request_id.is_empty():
		_pending_trade.clear()
		gameplay_view.set_command_busy(false)
		gameplay_view.show_notification("Quote request could not be sent.")
		return
	_pending_game_commands[request_id] = {"kind": "trade_quote", "label": "Trade quote"}
	gameplay_view.show_notification("Requesting the server's current trade quote…")

func _route_from_response(data: Dictionary) -> Array:
	var path = data.get("path", data.get("steps", data.get("sectors", [])))
	if not (path is Array):
		return []
	return path

func _request_autopilot_status() -> void:
	if not auth_session.is_authenticated() or not _pending_game_commands.is_empty():
		return
	var request_id: String = transport.request("move.autopilot.status", {}, auth_session.session_token)
	if not request_id.is_empty():
		_pending_game_commands[request_id] = {"kind": "autopilot_status"}

func _finish_autopilot_status(response: Dictionary) -> void:
	var result := Protocol.result_from_response(response)
	if result.get("kind") != "ok":
		return
	var data := _response_data_dictionary(result.get("data", {}))
	_autopilot_mode = str(data.get("mode", "manual"))
	var state := str(data.get("state", "idle"))
	var status_sector := _authoritative_positive_id(data.get("current_sector_id", 0))
	var hud_value: Variant = gameplay_view._snapshot.get("hud", {}).get("sector_id", 0)
	var confirmed_sector := _authoritative_positive_id(hud_value)
	var ship_sector := _authoritative_positive_id(gameplay_view._snapshot.get("ship", {}).get("sector_id", 0))
	if confirmed_sector > 0 and ship_sector > 0 and ship_sector != confirmed_sector:
		_planned_route.clear()
		_route_destination = 0
		gameplay_view.clear_planned_route()
		gameplay_view.show_notification("Autopilot paused · confirmed player and ship sectors disagree; refreshing authoritative movement state.")
		refresh_coordinator.refresh(true, ["player", "ship", "sector"])
		return
	if status_sector > 0 and confirmed_sector > 0 and status_sector != confirmed_sector:
		# The HUD/player/ship refresh is authoritative for location. Re-anchor the
		# saved path at that confirmed sector instead of discarding the route.
		var saved_path: Array = data.get("path", []) if data.get("path", []) is Array else []
		var confirmed_index := saved_path.find(confirmed_sector)
		if confirmed_index >= 0 and confirmed_index + 1 < saved_path.size():
			_planned_route.clear()
			for index in range(confirmed_index + 1, saved_path.size()):
				var next_sector = saved_path[index]
				if typeof(next_sector) == TYPE_INT or typeof(next_sector) == TYPE_FLOAT:
					_planned_route.append(int(next_sector))
			_route_destination = int(data.get("target_sector_id", _planned_route.back()))
			_route_stop_after_next = _autopilot_mode == "stop_at_next"
			gameplay_view.show_restored_autopilot_route(_planned_route, _route_destination, "reconciled", _autopilot_mode)
			gameplay_view.show_notification("Autopilot status was stale (server Sector %d, confirmed Sector %d); route re-anchored and ready to resume." % [status_sector, confirmed_sector])
		else:
			_planned_route.clear()
			_route_destination = 0
			gameplay_view.clear_planned_route()
			gameplay_view.show_notification("Autopilot route could not be re-anchored at confirmed Sector %d; refresh and plot a new route." % confirmed_sector)
		refresh_coordinator.refresh(true, ["player", "ship", "sector"])
		return
	if state in ["idle", "complete"]:
		_planned_route.clear()
		_route_destination = 0
		gameplay_view.clear_planned_route()
		return
	if state == "reconcile_required":
		_planned_route.clear()
		_route_destination = 0
		gameplay_view.clear_planned_route()
		gameplay_view.show_notification("Saved route needs reconciliation · current sector is outside its path.")
		return
	var path: Array = data.get("path", []) if data.get("path", []) is Array else []
	var current_index := path.find(int(data.get("current_sector_id", 0)))
	if current_index < 0 or current_index + 1 >= path.size():
		_planned_route.clear()
		_route_destination = 0
		gameplay_view.clear_planned_route()
		return
	_planned_route.clear()
	for index in range(current_index + 1, path.size()):
		var sector = path[index]
		if typeof(sector) == TYPE_INT or typeof(sector) == TYPE_FLOAT:
			_planned_route.append(int(sector))
	if _planned_route.is_empty():
		gameplay_view.clear_planned_route()
		return
	_route_destination = int(data.get("target_sector_id", _planned_route.back()))
	_route_stop_after_next = _autopilot_mode == "stop_at_next"
	gameplay_view.show_restored_autopilot_route(_planned_route, _route_destination, state, _autopilot_mode)

func _cancel_planned_route() -> void:
	if _route_running:
		if not _route_stop_requested:
			_route_stop_requested = true
			_planned_route.clear()
			gameplay_view.set_autonav_status("AUTONAV · finishing the current server-confirmed warp before stopping.", true)
			gameplay_view.show_notification("Autonav stop requested · the in-flight warp cannot be recalled.")
		return
	_on_command_requested("move.autopilot.stop", {}, "Stop saved autopilot route", true)

func _engage_planned_route(mode: String = "express") -> void:
	if _route_running or _planned_route.is_empty():
		return
	_autopilot_mode = "single_step" if mode == "single_step" else "express"
	_route_running = true
	_route_stop_requested = false
	_route_stop_after_next = _autopilot_mode == "single_step"
	_route_total_hops = _planned_route.size()
	_route_completed_hops = 0
	_route_current_sector = _authoritative_positive_id(gameplay_view._snapshot.get("hud", {}).get("sector_id", 0))
	gameplay_view.set_autonav_status("AUTONAV %s · %d confirmed warp%s planned · preparing first leg." % [_autopilot_mode.replace("_", " ").to_upper(), _route_total_hops, "" if _route_total_hops == 1 else "s"])
	gameplay_view.show_notification("%s mode engaged · waiting for the first warp confirmation." % _autopilot_mode.replace("_", " ").capitalize())
	_send_next_route_hop()

func _send_next_route_hop() -> void:
	if _planned_route.is_empty():
		_route_running = false
		_route_stop_after_next = false
		var was_stopped := _route_stop_requested
		_route_stop_requested = false
		gameplay_view.clear_planned_route()
		gameplay_view.show_notification("Autonav stopped at the last confirmed sector." if was_stopped else "Autonav complete · destination reached and confirmed.")
		_refresh_after_warp_attempt()
		return
	var next := int(_planned_route.pop_front())
	var current_text := "current Sector %d · " % _route_current_sector if _route_current_sector > 0 else ""
	gameplay_view.set_autonav_status("AUTONAV · %swarp %d of %d · requesting Sector %d." % [current_text, _route_completed_hops + 1, _route_total_hops, next])
	var request_id: String = transport.request("move.warp", {"to_sector_id": next}, auth_session.session_token)
	if request_id.is_empty():
		_route_running = false
		_route_stop_requested = false
		_planned_route.push_front(next)
		_route_destination = int(_planned_route.back())
		gameplay_view.show_restored_autopilot_route(_planned_route, _route_destination, "paused", _autopilot_mode)
		gameplay_view.show_notification("Autonav paused · the next warp was not sent. Resume when the connection is ready.")
		return
	_pending_game_commands[request_id] = {"kind": "route_warp", "destination": next}
	gameplay_view.show_notification("Autonav · requesting warp to Sector %d…" % next)

func _finish_route_warp(response: Dictionary, operation: Dictionary) -> void:
	var result := Protocol.result_from_response(response)
	if not _movement_response_type_matches(response, "move.result"):
		_route_running = false
		_route_stop_requested = false
		_route_stop_after_next = false
		_planned_route.push_front(int(operation.get("destination", 0)))
		_route_destination = int(_planned_route.back()) if not _planned_route.is_empty() else 0
		if not _planned_route.is_empty():
			gameplay_view.show_restored_autopilot_route(_planned_route, _route_destination, "paused", _autopilot_mode)
		gameplay_view.show_notification("Autonav paused · warp response was not recognized. Remaining route preserved while location is refreshed.")
		_refresh_after_command(["player", "ship", "sector"])
		return
	var hazards := Protocol.move_hazard_report(response)
	if not hazards.is_empty():
		gameplay_view.show_information("Sector hazards", hazards)
	var terminal_outcome := Protocol.move_terminal_outcome(response)
	if terminal_outcome.get("ship_destroyed", false) == true or terminal_outcome.get("ship_remains_in_sector", true) == false:
		# An explicit terminal entry result takes precedence over the ordinary
		# refusal path.  The requested route is no longer actionable after the
		# server reports destruction/non-arrival, so do not preserve it as a
		# resumable route while aftermath is being refreshed.
		_route_running = false
		_planned_route.clear()
		_route_stop_requested = false
		_route_stop_after_next = false
		gameplay_view.clear_planned_route()
		gameplay_view.show_notification("Autonav stopped · the server reported a terminal hazard outcome. Refreshing confirmed state.")
		_refresh_after_command(["player", "ship", "sector"])
		return
	if result.get("kind") != "ok":
		_route_running = false
		_route_stop_requested = false
		_planned_route.push_front(int(operation.get("destination", 0)))
		_route_destination = int(_planned_route.back()) if not _planned_route.is_empty() else 0
		if not _planned_route.is_empty():
			gameplay_view.show_restored_autopilot_route(_planned_route, _route_destination, "paused", _autopilot_mode)
		gameplay_view.show_notification("Autonav paused · warp refused · %s. Resume after correcting the route." % str(result.get("message", "server refusal")))
		_refresh_after_warp_attempt()
		return
	var confirmed_sector := Protocol.confirmed_move_sector(result.get("data", {}))
	if confirmed_sector <= 0 or confirmed_sector != int(operation.get("destination", 0)):
		_route_running = false
		_planned_route.clear()
		_route_stop_requested = false
		gameplay_view.clear_planned_route()
		gameplay_view.show_notification("Autonav stopped · server did not confirm the requested sector. Refreshing your location.")
		_refresh_after_warp_attempt()
		return
	_route_completed_hops += 1
	_route_current_sector = confirmed_sector
	if _route_stop_requested:
		_route_running = false
		_route_stop_requested = false
		_route_stop_after_next = false
		_planned_route.clear()
		gameplay_view.set_autonav_status("AUTONAV · sector %d confirmed · saving the stopped route." % int(operation.get("destination", 0)), true)
		var stop_request_id: String = transport.request("move.autopilot.stop", {}, auth_session.session_token)
		if stop_request_id.is_empty():
			gameplay_view.clear_planned_route()
			gameplay_view.show_notification("Autonav stopped locally · server route stop could not be sent; it will be reconciled after reconnect.")
			_refresh_after_warp_attempt()
			return
		_pending_game_commands[stop_request_id] = {"kind": "route_stop_confirmation"}
		return
	if _route_stop_after_next:
		_route_running = false
		_route_stop_after_next = false
		if _autopilot_mode == "single_step" and not _planned_route.is_empty():
			gameplay_view.show_single_step_ready(_planned_route, _route_destination, confirmed_sector)
			gameplay_view.show_notification("Single step complete · Sector %d confirmed. Continue for the next sector or cancel the route." % confirmed_sector)
			_refresh_after_warp_attempt()
			return
		_planned_route.clear()
		gameplay_view.clear_planned_route()
		gameplay_view.show_notification("Autopilot stopped at the next confirmed sector.")
		_refresh_after_warp_attempt()
		return
	_send_next_route_hop()

func _finish_trade_quote(response: Dictionary) -> void:
	var result: Dictionary = Protocol.result_from_response(response)
	if result.get("kind") != "ok":
		_pending_trade.clear()
		# A quote refusal is terminal for this request, not for the dock. Clear
		# the command rail before refreshing the authoritative port so the player
		# can retry after the server's current direction/stock is displayed.
		gameplay_view.set_command_busy(false)
		# A quote refusal can mean the cached port stock, direction, or current
		# location changed after the port was opened.  Reconcile the authoritative
		# port record before allowing another quote; never reuse stale inventory.
		_request_port_refresh()
		gameplay_view.show_notification("Trade quote refused · %s · refreshing port stock." % str(result.get("message", "Quote unavailable.")))
		return
	var trade_data: Dictionary = _pending_trade.duplicate(true)
	var quote: Variant = result.get("data")
	var unit_key := "buy_price" if trade_data.get("direction") == "buy" else "sell_price"
	var total_key := "total_buy_price" if trade_data.get("direction") == "buy" else "total_sell_price"
	if not _trade_quote_response_type_matches(response):
		_pending_trade.clear()
		gameplay_view.set_command_busy(false)
		gameplay_view.show_notification("Trade quote had an unexpected response type. No trade was made; refreshing port stock.")
		_request_port_refresh()
		return
	if not _valid_trade_quote(quote, trade_data, unit_key, total_key):
		_pending_trade.clear()
		gameplay_view.set_command_busy(false)
		gameplay_view.show_notification("Trade quote was incomplete or did not match the request. No trade was made.")
		# The cached port may have changed while the quote was in flight.  Keep
		# the refusal truthful and reconcile the displayed inventory before the
		# player can choose another transaction.
		_request_port_refresh()
		return
	# The quote is the authoritative commodity representation for the
	# settlement request.  Preserve the server's canonical code so a player
	# entering a case-variant value cannot produce a valid trade whose receipt
	# is later rejected by the client-side line correlation check.
	_pending_trade["commodity"] = str(quote["commodity"])
	gameplay_view.set_command_busy(false)
	# The quote owns the canonical commodity representation.  Keep the
	# confirmation dialog aligned with the exact line that will be settled;
	# otherwise a case-variant UI selection can display one commodity while the
	# subsequent authoritative request uses another spelling.
	gameplay_view.confirm_trade_quote(str(trade_data["direction"]), str(_pending_trade["commodity"]), int(trade_data["quantity"]), int(trade_data["account"]), quote)

func _trade_quote_response_type_matches(response: Dictionary) -> bool:
	# The V3 contract names the quote response. An envelope without that
	# authoritative type is not a quote and must not open a settlement dialog.
	return typeof(response.get("type", null)) == TYPE_STRING and response.get("type") == "trade.quote"

func _valid_trade_quote(quote: Variant, trade_data: Dictionary, unit_key: String, total_key: String) -> bool:
	if not quote is Dictionary:
		return false
	# JSON numeric values may arrive as integral floats from Godot's parser.
	# Preserve the identity/quantity checks while accepting that lossless wire
	# representation; strings, booleans and fractional values remain invalid.
	var quoted_port_id := _authoritative_positive_id(quote.get("port_id", null))
	var quoted_quantity := _authoritative_positive_id(quote.get("quantity", null))
	if quoted_port_id <= 0 or quoted_quantity <= 0:
		return false
	for key in [unit_key, total_key]:
		if not quote.has(key):
			continue
		var price: Variant = quote.get(key)
		var valid_price := (typeof(price) == TYPE_INT or typeof(price) == TYPE_FLOAT)
		# The authoritative schema permits JSON numbers.  Do not narrow that
		# contract to integral values: the server may return a real-valued quote.
		if (not valid_price or not is_finite(float(price)) or float(price) < 0.0):
			return false
	var requested_port_id: Variant = trade_data.get("port_id", 0)
	var requested_quantity: Variant = trade_data.get("quantity", 0)
	if (typeof(requested_port_id) != TYPE_INT or int(requested_port_id) <= 0
			or typeof(requested_quantity) != TYPE_INT or int(requested_quantity) <= 0):
		return false
	var quoted_commodity: Variant = quote.get("commodity", null)
	# Commodity codes are authoritative protocol identity, not display text.
	# Reject whitespace-padded values instead of opening a confirmation that
	# would send a malformed/non-canonical item line to the server.
	if (typeof(quoted_commodity) != TYPE_STRING
			or quoted_commodity.is_empty()
			or quoted_commodity != quoted_commodity.strip_edges()):
		return false
	return quoted_port_id == int(requested_port_id) and quoted_quantity == int(requested_quantity) and quoted_commodity.to_upper() == str(trade_data.get("commodity", "")).to_upper()

func _valid_trade_receipt(receipt: Dictionary, request_data: Variant, response_type: String = "", command_direction: String = "", expected_player_id: int = 0) -> bool:
	if not request_data is Dictionary:
		return false
	var direction := command_direction if not command_direction.is_empty() else str(request_data.get("direction", ""))
	if not ["buy", "sell"].has(direction):
		return false
	# The V3 contract names the receipt response. Do not accept an omitted or
	# null type as a second response variant.
	if response_type != "trade.%s_receipt_v1" % direction:
		return false
	var receipt_port_id := _authoritative_positive_id(receipt.get("port_id", null))
	var receipt_sector_id := _authoritative_positive_id(receipt.get("sector_id", null))
	if receipt_port_id <= 0 or receipt_sector_id <= 0:
		return false
	if typeof(request_data.get("port_id")) != TYPE_INT or int(request_data["port_id"]) <= 0 or typeof(request_data.get("sector_id")) != TYPE_INT or int(request_data["sector_id"]) <= 0:
		return false
	if receipt_port_id != int(request_data["port_id"]) or receipt_sector_id != int(request_data["sector_id"]):
		return false
	if receipt.has("player_id"):
		# A receipt identity is only actionable when it can be bound to the
		# authenticated session.  Do not present a valid-looking settlement for
		# an unknown/other player while the session identity is unavailable.
		var receipt_player_id := _authoritative_positive_id(receipt.get("player_id", null))
		if receipt_player_id <= 0:
			return false
		if expected_player_id <= 0 or receipt_player_id != expected_player_id:
			return false
	else:
		# The published receipt always identifies the settling player. An omitted
		# identity is not a compact response variant, even if the local session
		# snapshot is incomplete; it cannot authorize this settlement.
		return false
	var lines: Variant = receipt.get("lines", null)
	# The V3 receipt schema includes settlement lines. Missing or empty lines
	# cannot prove that this receipt settles the requested commodity/quantity,
	# so do not present a compact success-shaped response as a completed trade.
	if not lines is Array or lines.is_empty() or not (request_data.get("items", []) is Array) or request_data["items"].size() != 1:
		return false
	var requested: Variant = request_data["items"][0]
	if not requested is Dictionary:
		return false
	var requested_commodity: Variant = requested.get("commodity", null)
	var requested_quantity: Variant = requested.get("quantity", null)
	if (typeof(requested_commodity) != TYPE_STRING
			or requested_commodity.is_empty()
			or requested_commodity != requested_commodity.strip_edges()
			or typeof(requested_quantity) != TYPE_INT
			or int(requested_quantity) <= 0):
		return false
	var total := 0
	for line in lines:
		# Receipt identity is server-owned. Do not stringify malformed JSON
		# values into a commodity code or accept a presentation-only alias.
		if not line is Dictionary or typeof(line.get("commodity", null)) != TYPE_STRING:
			return false
		if line.get("commodity") != requested_commodity:
			return false
		var line_quantity := _authoritative_positive_id(line.get("quantity", null))
		if line_quantity <= 0:
			return false
		total += line_quantity
	return total == int(requested_quantity)

func _on_trade_confirmation(accepted: bool) -> void:
	if _pending_trade.is_empty():
		return
	if not accepted:
		_pending_trade.clear()
		gameplay_view.show_notification("Trade cancelled. No changes were made.")
		return
	var trade_data: Dictionary = _pending_trade.duplicate(true)
	# A quote is read-only and may outlive a reconnect or authoritative sector
	# refresh while the confirmation dialog is open.  Do not send a settlement
	# for a different port/location; the server remains the authority for quote
	# validity, but the client must not turn a stale UI selection into a new
	# protocol request.
	var confirmed_sector_id := _authoritative_positive_id(gameplay_view._snapshot.get("hud", {}).get("sector_id", 0))
	var confirmed_port_id := _confirmed_port_id()
	var sector_is_fresh := str(gameplay_view._snapshot.get("freshness", {}).get("sector", "")) == "fresh"
	var requested_sector_id := _authoritative_positive_id(trade_data.get("sector_id", 0))
	var requested_port_id := _authoritative_positive_id(trade_data.get("port_id", 0))
	if (not sector_is_fresh or confirmed_sector_id <= 0 or confirmed_port_id <= 0
			or requested_sector_id != confirmed_sector_id
			or requested_port_id != confirmed_port_id):
		_pending_trade.clear()
		_pending_trade["refresh_port_after"] = true
		gameplay_view.set_command_busy(false)
		gameplay_view.show_notification("Trade quote expired because the confirmed port or sector changed; refreshing before retry.")
		_refresh_after_command(["player", "ship", "sector"])
		return
	var direction := str(trade_data["direction"])
	var command := "trade.%s" % direction
	var payload := {
		"port_id": int(trade_data["port_id"]),
		"sector_id": int(trade_data["sector_id"]),
		"items": [{"commodity": str(trade_data["commodity"]), "quantity": int(trade_data["quantity"])}],
		"idempotency_key": "godot-%d-%d" % [Time.get_ticks_usec(), randi()],
	}
	# Omitting account is the authoritative protocol path for the saved
	# player.set_trade_account_preference. Do not send the UI sentinel -1.
	var selected_account := int(trade_data.get("account", -1))
	if selected_account >= 0:
		payload["account"] = selected_account
	var request_id: String = transport.request(command, payload, auth_session.session_token)
	if request_id.is_empty():
		_pending_trade.clear()
		gameplay_view.set_command_busy(false)
		gameplay_view.show_notification("Trade was not sent. Your quoted amount has not been charged.")
		return
	_pending_game_commands[request_id] = {"kind": "command", "command": command, "label": direction.capitalize(), "data": payload.duplicate(true), "mutating": true}
	gameplay_view.set_command_busy(true)
	gameplay_view.show_notification("Trade submitted · waiting for server confirmation.")

func _request_port_refresh() -> void:
	if not auth_session.is_authenticated():
		_pending_trade.clear()
		return
	# port.info requires an authoritative location selector. Reuse the confirmed
	# port when the sector snapshot identifies one; otherwise fall back to the
	# confirmed current sector. Never send an empty selector after a trade because
	# that would turn a refresh into a predictable server refusal.
	var refresh_data: Dictionary = {}
	var confirmed_port_id := _confirmed_port_id()
	var confirmed_sector_id := _authoritative_positive_id(gameplay_view._snapshot.get("hud", {}).get("sector_id", 0))
	if confirmed_port_id > 0:
		refresh_data["port_id"] = confirmed_port_id
	elif confirmed_sector_id > 0:
		refresh_data["sector_id"] = confirmed_sector_id
	if refresh_data.is_empty():
		_pending_trade.clear()
		gameplay_view.set_command_busy(false)
		gameplay_view.port_workflow.close_for_location_change()
		gameplay_view.show_notification("Port inventory could not be refreshed because the current location is not confirmed.")
		return
	var request_id: String = transport.request("port.info", refresh_data, auth_session.session_token)
	if request_id.is_empty():
		_pending_trade.clear()
		gameplay_view.set_command_busy(false)
		gameplay_view.port_workflow.close_for_location_change()
		gameplay_view.show_notification("Port inventory could not be refreshed; reopen the port to obtain current data.")
		return
	_pending_game_commands[request_id] = {"kind": "port_refresh", "label": "Port refresh", "sector_id": confirmed_sector_id}
	_pending_trade.clear()

func _on_client_state_changed(snapshot: Dictionary) -> void:
	gameplay_view.set_snapshot(snapshot)

func _on_protocol_error(message: String) -> void:
	if auth_session.is_authenticated():
		gameplay_view.show_notification("The server returned an invalid response.")
	else:
		login_view.set_status("The server returned an invalid response. Try again.")
	log_network("PROTOCOL_ERROR", message)

func _on_protocol_warning(message: String) -> void:
	log_network("PROTOCOL_WARNING", message)

func _on_session_invalidated(reason: String) -> void:
	log_network("SESSION_INVALIDATED", reason)

func _on_reconnect_requested() -> void:
	login_view.set_overlay_mode(true)
	login_view.visible = true
	login_view.set_connection_values(server_ip, server_port, auth_username)
	login_view.set_status("Reconnect to refresh the displayed state.")

func _subscribe_to_player_events() -> void:
	# The published V2 event contract defines `topic` for subscription commands.
	# system.notice is locked and auto-enabled by the server after login.
	# Combat destruction events identify the victim player and are the trigger
	# for the existing authoritative aftermath refresh.  Subscribe to the
	# published combat topic; the refresh still decides the escape-pod,
	# cargo, credit, and location facts.
	# broadcast.global is published by the server but is not an accepted client
	# subscription pattern; global chat delivery uses the chat/global event path.
	for topic in ["sector.*", "combat.*"]:
		var request_id: String = transport.request(
			"subscribe.add", {"topic": topic}, auth_session.session_token)
		if request_id.is_empty():
			gameplay_view.show_notification("Live event feed unavailable · subscription could not be sent.")
		else:
			_pending_event_subscriptions[request_id] = topic

func _present_server_event(event: Dictionary) -> void:
	var event_type := str(event.get("type", ""))
	if event_type in ["ferengi.trader.offer_v1", "ferengi.offer.created_v1"]:
		gameplay_view.command_menu.set_ferengi_offer_available(true)
		gameplay_view.show_notification("A Ferengi trade offer is available in this sector.")
	if auth_session.is_authenticated() and EventPresenter.targets_player_ship_destruction(event, auth_session.player_id):
		# The event identifies only that the server resolved destruction.  The
		# canonical escape-pod, cargo, credit, and location outcomes arrive via
		# the normal authoritative refresh contract.
		gameplay_view.show_notification("Ship destruction reported · refreshing authoritative state…")
		_refresh_after_command(["player", "ship", "sector"])
	var message := EventPresenter.notification_for(event)
	if not message.is_empty():
		_activity_history.append(message)
		while _activity_history.size() > 50:
			_activity_history.pop_front()
		gameplay_view.set_activity_history(_activity_history)
		gameplay_view.show_notification(message)

func _on_transport_diagnostic(kind: String, payload: String) -> void:
	log_network(kind, payload)

func _load_config() -> void:
	var config := ConfigFile.new()
	if config.load(CONFIG_PATH) == OK:
		server_ip = str(config.get_value("server", "ip", ""))
		server_port = int(config.get_value("server", "port", 0))
		auth_username = str(config.get_value("auth", "username", ""))
		var stored_profiles: Variant = config.get_value("server", "profiles", [])
		if stored_profiles is Array:
			for candidate in stored_profiles:
				if not candidate is Dictionary:
					continue
				var name := str(candidate.get("name", "")).strip_edges()
				var host := str(candidate.get("host", "")).strip_edges()
				var port := int(candidate.get("port", 0))
				if name.is_empty() or host.is_empty() or port < 1 or port > 65535:
					continue
				server_profiles.append({"name": name, "host": host, "port": port, "username": str(candidate.get("username", ""))})
		selected_server_profile = int(config.get_value("server", "selected_profile", -1))
	if server_profiles.is_empty() and not server_ip.strip_edges().is_empty() and server_port > 0 and server_port <= 65535:
		var profile_name := "%s:%d" % [server_ip, server_port]
		if server_ip.to_lower() in ["localhost", "127.0.0.1", "::1"]:
			profile_name = "Local server"
		server_profiles.append({"name": profile_name, "host": server_ip, "port": server_port, "username": auth_username})
		selected_server_profile = 0
	# Recover the first saved connection when an older client left the selected
	# index unset (for example after the captain name was edited). The profile is
	# still valid for the server; the username can be changed independently.
	if selected_server_profile < 0 or selected_server_profile >= server_profiles.size():
		selected_server_profile = 0 if not server_profiles.is_empty() else -1

func _save_config() -> void:
	var config := ConfigFile.new()
	config.load(CONFIG_PATH)
	config.set_value("server", "ip", server_ip)
	config.set_value("server", "port", server_port)
	config.set_value("server", "profiles", server_profiles.duplicate(true))
	config.set_value("server", "selected_profile", selected_server_profile)
	config.set_value("auth", "username", auth_username)
	config.save(CONFIG_PATH)

func _on_profiles_changed(profiles: Array, selected_index: int) -> void:
	server_profiles.clear()
	for profile in profiles:
		if profile is Dictionary:
			server_profiles.append({
				"name": str(profile.get("name", "")),
				"host": str(profile.get("host", "")),
				"port": int(profile.get("port", 0)),
				"username": str(profile.get("username", "")),
			})
	selected_server_profile = selected_index if selected_index >= 0 and selected_index < server_profiles.size() else -1
	server_ip = login_view.host_input.text.strip_edges()
	server_port = int(login_view.port_input.text)
	auth_username = login_view.username_input.text.strip_edges()
	_save_config()

func log_network(kind: String, message: String) -> void:
	if network_log_file:
		var timestamp := Time.get_datetime_string_from_system(false, true)
		network_log_file.store_line("[%s] %s: %s" % [timestamp, kind, message])
		network_log_file.flush()
