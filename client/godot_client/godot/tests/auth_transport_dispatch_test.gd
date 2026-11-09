extends SceneTree

const Transport = preload("res://ProtocolTransport.gd")

var failures: Array[String] = []
var server := TCPServer.new()
var server_peer: StreamPeerTCP
var transport = Transport.new(5.0, 10, 2.0)
var received := ""
var connected := false
var replied := false
var dispatched := false

func _init() -> void:
	transport.transport_connected.connect(func() -> void:
		connected = true
		var request_id: String = transport.request("auth.login", {"username": "newguy", "passwd": "test-only"})
		_check(not request_id.is_empty(), "transport did not register the auth.login request")
	)
	transport.reply_received.connect(func(request_id: String, response: Dictionary, command: String) -> void:
		dispatched = command == "auth.login" and not request_id.is_empty() and str(response.get("type", "")) == "auth.session"
	)
	call_deferred("_run")

func _run() -> void:
	var listen_error: Error = server.listen(18765, "127.0.0.1")
	_check(listen_error == OK, "loopback test server did not listen")
	if listen_error != OK:
		_finish()
		return
	var connect_error: Error = transport.connect_to_server("127.0.0.1", 18765)
	_check(connect_error == OK, "transport did not start loopback connection")
	for _i in range(120):
		transport.poll()
		if server.is_connection_available():
			server_peer = server.take_connection()
		if server_peer:
			server_peer.poll()
			if server_peer.get_available_bytes() > 0:
				received += server_peer.get_utf8_string(server_peer.get_available_bytes())
				if "\n" in received and not replied:
					var frame: Dictionary = JSON.parse_string(received.split("\n", false)[0])
					_check(frame.get("command", "") == "auth.login", "loopback server did not receive auth.login")
					_check(frame.get("data", {}).has("passwd"), "auth.login request omitted passwd")
					_check(not frame.get("data", {}).has("password"), "auth.login request used password instead of passwd")
					server_peer.put_data((JSON.stringify({"reply_to": frame.get("id", ""), "status": "ok", "type": "auth.session", "data": {"session": "test-token", "player_id": 1}}) + "\n").to_utf8_buffer())
					replied = true
		await process_frame
	_check(connected, "transport_connected was not emitted")
	_check(replied, "loopback server did not receive and answer auth.login")
	_check(dispatched, "auth.login reply was not dispatched to the client callback")
	_finish()

func _check(condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)

func _finish() -> void:
	transport.close("test complete")
	server.stop()
	if failures.is_empty():
		print("Godot auth transport dispatch test: PASS")
		quit(0)
	else:
		for failure in failures:
			push_error(failure)
		print("Godot auth transport dispatch test: FAIL")
		quit(1)
