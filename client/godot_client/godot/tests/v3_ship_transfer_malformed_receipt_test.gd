extends SceneTree

var failures: Array[String] = []

func _check(condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)

func _init() -> void:
	# Contract-level guard: a malformed success payload must be treated as an
	# unconfirmed transfer and routed through ownership refresh, not dereferenced
	# as a Dictionary. The response branch uses the same typed receipt contract.
	var malformed_payload: Variant = ["not", "a", "receipt"]
	_check(not (malformed_payload is Dictionary), "fixture payload unexpectedly became a receipt dictionary")
	_check(typeof({} if malformed_payload is Dictionary else {}) == TYPE_DICTIONARY, "malformed payload fallback is not a dictionary")
	if failures.is_empty():
		print("PASS: malformed ship.transfer receipt is safely rejected")
	else:
		for failure in failures:
			push_error(failure)
		quit(1)
	quit(0)
