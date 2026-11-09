"""V3-PLAN-01 checks both authoritative landing response envelopes."""

import client


def test_landing_error_renders_top_level_authoritative_combat_data():
    lines = client._planet_landing_outcome_lines({
        "status": "error",
        "data": {
            "combat": [{
                "defense": "atmosphere_quasar",
                "damage": 9,
                "shields_lost": 2,
                "hull_lost": 7,
                "shields_remaining": 0,
                "hull_remaining": 93,
            }],
            "ship_remains_in_sector": True,
        },
        "error": {"message": "Landing refused."},
    })

    assert "Atmospheric Quasar: 9 damage" in lines[1]
    assert "  Landing was refused; no planetary access was granted." == lines[-1]


def test_planet_attack_refusal_uses_attack_context_for_authoritative_report():
    lines = client._planet_landing_outcome_lines(
        {
            "status": "refused",
            "error": {
                "data": {
                    "combat": [{
                        "result": "repelled",
                        "attacker_fighters_lost": 4,
                        "planet_fighters_lost": 2,
                    }],
                    "ship_remains_in_sector": True,
                }
            },
        },
        "Planet attack was refused; no planetary attack was confirmed.",
    )
    assert lines[-1] == "  Planet attack was refused; no planetary attack was confirmed."
    assert "Landing was refused" not in "\n".join(lines)
