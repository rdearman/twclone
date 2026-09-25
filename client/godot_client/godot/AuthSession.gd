class_name TwAuthSession
extends RefCounted

## Authentication/session state independent from transport and presentation.

const Protocol = preload("res://Protocol.gd")

signal authenticated(session_token: String, player_id: int)
signal authentication_failed(message: String)
signal session_invalidated(reason: String)

enum State { UNAUTHENTICATED, AUTHENTICATING, AUTHENTICATED }

var state: State = State.UNAUTHENTICATED
var session_token := ""
var player_id: int = 0
var _login_in_flight := false

func begin_login(username: String, password: String) -> Dictionary:
	if _login_in_flight:
		return {}
	if username.strip_edges().is_empty() or password.is_empty():
		authentication_failed.emit("Username and password are required.")
		return {}
	_login_in_flight = true
	state = State.AUTHENTICATING
	return {
		"username": username.strip_edges(),
		"passwd": password,
		"client": "GodotClient",
		"version": "1.0.0",
	}

func handle_response(response: Dictionary) -> bool:
	var status := Protocol.response_status(response)
	if status != "ok":
		if state == State.AUTHENTICATING:
			_login_in_flight = false
			state = State.UNAUTHENTICATED
			authentication_failed.emit(Protocol.response_message(response))
		return false
	if str(response.get("type", "")) != "auth.session":
		return false
	var data = response.get("data", {})
	if not (data is Dictionary):
		_login_in_flight = false
		state = State.UNAUTHENTICATED
		authentication_failed.emit("The server returned an invalid login response.")
		return false
	var token := str(data.get("session", data.get("session_token", "")))
	if token.is_empty():
		_login_in_flight = false
		state = State.UNAUTHENTICATED
		authentication_failed.emit("The server did not return a session token.")
		return false
	session_token = token
	player_id = int(data.get("player_id", 0))
	_login_in_flight = false
	state = State.AUTHENTICATED
	authenticated.emit(session_token, player_id)
	return true

func fail_request(message: String) -> void:
	if state == State.AUTHENTICATING:
		_login_in_flight = false
		state = State.UNAUTHENTICATED
		authentication_failed.emit(message)

func invalidate(reason: String = "The session ended.") -> void:
	var had_session := state != State.UNAUTHENTICATED or not session_token.is_empty()
	state = State.UNAUTHENTICATED
	_login_in_flight = false
	session_token = ""
	player_id = 0
	if had_session:
		session_invalidated.emit(reason)

func is_authenticated() -> bool:
	return state == State.AUTHENTICATED and not session_token.is_empty()

func login_in_flight() -> bool:
	return _login_in_flight
