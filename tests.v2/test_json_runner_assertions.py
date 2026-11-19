"""The JSON acceptance DSL must fail closed for missing or unsupported checks."""

from json_runner import JsonSuiteRunner


def runner():
    result = JsonSuiteRunner.__new__(JsonSuiteRunner)
    result.vars = {}
    return result


def test_assertion_path_distinguishes_missing_values_from_explicit_null():
    test_runner = runner()
    response = {"data": {"present_null": None, "values": [{"id": 7}]}}

    assert test_runner._resolve_assertion_path(response, "data.present_null") == (None, True)
    assert test_runner._resolve_assertion_path(response, "data.missing") == (None, False)
    assert test_runner._resolve_assertion_path(response, "data.values.4") == (None, False)


def test_saved_variable_paths_and_json_filter_paths_resolve():
    test_runner = runner()
    test_runner.vars["rows"] = [["Ferengi Alliance", 259]]
    response = {"data": {"topics": [{"pattern": "system.notice"}, {"pattern": "combat.*"}]}}

    assert test_runner._resolve_assertion_path(response, "rows.0.1") == (259, True)
    assert test_runner._resolve_assertion_path(
        response, "data.topics[?(@.pattern == 'system.notice')]"
    ) == ([{"pattern": "system.notice"}], True)


def test_supported_assertion_operators_are_evaluated():
    evaluate = JsonSuiteRunner._evaluate_assertion
    cases = [
        ("equals", 3, "3", True),
        (">", 4, 3, True),
        (">=", 3, 3, True),
        ("<", 2, 3, True),
        ("<=", 3, 3, True),
        ("greater_than", 4, 3, True),
        ("contains", ["ore", "org"], "ore", True),
        ("not_contains", ["ore", "org"], "shipyard", True),
        ("is_array", [], None, True),
        ("is_empty", [], None, True),
        ("len_gt", [1], 0, True),
        ("exists", None, None, True),
        ("exists", None, None, False),
        ("not_exists", None, None, False),
        ("not_exists", None, None, True),
    ]
    for op, actual, expected, found in cases:
        passed, _ = evaluate(op, actual, expected, found)
        assert passed is (found if op == "exists" else not found if op == "not_exists" else True)
    assert evaluate("not_contains", ["ore", "org"], "ore", True)[0] is False


def test_missing_paths_and_unknown_operators_fail_closed():
    evaluate = JsonSuiteRunner._evaluate_assertion
    assert evaluate("equals", None, None, False)[0] is False
    passed, detail = evaluate("not_a_real_operator", 1, 1, True)
    assert passed is False
    assert "unsupported assertion operator" in detail


def test_unique_and_sorted_key_assertions():
    evaluate = JsonSuiteRunner._evaluate_assertion
    ordered = [{"player_id": 7}, {"player_id": 12}]
    assert evaluate("unique_by", ordered, "player_id", True)[0] is True
    assert evaluate("sorted_by", ordered, "player_id", True)[0] is True
    assert evaluate("unique_by", [{"player_id": 7}, {"player_id": 7}], "player_id", True)[0] is False
    assert evaluate("sorted_by", [{"player_id": 12}, {"player_id": 7}], "player_id", True)[0] is False
    assert evaluate("unique_by", [{"username": "x"}], "player_id", True)[0] is False
