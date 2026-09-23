extends Control

const Protocol = preload("res://Protocol.gd")
const ProtocolTransport = preload("res://ProtocolTransport.gd")
const AuthSession = preload("res://AuthSession.gd")

# Config File Path
const CONFIG_PATH = "user://client_config.cfg"

# UI Nodes
@onready var output_log: RichTextLabel = $VBoxContainer/HBoxContainer/MainView/OutputLog
@onready var command_input: LineEdit = $VBoxContainer/CommandInput
@onready var suggestion_popup: PopupMenu = $SuggestionPopup
@onready var left_panel_label: RichTextLabel = $VBoxContainer/HBoxContainer/LeftPanel/InfoLabel
@onready var right_panel_label: RichTextLabel = $VBoxContainer/HBoxContainer/RightPanel/InfoLabel
@onready var action_deck: HFlowContainer = $VBoxContainer/ActionDeck
@onready var connection_banner: Label = $VBoxContainer/NewsPanel/NewsLabel

# Network & State
var transport
var auth_session
var is_connected_flag: bool = false
var session_token: String = ""
var player_id: int = 0

# Configuration Data
var server_ip: String = ""
var server_port: int = 0
var auth_username: String = ""
var auth_token: String = "" # Stored persistent token if any
var network_log_file: FileAccess
var schema_cache: Dictionary = {}
var pending_schemas: Array = []
var player_cache: Dictionary = {}
var ship_cache: Dictionary = {}

# Application State Machine
enum AppState { INIT, PROMPT_SERVER_IP, PROMPT_SERVER_PORT, PROMPT_USERNAME, PROMPT_PASSWORD, CONNECTING, HARVESTING_SCHEMAS, AUTHENTICATING, LOGGED_IN }
var current_state: AppState = AppState.INIT
enum ConnectionState { DISCONNECTED, CONNECTING, CONNECTED, AUTHENTICATING, AUTHENTICATED, FAILED }
var connection_state: ConnectionState = ConnectionState.DISCONNECTED

# Command Logic
var command_tree = {}
var current_menu_path: Array = []
var command_list: Array = [
    "quit", "connect", "auth.logout",
    "move.warp", "move.scan", "move.pathfind",
    "port.dock", "port.info",
    "trade.buy", "trade.sell",
    "sector.info", "sector.scan", "sector.search",
    "player.my_info", "player.list_online", "player.settings",
    "system.capabilities"
]

func _ready():
    transport = ProtocolTransport.new(15.0, 200)
    auth_session = AuthSession.new()
    transport.transport_connected.connect(_on_transport_connected)
    transport.transport_disconnected.connect(_on_transport_disconnected)
    transport.reply_received.connect(_on_transport_reply)
    transport.request_timed_out.connect(_on_request_timed_out)
    transport.request_failed.connect(_on_request_failed)
    transport.protocol_error.connect(_on_protocol_error)
    transport.protocol_warning.connect(_on_protocol_warning)
    transport.events_available.connect(_on_events_available)
    transport.diagnostic.connect(_on_transport_diagnostic)
    auth_session.authentication_failed.connect(_on_authentication_failed)
    auth_session.session_invalidated.connect(_on_session_invalidated)

    network_log_file = FileAccess.open("user://network_debug.log", FileAccess.WRITE)
    if network_log_file:
        log_network("CLIENT_START", "Logging started.")
    else:
        print("Error opening log file: ", FileAccess.get_open_error())

    command_input.text_submitted.connect(_on_command_input_text_submitted)
    command_input.text_changed.connect(_on_command_input_text_changed)
    suggestion_popup.index_pressed.connect(_on_suggestion_index_pressed)
    
    _build_command_tree(command_list)
    
    _load_config()
    _update_connection_banner("Disconnected")
    _start_state_machine()

func _exit_tree():
    if transport:
        transport.close("Client closed.")
    if network_log_file:
        log_network("CLIENT_STOP", "Logging stopped.")
        network_log_file.close()

