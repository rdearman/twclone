import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def test_feedback_commands_are_published_and_authenticated_handlers_exist():
    commands = json.loads((ROOT / "published_commands.json").read_text())
    names = {item.get("cmd") for item in commands.get("data", {}).get("commands", []) if isinstance(item, dict)}
    assert {"feedback.preview", "feedback.submit", "feedback.status", "feedback.diagnostics"} <= names
    server = (ROOT / "src/server_feedback.c").read_text()
    for handler in ("cmd_feedback_preview", "cmd_feedback_submit", "cmd_feedback_status", "cmd_feedback_diagnostics"):
        assert handler in server
    loop = (ROOT / "src/server_loop.c").read_text()
    assert '"feedback.submit", cmd_feedback_submit' in loop
    assert '"feedback.status", cmd_feedback_status' in loop


def test_postgres_outbox_has_idempotency_owner_and_exact_preview_constraints():
    migration = (ROOT / "sql/pg/188_v3_player_feedback_outbox.sql").read_text()
    for required in ("player_feedback_reports", "UNIQUE (player_id, idempotency_key)",
                     "public_issue_title", "public_issue_body", "publication_state",
                     "github_issue_number", "next_retry_at", "REFERENCES players(player_id)"):
        assert required in migration
    manifest = json.loads((ROOT / "sql/pg/migration_manifest.json").read_text())
    assert any(item.get("name") == "188_v3_player_feedback_outbox.sql" for item in manifest["scripts"])


def test_mysql_schema_inventory_has_bigint_player_and_matching_outbox_fields():
    migration = (ROOT / "sql/my/131_v3_player_feedback_outbox.sql").read_text()
    assert "player_id BIGINT NOT NULL" in migration
    assert "public_issue_title" in migration and "public_issue_body" in migration
    assert "UNIQUE KEY player_feedback_idempotency (player_id, idempotency_key)" in migration


def test_both_clients_offer_feedback_and_status_with_bounded_capture():
    python_menu = json.loads((ROOT / "client/python_client/menus.json").read_text())
    labels = {item.get("label", "") for item in python_menu["menus"]["MAIN"]["options"]}
    assert any("Report Bug" in label for label in labels)
    assert any("Feedback Report Status" in label for label in labels)
    godot_menu = (ROOT / "client/godot_client/godot/CommandMenu.gd").read_text()
    transport = (ROOT / "client/godot_client/godot/ProtocolTransport.gd").read_text()
    assert "local_feedback" in godot_menu and "local_feedback_status" in godot_menu
    assert "DIAGNOSTIC_CAPACITY := 100" in transport
    assert "diagnostic_snapshot(count: int = 25)" in transport


def test_publisher_is_queue_only_without_github_and_uses_stable_report_marker():
    publisher = (ROOT / "tools/feedback_github_publisher.py").read_text()
    assert '"TWCLONE_FEEDBACK_GITHUB_ENABLED"' in publisher
    assert "twclone-report-id:{report_id}" in publisher
    assert "FOR UPDATE SKIP LOCKED" in publisher
    assert "Retry-After" in publisher and "X-RateLimit-Reset" in publisher
    assert "GitHub publication failed; retry scheduled." in publisher
