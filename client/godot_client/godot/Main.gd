extends Control

const Protocol = preload("res://Protocol.gd")
const ProtocolTransport = preload("res://ProtocolTransport.gd")
const AuthSession = preload("res://AuthSession.gd")
const ClientState = preload("res://ClientState.gd")
const AuthoritativeRefresh = preload("res://AuthoritativeRefresh.gd")
const EventPresenter = preload("res://EventPresenter.gd")
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
var _login_password := ""
var _connection_started := false
var _authenticated_once := false
var _pending_game_commands: Dictionary = {}
var _warp_refresh_pending := false
var _command_refresh_pending := false
var _pending_trade: Dictionary = {}
var _landed_planet_id := 0
var _planet_pending_count := 0
var _activity_history: Array[String] = []

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
	gameplay_view.reconnect_requested.connect(_on_reconnect_requested)
	gameplay_view.warp_requested.connect(_on_warp_requested)
	gameplay_view.warp_activated.connect(_on_warp_requested)
	gameplay_view.command_requested.connect(_on_command_requested)
	gameplay_view.trade_requested.connect(_on_trade_requested)
	gameplay_view.trade_confirmation.connect(_on_trade_confirmation)
	_load_config()
	login_view.set_connection_values(server_ip if not server_ip.is_empty() else "127.0.0.1", server_port if server_port > 0 else 1234, auth_username)
	login_view.set_status("Enter your captain credentials to begin.")
	gameplay_view.visible = false
	login_view.visible = true
	network_log_file = FileAccess.open("user://network_debug.log", FileAccess.WRITE)
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
	server_ip = host
	server_port = port
	auth_username = username
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
	_connection_started = false
	_submit_login()

func _submit_login() -> void:
	var payload: Dictionary = auth_session.begin_login(auth_username, _login_password)
	_login_password = ""
	if payload.is_empty():
		login_view.set_status("Enter your captain name and password.")
		return
	login_view.set_status("Authenticating captain…", true)
	transport.request("auth.login", payload)

func _on_transport_disconnected(reason: String) -> void:
	_connection_started = false
	_pending_game_commands.clear()
	_command_refresh_pending = false
	_warp_refresh_pending = false
	_planet_pending_count = 0
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
		login_view.set_status("Connection lost. Check the server address and retry.")
	if network_log_file:
		log_network("DISCONNECTED", reason)