func _start_state_machine():
    # If we have valid config, jump to connecting
    if server_ip != "" and server_port != 0 and auth_username != "":
        log_message("System: Found config for %s@%s:%d" % [auth_username, server_ip, server_port])
        if auth_token != "":
            log_message("System: Found saved session token.")
        
        current_state = AppState.CONNECTING
        _connect_to_server()
    else:
        log_message("System: No configuration found.")
        _prompt_server_ip()

func _prompt_server_ip():
    current_state = AppState.PROMPT_SERVER_IP
    log_message("Setup: Please enter Server IP (default: 192.168.25.170):")

func _prompt_server_port():
    current_state = AppState.PROMPT_SERVER_PORT
    log_message("Setup: Please enter Server Port (default: 1234):")

func _prompt_username():
    current_state = AppState.PROMPT_USERNAME
    log_message("Setup: Please enter Username:")

func _prompt_password():
    current_state = AppState.PROMPT_PASSWORD
    log_message("Setup: Please enter Password:")

# --- Configuration Persistence ---

func _load_config():
    var config = ConfigFile.new()
    var err = config.load(CONFIG_PATH)
    if err == OK:
        server_ip = config.get_value("server", "ip", "")
        server_port = config.get_value("server", "port", 0)
        auth_username = config.get_value("auth", "username", "")
        auth_token = config.get_value("auth", "token", "") # Optional: if we support token reuse
    else:
        pass # Start fresh

func _save_config():
    var config = ConfigFile.new()
    config.set_value("server", "ip", server_ip)
    config.set_value("server", "port", server_port)
    config.set_value("auth", "username", auth_username)
    if session_token != "":
        config.set_value("auth", "token", session_token)
    config.save(CONFIG_PATH)

# --- Network Logic ---

func _connect_to_server():
    current_state = AppState.CONNECTING
    connection_state = ConnectionState.CONNECTING
    _update_connection_banner("Connecting...")
    log_message("System: Connecting to %s:%d..." % [server_ip, server_port])
    var err: Error = transport.connect_to_server(server_ip, server_port)
    if err != OK:
        log_message("System: Unable to start the connection.")
        is_connected_flag = false
        connection_state = ConnectionState.FAILED
        current_state = AppState.INIT
        _update_connection_banner("Connection failed")


func _process(delta):
    transport.poll(delta)
    _drain_events()

func _on_transport_connected():
    is_connected_flag = true
    connection_state = ConnectionState.CONNECTED
    log_message("System: Connected.")
    _update_connection_banner("Connected — authenticating is required")
    if not meta_temp_password.is_empty():
        current_state = AppState.AUTHENTICATING
        _perform_login_with_password()
        return
    send_request("system.cmd_list", {})

func _on_transport_disconnected(reason: String):
    is_connected_flag = false
    connection_state = ConnectionState.DISCONNECTED
    auth_session.invalidate("The connection was lost.")
    session_token = ""
    if current_state != AppState.INIT:
        current_state = AppState.INIT
    log_message("System: Disconnected. %s" % reason)
    _update_connection_banner("Disconnected")

func _on_transport_reply(_request_id: String, response: Dictionary, command: String):
    _process_response(response, command)

func _on_request_timed_out(_request_id: String, command: String):
    log_message("System: Request timed out: %s." % command)
    if command == "auth.login":
        auth_session.fail_request("Login timed out.")

func _on_request_failed(_request_id: String, command: String, result: Dictionary):
    if result.get("kind", "") == "timeout":
        return
    if command == "auth.login":
        auth_session.fail_request(str(result.get("message", "Login failed.")))

func _on_protocol_error(message: String):
    log_message("System: The server sent an invalid response.")
    log_network("PROTOCOL_ERROR", message)

func _on_protocol_warning(message: String):
    log_network("PROTOCOL_WARNING", message)

func _on_authentication_failed(message: String):
    connection_state = ConnectionState.CONNECTED if is_connected_flag else ConnectionState.DISCONNECTED
    current_state = AppState.PROMPT_PASSWORD
    log_message("Login refused: %s" % message)
    _update_connection_banner("Connected — login required")

func _on_session_invalidated(reason: String):
    log_message("System: Session ended. %s" % reason)

func _on_transport_diagnostic(kind: String, payload: String):
    log_network(kind, payload)

