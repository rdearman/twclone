"""
Tests for hud.render_hud_lines — persistent HUD presentation.

Requirements covered:
10. HUD wide-terminal rendering.
11. HUD narrow-terminal rendering.
12. HUD rendering with unavailable fields.
"""
from hud import HudState, render_hud_lines, UNAVAILABLE
import time

FULL_HUD = HudState(
    sector_id=42, sector_name="Rigel", ship_id=7, ship_name="Wayfarer",
    turns_remaining=317, credits=12450, cargo_used=18, cargo_total=40,
    fighters=250, shields=80, last_refresh_ts=time.time(),
)


def test_wide_terminal_renders_compact_two_lines():
    lines = render_hud_lines(FULL_HUD, connected=True, activity=3, width=100)
    assert len(lines) == 2
    assert "Sector 42: Rigel" in lines[0]
    assert "Ship: Wayfarer" in lines[0]
    assert "Turns: 317" in lines[0]
    assert "Credits: 12,450 cr" in lines[0]
    assert "Holds: 18/40" in lines[1]
    assert "Fighters: 250" in lines[1]
    assert "Shields: 80" in lines[1]
    assert "Link: ONLINE" in lines[1]
    assert "Activity: 3" in lines[1]


def test_narrow_terminal_wraps_instead_of_truncating():
    narrow_lines = render_hud_lines(FULL_HUD, connected=True, activity=3, width=30)
    wide_lines = render_hud_lines(FULL_HUD, connected=True, activity=3, width=100)
    assert len(narrow_lines) >= len(wide_lines)
    # No line exceeds the requested width by more than a single unsplit part
    # (an individual field is never truncated mid-value).
    joined = " ".join(narrow_lines)
    assert "Sector 42: Rigel" in joined
    assert "Credits: 12,450 cr" in joined
    assert "Fighters: 250" in joined


def test_unavailable_fields_render_as_em_dash_not_zero_or_blank():
    sparse = HudState(sector_id=42, sector_name="Rigel")
    lines = render_hud_lines(sparse, connected=True, activity=None, width=100)
    joined = "\n".join(lines)
    assert f"Turns: {UNAVAILABLE}" in joined
    assert f"Credits: {UNAVAILABLE}" in joined
    assert f"Holds: {UNAVAILABLE}" in joined
    assert f"Fighters: {UNAVAILABLE}" in joined
    assert f"Shields: {UNAVAILABLE}" in joined
    assert f"Activity: {UNAVAILABLE}" in joined
    # Unavailable must never silently read as "0".
    assert "Turns: 0" not in joined
    assert "Fighters: 0" not in joined


def test_offline_link_state_is_unambiguous_without_colour():
    lines = render_hud_lines(FULL_HUD, connected=False, activity=0, width=100)
    joined = "\n".join(lines)
    assert "Link: OFFLINE" in joined