func _on_transport_reply(request_id: String, response: Dictionary, command: String) -> void:
	if _pending_game_commands.has(request_id):
		var operation: Dictionary = _pending_game_commands[request_id]
		_pending_game_commands.erase(request_id)
		if operation.get("kind") == "planet_info" or operation.get("kind") == "planet_colonists":
			var planet_result: Dictionary = Protocol.result_from_response(response)
			if planet_result.get("kind") == "ok":
				if operation.get("kind") == "planet_info":
					gameplay_view.set_planet_information(planet_result.get("data", {}))
				else:
					gameplay_view.set_planet_colonist_state(planet_result.get("data", {}))
			else:
				gameplay_view.show_notification("Planet records unavailable · %s" % str(planet_result.get("message", "The server refused the request.")))
			_planet_pending_count = maxi(0, _planet_pending_count - 1)
			if _planet_pending_count == 0 and not _command_refresh_pending:
				gameplay_view.set_command_busy(false)
		elif operation.get("kind") == "warp":
			await _finish_warp_reply(response, int(operation["destination"]))
		elif operation.get("kind") == "trade_quote":
			_finish_trade_quote(response)
		elif operation.get("kind") == "port_refresh":
			var port_result: Dictionary = Protocol.result_from_response(response)
			gameplay_view.set_command_busy(false)
			if port_result.get("kind") == "ok":
				gameplay_view.enter_port_workflow(port_result.get("data", {}))
			else:
				gameplay_view.show_notification("Port refresh refused · %s" % str(port_result.get("message", "No current port data.")))
		else:
			await _finish_command_reply(response, operation)
		return
	if refresh_coordinator.accepts_request(request_id):
		refresh_coordinator.handle_reply(request_id, response, command)
		return
	if command == "auth.login" or str(response.get("type", "")) == "auth.session":
		if not auth_session.handle_response(response):
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
	elif command == "move.warp":
		_pending_game_commands.erase(request_id)
		gameplay_view.show_notification("Warp response timed out. Checking the server's current state…")
		_refresh_after_warp_attempt()
	elif _pending_game_commands.has(request_id):
		var timed_out: Dictionary = _pending_game_commands[request_id]
		_pending_game_commands.erase(request_id)
		if timed_out.get("kind") in ["planet_info", "planet_colonists"]:
			_planet_pending_count = maxi(0, _planet_pending_count - 1)
			gameplay_view.show_notification("Planet record request timed out. No mutation was made.")
			if _planet_pending_count == 0 and not _command_refresh_pending:
				gameplay_view.set_command_busy(false)
			return
		if str(timed_out.get("command", "")) == "planet.colonists.set":
			gameplay_view.show_notification("Colonist-transfer reply timed out; checking the confirmed planet and ship state.")
			_request_planet_surface_records()
			_refresh_after_command(["player", "ship"])
			return
		if timed_out.get("kind") == "trade_quote":
			_pending_trade.clear()
			gameplay_view.set_command_busy(false)
			gameplay_view.show_notification("Trade quote timed out. No trade was made.")
			return
		if timed_out.get("kind") == "port_refresh":
			_pending_trade.clear()
			gameplay_view.set_command_busy(false)
			gameplay_view.show_notification("Trade completed, but the port inventory refresh timed out.")
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
	elif command == "move.warp":
		if result.get("kind") == "timeout":
			return # Timeout path already started an authoritative location refresh.
		_pending_game_commands.erase(request_id)
		gameplay_view.show_notification("Warp could not be confirmed. Reconnect to verify your location.")
		gameplay_view.set_action_pending(false)
	elif _pending_game_commands.has(request_id):
		var failed_operation: Dictionary = _pending_game_commands[request_id]
		_pending_game_commands.erase(request_id)
		if failed_operation.get("kind") in ["planet_info", "planet_colonists"]:
			_planet_pending_count = maxi(0, _planet_pending_count - 1)
			gameplay_view.show_notification("Planet record request failed · %s" % str(result.get("message", "request failed")))
			if _planet_pending_count == 0 and not _command_refresh_pending:
				gameplay_view.set_command_busy(false)
			return
		gameplay_view.set_command_busy(false)
		if failed_operation.get("kind") == "port_refresh":
			_pending_trade.clear()
			gameplay_view.show_notification("Trade completed, but the port inventory could not be refreshed.")
			return
		if failed_operation.get("kind") == "trade_quote":
			_pending_trade.clear()
			gameplay_view.show_notification("Trade quote failed. No trade was made.")
			return
		if str(failed_operation.get("command", "")) in ["trade.buy", "trade.sell"]:
			_pending_trade.clear()
		gameplay_view.show_notification("%s failed · %s" % [str(failed_operation.get("label", command)), str(result.get("message", "request failed"))])

func _on_refresh_failed(snapshot: Dictionary) -> void:
	var freshness: Dictionary = snapshot.get("freshness", {})
	gameplay_view.show_notification("State is %s. Some sector information may be unavailable." % str(freshness.get("overall", "partially available")))
	if _warp_refresh_pending:
		_warp_refresh_pending = false
		gameplay_view.set_action_pending(false)
	if _command_refresh_pending:
		_command_refresh_pending = false
		if _pending_trade.get("refresh_port_after") == true:
			_pending_trade.erase("refresh_port_after")
			_request_port_refresh()
		else:
			gameplay_view.set_command_busy(false)

func _on_refresh_finished(_snapshot: Dictionary) -> void:
	if _warp_refresh_pending:
		_warp_refresh_pending = false
		gameplay_view.set_action_pending(false)
	if _command_refresh_pending:
		_command_refresh_pending = false
		if _pending_trade.get("refresh_port_after") == true:
			_pending_trade.erase("refresh_port_after")
			_request_port_refresh()
		else:
			gameplay_view.set_command_busy(false)

func _on_warp_requested(destination: int) -> void:
	if not auth_session.is_authenticated() or _pending_game_commands.size() > 0 or _warp_refresh_pending:
		return
	gameplay_view.set_action_pending(true)
	var request_id: String = transport.request("move.warp", {"to_sector_id": destination}, auth_session.session_token)
	if request_id.is_empty():
		gameplay_view.set_action_pending(false)
		gameplay_view.show_notification("Warp was not sent. Check the connection and try again.")
		return
	_pending_game_commands[request_id] = {"kind": "warp", "destination": destination}
	gameplay_view.show_notification("Warp request sent · waiting for server confirmation.")

func _finish_warp_reply(response: Dictionary, destination: int) -> void:
	var result: Dictionary = Protocol.result_from_response(response)
	if result.get("kind") == "ok":
		gameplay_view.show_notification("Warp confirmed · Sector %d." % destination)
		await gameplay_view.play_warp_transition()
	else:
		gameplay_view.show_notification("Warp refused · %s" % str(result.get("message", "The server did not move your ship.")))
	_refresh_after_warp_attempt()