func _on_events_available():
    # Events are drained separately from correlated replies. Their player-facing
    # presentation remains compact until the communications slice adds a view.
    pass

func _drain_events():
    for event in transport.drain_events():
        var event_type := str(event.get("type", "unknown"))
        log_message("Event received: %s" % event_type)

func _process_response(msg: Dictionary, request_command: String = ""):
    if str(msg.get("type", "")) == "auth.session" or request_command == "auth.login":
        auth_session.handle_response(msg)
    var result: Dictionary = Protocol.result_from_response(msg)
    if result["kind"] != "ok":
        log_message("Server refused the request: %s" % result["message"])
        return

    var type := str(msg.get("type", ""))
    var data = msg.get("data", {})
    if not (data is Dictionary):
        data = {}

    if type == "auth.session":
        session_token = auth_session.session_token
        player_id = auth_session.player_id
        connection_state = ConnectionState.AUTHENTICATED
        _update_connection_banner("Authenticated")
        current_state = AppState.LOGGED_IN
        log_message("System: Login successful. Session stored.")
        _save_config()
        send_request("player.my_info", {})
        send_request("sector.info", {})
    elif type == "move.result":
        log_message("Move accepted by the server.")
        send_request("player.my_info", {})
        send_request("sector.info", {})
    elif type == "player.info" or msg.get("command") == "player.my_info":
        _update_left_panel(data)
        var p = data.get("player", {})
        if p is Dictionary and p.has("ship_id"):
            send_request("ship.info", {"ship_id": int(p["ship_id"])})
    elif type == "ship.info" or type == "ship.status":
        _update_left_panel({"active_ship": data.get("ship", data)})
    elif type == "sector.info":
        _update_right_panel(data)
    elif type == "system.cmd_list":
        var commands = data.get("commands", [])
        var names := []
        for command in commands:
            if command is Dictionary and command.has("cmd"):
                names.append(str(command["cmd"]))
        log_message("System: Received %d commands. Harvesting schemas..." % names.size())
        _build_command_tree(names)
        pending_schemas = names.duplicate()
        current_state = AppState.HARVESTING_SCHEMAS
        _harvest_next_schema()
    elif type == "system.schema" or msg.get("command") == "system.describe_schema":
        var schema_name := str(data.get("name", ""))
        if not schema_name.is_empty():
            schema_cache[schema_name] = data.get("schema", {})
            pending_schemas.erase(schema_name)
            log_network("SCHEMA", "Harvested %s; %d remaining" % [schema_name, pending_schemas.size()])
            if current_state == AppState.HARVESTING_SCHEMAS:
                if schema_cache.has("auth.login") and schema_cache.has("auth.refresh"):
                    _perform_login()
                elif pending_schemas.is_empty():
                    _perform_login()
                else:
                    _harvest_next_schema()
            else:
                _harvest_next_schema()
    elif not type.is_empty():
        log_message("Server accepted: %s" % type)
        
# --- UI Updates ---

func _update_left_panel(data: Dictionary):
    # Update Cache
    if data.has("player"):
        player_cache = data.get("player")
    if data.has("active_ship"):
        ship_cache = data.get("active_ship")
        
    var text = "[center][b]Player & Ship[/b][/center]\n"
    
    if not player_cache.is_empty():
        text += "[b]Name:[/b] %s\n" % player_cache.get("username", "Unknown")
        text += "[b]Credits:[/b] %s\n" % player_cache.get("credits", "0")
        text += "[b]Corp:[/b] %s\n" % str(player_cache.get("corp_id", "None"))
        text += "[b]Turns:[/b] %s\n" % str(int(player_cache.get("turns_remaining", 0)))
    
    text += "\n[center][b]Active Ship[/b][/center]\n"
    
    if not ship_cache.is_empty():
        text += "[b]Name:[/b] %s\n" % ship_cache.get("name", "Unnamed")
        
        # Type handling
        var stype = ship_cache.get("type", "Unknown")
        if stype is Dictionary:
            stype = stype.get("name", "Unknown")
        text += "[b]Type:[/b] %s\n" % stype
        
        text += "[b]Hull:[/b] %s/%s\n" % [str(int(ship_cache.get("hp",0))), str(int(ship_cache.get("max_hp",0)))]
        
        # Holds/Cargo handling
        var holds = ship_cache.get("holds", ship_cache.get("cargo_max", 0))
        var cargo_used = 0
        var cargo = ship_cache.get("cargo")
        if cargo is Dictionary:
            for val in cargo.values():
                cargo_used += int(val)
        else:
            cargo_used = int(ship_cache.get("cargo_used", 0))
            
        text += "[b]Cargo:[/b] %s/%s\n" % [str(cargo_used), str(int(holds))]
    else:
        text += "No ship data."
        
    left_panel_label.text = text

