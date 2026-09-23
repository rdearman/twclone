"""
Test 8: existing CLI arguments and --help continue to work, and --debug is
the only addition to the interface.
"""
import subprocess
import sys
import os

CLIENT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CLIENT_PY = os.path.join(CLIENT_DIR, "client.py")


def _run_help():
    return subprocess.run(
        [sys.executable, CLIENT_PY, "--help"],
        capture_output=True, text=True, timeout=10,
    )


def test_help_exits_zero_and_lists_existing_and_new_flags():
    result = _run_help()
    assert result.returncode == 0
    out = result.stdout
    for flag in ("--host", "--port", "--user", "--passwd", "--debug", "--menus"):
        assert flag in out, f"{flag} missing from --help output"


def test_debug_flag_is_a_plain_boolean_switch():
    result = _run_help()
    # --debug must remain a store_true flag (no required argument), i.e. it
    # appears in the usage line as a bare "[--debug]" token.
    assert "[--debug]" in result.stdout