func _refresh_after_warp_attempt() -> void:
	_warp_refresh_pending = true
	if not refresh_coordinator.refresh(true):
		_warp_refresh_pending = false
		gameplay_view.set_action_pending(false)
		gameplay_view.show_notification("Could not refresh your location. The displayed sector may be stale.")

func _on_command_requested(command: String, data: Dictionary, label: String, mutating: bool) -> void:
	var is_planet_read := _landed_planet_id > 0 and command in ["planet.info", "planet.colonists.get"]
	if not auth_session.is_authenticated() or (not is_planet_read and _pending_game_commands.size() > 0) or not _pending_trade.is_empty() or _warp_refresh_pending or (_command_refresh_pending and not is_planet_read):
		gameplay_view.show_notification("A command is already pending, or the connection is unavailable.")
		return
	var request_id: String = transport.request(command, data, auth_session.session_token)
	if request_id.is_empty():
		gameplay_view.show_notification("%s was not sent. Check the connection and try again." % label)
		return
	var kind := "command"
	if is_planet_read:
		kind = "planet_info" if command == "planet.info" else "planet_colonists"
		_planet_pending_count += 1
	_pending_game_commands[request_id] = {"kind": kind, "command": command, "label": label, "data": data.duplicate(true), "mutating": mutating}
	gameplay_view.set_command_busy(true)
	gameplay_view.show_notification("%s · waiting for server confirmation." % label)

func _finish_command_reply(response: Dictionary, operation: Dictionary) -> void:
	var result: Dictionary = Protocol.result_from_response(response)
	var label := str(operation.get("label", operation.get("command", "Command")))
	if result.get("kind") == "ok":
		var data: Dictionary = result.get("data", {})
		var summary := _result_summary(data)
		var command_name := str(operation.get("command", ""))
		if command_name == "planet.land":
			var planet_id := int(data.get("planet_id", 0))
			if planet_id > 0:
				_landed_planet_id = planet_id
				await gameplay_view.play_warp_transition()
				gameplay_view.enter_planet_workflow(planet_id)
				gameplay_view.show_notification("Landing confirmed · planet surface opened.")
				_request_planet_surface_records()
				_refresh_after_command(["player", "ship"])
			else:
				gameplay_view.set_command_busy(false)
				gameplay_view.show_notification("Landing succeeded, but the response omitted the planet ID.")
			return
		if command_name == "planet.colonists.set":
			gameplay_view.show_notification("Colonist transfer confirmed.%s" % (" · " + summary if not summary.is_empty() else ""))
			_request_planet_surface_records()
			_refresh_after_command(["player", "ship"])
			return
		if command_name == "planet.launch":
			await gameplay_view.play_warp_transition()
			_landed_planet_id = 0
			_planet_pending_count = 0
			gameplay_view.leave_planet_workflow()
			gameplay_view.show_notification("Launch confirmed · returning to sector view.")
			_refresh_after_command()
			return
		gameplay_view.show_notification("%s complete.%s" % [label, " · " + summary if not summary.is_empty() else ""])
		if str(operation.get("command", "")) == "port.info" and data.has("port"):
			gameplay_view.enter_port_workflow(data)
		elif str(operation.get("command", "")) == "shipyard.list":
			gameplay_view.show_shipyard(data)
		elif str(operation.get("command", "")) in ["deploy.fighters.list", "deploy.mines.list"]:
			var asset_kind := "fighters" if str(operation.get("command", "")) == "deploy.fighters.list" else "mines"
			gameplay_view.show_deployed_assets(data, asset_kind, int(operation.get("data", {}).get("sector_id", 0)))
		else:
			gameplay_view.show_command_result(label, data)
		if bool(operation.get("mutating", false)):
			if str(operation.get("command", "")) in ["trade.buy", "trade.sell"]:
				_pending_trade["refresh_port_after"] = true
			_refresh_after_command()
		else:
			gameplay_view.set_command_busy(false)
	else:
		gameplay_view.set_command_busy(false)
		if str(operation.get("command", "")) in ["trade.buy", "trade.sell"]:
			_pending_trade.clear()
		gameplay_view.show_notification("%s refused · %s" % [label, str(result.get("message", "The server did not accept the command."))])

func _refresh_after_command(domains: Array = []) -> void:
	_command_refresh_pending = true
	if not refresh_coordinator.refresh(true, domains):
		_command_refresh_pending = false
		gameplay_view.set_command_busy(false)
		if _pending_trade.get("refresh_port_after") == true:
			_pending_trade.erase("refresh_port_after")
			_request_port_refresh()
		gameplay_view.show_notification("Command completed, but the refreshed state could not be confirmed.")