func _update_right_panel(data: Dictionary):
    var text = "[center][b]Sector Info[/b][/center]\n"
    
    text += "[b]ID:[/b] %d\n" % int(data.get("sector_id", 0))
    
    var sname = data.get("name", "Unknown").replace("System Volume", "").strip_edges()
    text += "[b]Name:[/b] %s\n" % sname
    
    # Beacon
    var beacon = data.get("beacon")
    if beacon:
        text += "\n[color=yellow][b]Beacon:[/b] %s[/color]\n" % beacon
    
    var adj = data.get("adjacent", [])
    if adj.size() > 0:
        var adj_ints = []
        for a in adj:
            adj_ints.append(str(int(a)))
        text += "\n[b]Warps:[/b] " + ", ".join(adj_ints) + "\n"

    var ports = data.get("ports", [])
    if ports.size() > 0:
        text += "\n[b]Ports:[/b]\n"
        for port in ports:
            text += "- %s (%s)\n" % [port.get("name"), str(int(port.get("type", 0)))]
    
    var planets = data.get("planets", [])
    if planets.size() > 0:
        text += "\n[b]Planets:[/b]\n"
        for obj in planets:
            text += "- %s\n" % obj.get("name", "Unknown")

    var ships = data.get("ships", [])
    if ships.size() > 0:
        text += "\n[b]Ships:[/b]\n"
        for s in ships:
            var sname_obj = s.get("name", s.get("ship_name", "Unknown"))
            var owner = s.get("owner", "Unknown")
            text += "- %s (%s)\n" % [sname_obj, owner]
    
    right_panel_label.text = text

# --- Button Deck (Drill-Down UI) ---

func _update_action_deck():
    # Clear existing buttons
    for child in action_deck.get_children():
        child.queue_free()
    
    # 1. Find the current node in the command tree
    var current_node = command_tree
    for step in current_menu_path:
        if current_node.has(step):
            current_node = current_node[step]
    
    # 2. Add a "BACK" button if we are deep in a menu
    if not current_menu_path.is_empty():
        var btn_back = Button.new()
        btn_back.text = "< BACK"
        btn_back.add_theme_color_override("font_color", Color.ORANGE)
        btn_back.pressed.connect(_on_action_button_pressed.bind("..BACK.."))
        action_deck.add_child(btn_back)

    # 3. Create buttons for current options
    if current_node is Dictionary:
        var keys = current_node.keys()
        keys.sort() # Alphabetical order
        
        for key in keys:
            var btn = Button.new()
            btn.text = key.capitalize() # "my_info" -> "My Info"
            btn.pressed.connect(_on_action_button_pressed.bind(key))
            action_deck.add_child(btn)

func _on_action_button_pressed(key: String):
    # Handle "Back"
    if key == "..BACK..":
        if not current_menu_path.is_empty():
            current_menu_path.pop_back()
            _update_action_deck()
        return

    # Determine if this is a Category or a Final Command
    var current_node = command_tree
    for step in current_menu_path:
        current_node = current_node[step]
    
    var next_node = current_node[key]
    
    # If it's a Dictionary, it has sub-commands (Drill down!)
    if next_node is Dictionary and not next_node.is_empty():
        current_menu_path.append(key)
        _update_action_deck()
    else:
        # It's a final command! 
        _execute_or_prefill_command(key)

