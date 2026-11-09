"""V3-PLAN-01 preserves terminal landing-refusal state supplied by the server."""

import client


def test_landing_refusal_renders_terminal_ship_loss_without_combat_rows():
    lines = client._planet_landing_outcome_lines({
        "status": "error",
        "error": {"data": {"ship_remains_in_sector": False}},
    })

    assert lines == [
        "Authoritative planetary defense report:",
        "  The server reports that your ship did not remain in the sector.",
        "  Landing was refused; no planetary access was granted.",
    ]


def test_landing_refusal_does_not_invent_terminal_state():
    assert client._planet_landing_outcome_lines({
        "status": "error",
        "error": {"data": {"landing_allowed": False}},
    }) == []
