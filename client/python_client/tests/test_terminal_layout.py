"""Deterministic responsive terminal layout tests."""

import io

import client


def test_non_tty_uses_deterministic_fallback():
    assert client.terminal_width(io.StringIO(), fallback=73) == 73


def test_narrow_layout_stays_one_column_and_preserves_hotkeys():
    labels = ["(A) First option", "(B) A much longer option", "(C) Third option"]
    lines = client.layout_menu_options(labels, width=40)
    assert lines == ["  " + label for label in labels]
    assert all(label in "\n".join(lines) for label in labels)


def test_standard_width_uses_two_columns_only_when_safe():
    labels = ["(A) Move", "(B) Dock", "(C) Comms", "(D) Settings"]
    lines = client.layout_menu_options(labels, width=80)
    assert len(lines) == 2
    assert all(len(line) <= 80 for line in lines)
    assert "(A) Move" in lines[0] and "(B) Dock" in lines[0]


def test_long_labels_force_one_column_even_on_wide_terminal():
    labels = ["(A) This is a deliberately long navigation label", "(B) Another long label"]
    lines = client.layout_menu_options(labels, width=80)
    assert len(lines) == 2
    assert all(label in line for label, line in zip(labels, lines))


def test_section_layout_keeps_sections_and_hidden_options_stable():
    menu = {"sections": [
        {"name": "Core", "options": [{"label": "(A) Alpha"}, {"label": "(B) Beta"}]},
        {"name": "Other", "options": [{"label": "(C) Gamma"}]},
    ]}
    lines = client.layout_menu_lines(menu, [["(A) Alpha"], ["(C) Gamma"]], width=60)
    assert lines == ["[Core]", "  (A) Alpha", "[Other]", "  (C) Gamma"]