func _execute_or_prefill_command(final_key: String):
    # Construct the full command string
    var full_path = current_menu_path.duplicate()
    full_path.append(final_key)
    var cmd_str = ".".join(full_path)
    
    # 1. Determine if command is complex dynamically
    var is_complex = false
    
    # SAFE LOOKUP: Use .get() to avoid crashing if the schema isn't downloaded yet
    var schema = schema_cache.get(cmd_str, {})
    
    if schema.is_empty():
        # Schema missing? Default to true (complex) to prevent accidental execution
        # This handles the "I clicked before the download finished" case.
        is_complex = true 
        log_message("System: Schema for '%s' not yet loaded. Defaulting to input mode." % cmd_str)
    else:
        # Schema found! Check if it has required fields
        if schema.has("required") and schema["required"].size() > 0:
            is_complex = true

    # 2. Execute or Prefill
    if is_complex:
        var ui_path = "/".join(full_path)
        command_input.text = "/" + ui_path + "/"
        command_input.caret_column = command_input.text.length()
        command_input.grab_focus()
        
        # Optional hint
        if not schema.is_empty():
            var reqs = schema.get("required", [])
            log_message("Hint: %s requires %s" % [cmd_str, str(reqs)])
    else:
        _handle_normal_command(cmd_str)



# --- Input Handling & State Machine Intercept ---

func _on_command_input_text_submitted(text: String):
    if text.strip_edges() == "": 
        command_input.call_deferred("grab_focus")
        return
    
    suggestion_popup.hide()
    command_input.clear()
    
    # Intercept for Setup Phase
    match current_state:
        AppState.PROMPT_SERVER_IP:
            if text != "": server_ip = text
            else: server_ip = "192.168.25.170"
            _prompt_server_port()
            command_input.call_deferred("grab_focus")
            return
        AppState.PROMPT_SERVER_PORT:
            if text != "": server_port = int(text)
            else: server_port = 1234
            _prompt_username()
            command_input.call_deferred("grab_focus")
            return
        AppState.PROMPT_USERNAME:
            auth_username = text
            _prompt_password()
            command_input.call_deferred("grab_focus")
            return
        
        AppState.PROMPT_PASSWORD:
            var password = text
            
            # FIX: Only connect if we aren't already connected!
            if not transport.is_transport_connected():
                _connect_to_server()
                meta_temp_password = password # Save it for when we connect
            else:
                # We are already connected, so just login immediately
                var login_data: Dictionary = auth_session.begin_login(auth_username, password)
                if not login_data.is_empty():
                    connection_state = ConnectionState.AUTHENTICATING
                    send_request("auth.login", login_data)
            
            command_input.call_deferred("grab_focus")
            return


    log_message("You: " + text)
    _handle_normal_command(text)
    command_input.call_deferred("grab_focus")

var meta_temp_password = ""

func _perform_login_with_password():
    if meta_temp_password != "":
        var login_data: Dictionary = auth_session.begin_login(auth_username, meta_temp_password)
        if not login_data.is_empty():
            connection_state = ConnectionState.AUTHENTICATING
            send_request("auth.login", login_data)
        meta_temp_password = "" # Clear
    else:
        log_message("Error: Password missing.")

func _get_field_name(cmd: String, preferred: String, fallback: String) -> String:
    if schema_cache.has(cmd):
        var schema = schema_cache[cmd]
        var props = schema.get("properties", {})
        if props.has(preferred):
            return preferred
        if props.has(fallback):
            return fallback
    return preferred

func _perform_login():
    log_network("DEBUG", "Performing login sequence.")
    # Force password prompt every time as requested
    current_state = AppState.AUTHENTICATING
    
    if meta_temp_password != "":
        _perform_login_with_password()
    else:
        log_message("Auth: Password required for %s." % auth_username)
        current_state = AppState.PROMPT_PASSWORD


func _harvest_next_schema():
    if pending_schemas.is_empty():
        return
    var next_cmd = pending_schemas[0]
    send_request("system.describe_schema", {"name": next_cmd})

# --- Normal Command Handling ---