func _request_planet_surface_records() -> void:
	if _landed_planet_id <= 0:
		return
	gameplay_view.set_command_busy(true)
	gameplay_view.planet_workflow.refresh_details()

func _result_summary(data: Dictionary) -> String:
	for key in ["message", "status", "name", "sector_id", "amount", "balance"]:
		if data.has(key) and not str(data[key]).is_empty():
			return "%s: %s" % [key.replace("_", " ").capitalize(), str(data[key])]
	return ""

func _on_trade_requested(direction: String, port_id: int, sector_id: int, commodity: String, quantity: int) -> void:
	if not auth_session.is_authenticated() or not _pending_trade.is_empty() or _pending_game_commands.size() > 0 or _command_refresh_pending:
		gameplay_view.show_notification("Another server command is pending. Wait for it to finish before trading.")
		return
	_pending_trade = {"direction": direction, "port_id": port_id, "sector_id": sector_id, "commodity": commodity, "quantity": quantity}
	gameplay_view.set_command_busy(true)
	var request_id: String = transport.request("trade.quote", {"port_id": port_id, "commodity": commodity, "quantity": quantity}, auth_session.session_token)
	if request_id.is_empty():
		_pending_trade.clear()
		gameplay_view.set_command_busy(false)
		gameplay_view.show_notification("Quote request could not be sent.")
		return
	_pending_game_commands[request_id] = {"kind": "trade_quote", "label": "Trade quote"}
	gameplay_view.show_notification("Requesting the server's current trade quote…")

func _finish_trade_quote(response: Dictionary) -> void:
	var result: Dictionary = Protocol.result_from_response(response)
	if result.get("kind") != "ok":
		_pending_trade.clear()
		gameplay_view.set_command_busy(false)
		gameplay_view.show_notification("Trade quote refused · %s" % str(result.get("message", "Quote unavailable.")))
		return
	var trade_data: Dictionary = _pending_trade.duplicate(true)
	gameplay_view.set_command_busy(false)
	gameplay_view.confirm_trade_quote(str(trade_data["direction"]), str(trade_data["commodity"]), int(trade_data["quantity"]), result.get("data", {}))

func _on_trade_confirmation(accepted: bool) -> void:
	if _pending_trade.is_empty():
		return
	if not accepted:
		_pending_trade.clear()
		gameplay_view.show_notification("Trade cancelled. No changes were made.")
		return
	var trade_data: Dictionary = _pending_trade.duplicate(true)
	var direction := str(trade_data["direction"])
	var command := "trade.%s" % direction
	var payload := {
		"port_id": int(trade_data["port_id"]),
		"sector_id": int(trade_data["sector_id"]),
		"items": [{"commodity": str(trade_data["commodity"]), "quantity": int(trade_data["quantity"])}],
		"account": 0,
		"idempotency_key": "godot-%d-%d" % [Time.get_ticks_usec(), randi()],
	}
	var request_id: String = transport.request(command, payload, auth_session.session_token)
	if request_id.is_empty():
		_pending_trade.clear()
		gameplay_view.show_notification("Trade was not sent. Your quoted amount has not been charged.")
		return
	_pending_game_commands[request_id] = {"kind": "command", "command": command, "label": direction.capitalize(), "mutating": true}
	gameplay_view.set_command_busy(true)
	gameplay_view.show_notification("Trade submitted · waiting for server confirmation.")

func _request_port_refresh() -> void:
	if not auth_session.is_authenticated():
		_pending_trade.clear()
		return
	var request_id: String = transport.request("port.info", {}, auth_session.session_token)
	if request_id.is_empty():
		_pending_trade.clear()
		gameplay_view.set_command_busy(false)
		gameplay_view.show_notification("Trade completed; the port inventory could not be refreshed.")
		return
	_pending_game_commands[request_id] = {"kind": "port_refresh", "label": "Port refresh"}
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
	# system.notice is an always-on topic. These streams are opt-in under Protocol v3.
	for topic in ["sector.*", "combat.*", "trade.*"]:
		transport.request("subscribe.add", {"topic": topic}, auth_session.session_token)

func _present_server_event(event: Dictionary) -> void:
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

func _save_config() -> void:
	var config := ConfigFile.new()
	config.set_value("server", "ip", server_ip)
	config.set_value("server", "port", server_port)
	config.set_value("auth", "username", auth_username)
	config.save(CONFIG_PATH)

func log_network(kind: String, message: String) -> void:
	if network_log_file:
		var timestamp := Time.get_datetime_string_from_system(false, true)
		network_log_file.store_line("[%s] %s: %s" % [timestamp, kind, message])
		network_log_file.flush()