func _handle_normal_command(text: String):
    var parts = text.split("/")
    if text.begins_with("/"): parts.remove_at(0)

    # Filter out empty strings caused by trailing slashes
    var clean_parts = []
    for p in parts:
        if p.strip_edges() != "":
            clean_parts.append(p)
    parts = clean_parts

    if parts.size() > 0 and parts[0] == "quit":
        transport.close("Client quit.")
        get_tree().quit()
        return
    
    if parts.size() >= 3 and parts[0] == "connect":
        server_ip = parts[1]
        server_port = int(parts[2])
        _connect_to_server()
        return

    var command = ""
    var payload = {}
    
    # Basic Mapping
    if parts.size() >= 2:
        var cat = parts[0]
        var act = parts[1]
        command = cat + "." + act
        
        if command == "move.warp" and parts.size() >= 3:
            payload["to_sector_id"] = int(parts[2])
        elif command == "trade.buy" and parts.size() >= 4:
            payload["commodity"] = parts[2]
            payload["quantity"] = int(parts[3])
        elif command == "trade.sell" and parts.size() >= 4:
            payload["commodity"] = parts[2]
            payload["quantity"] = int(parts[3])
    
    elif parts.size() == 1:
        # Handle /player/my_info aliases if user typed partial
        pass

    if command != "":
        send_request(command, payload)
    else:
        log_message("System: Sending raw command...")


# --- Utils ---

func send_request(cmd_name: String, data: Dictionary):
    if not transport.is_transport_connected():
        log_message("System: Not connected.")
        return
    if not cmd_name.begins_with("system.") and cmd_name != "auth.login" and not auth_session.is_authenticated():
        log_message("System: Login is required for %s." % cmd_name)
        return
    transport.request(cmd_name, data, session_token)

func _update_connection_banner(status: String) -> void:
    if connection_banner:
        connection_banner.text = "Connection: %s" % status

func log_message(msg: String):
    output_log.text += msg + "\n"

func log_network(prefix: String, data: String):
    if network_log_file:
        var timestamp = Time.get_datetime_string_from_system(false, true)
        network_log_file.store_line("[%s] %s: %s" % [timestamp, prefix, data])
        network_log_file.flush()

# --- Autocomplete (Preserved) ---
func _build_command_tree(cmd_list: Array):
    command_tree = {}
    for cmd in cmd_list:
        var parts = cmd.split(".")
        var current = command_tree
        for i in range(parts.size()):
            var part = parts[i]
            if not current.has(part): current[part] = {}
            current = current[part]
    
    # UPDATE: Render buttons after tree is built
    _update_action_deck()

func _on_command_input_text_changed(new_text: String):
    if not new_text.begins_with("/"):
        suggestion_popup.hide()
        return
    var text_content = new_text.substr(1)
    var parts = text_content.split("/")
    var current_node = command_tree
    var valid_path = true
    for i in range(parts.size() - 1):
        var part = parts[i]
        if current_node is Dictionary and current_node.has(part): current_node = current_node[part]
        else:
            valid_path = false
            break
    if not valid_path or not (current_node is Dictionary):
        suggestion_popup.hide()
        return
    var current_input = parts[parts.size() - 1]
    var matches = []
    for key in current_node.keys():
        if key.begins_with(current_input): matches.append(key)
    if matches.size() > 0: _show_suggestions(matches)
    else: suggestion_popup.hide()

func _show_suggestions(suggestions: Array):
    suggestion_popup.clear()
    for s in suggestions: suggestion_popup.add_item(s)
    var input_pos = command_input.global_position
    suggestion_popup.position = Vector2i(input_pos.x, input_pos.y - suggestion_popup.get_contents_minimum_size().y)
    suggestion_popup.show() 

func _on_suggestion_index_pressed(index: int):
    var selected_text = suggestion_popup.get_item_text(index)
    _apply_autocomplete(selected_text)

func _apply_autocomplete(completion: String):
    var text = command_input.text
    var last_slash = text.rfind("/")
    if last_slash != -1:
        var prefix = text.substr(0, last_slash + 1)
        command_input.text = prefix + completion + "/" 
        command_input.caret_column = command_input.text.length()
        suggestion_popup.hide()
        _on_command_input_text_changed(command_input.text)

func _input(event):
    if event is InputEventKey and event.pressed:
        if event.keycode == KEY_TAB:
            if suggestion_popup.visible and suggestion_popup.item_count > 0:
                _apply_autocomplete(suggestion_popup.get_item_text(0))
                get_viewport().set_input_as_handled()
