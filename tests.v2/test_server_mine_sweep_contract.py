from pathlib import Path


ROOT = Path(__file__).parents[1]
SCHEMAS = (ROOT / "src/schemas.c").read_text()
COMBAT = (ROOT / "src/server_combat.c").read_text()


def test_mine_sweep_schema_requires_only_current_target_sector():
    start = SCHEMAS.index("json_t *\nschema_combat_sweep_mines")
    body = SCHEMAS[start:SCHEMAS.index("schema_combat_status", start)]
    assert '"target_sector_id"' in body
    assert 'json_string ("target_sector_id")' in body
    assert '"additionalProperties", json_boolean (0)' in body


def test_mine_sweep_locks_and_removes_assets_inside_one_transaction():
    start = COMBAT.index("cmd_combat_sweep_mines")
    body = COMBAT[start:COMBAT.index("cmd_fighters_recall", start)]
    assert body.index("db_tx_begin") < body.index("db_combat_select_mines_locked")
    assert body.index("db_combat_select_mines_locked") < body.index("db_combat_delete_sector_asset")
    assert "db_tx_commit" in body and "db_tx_rollback" in body
    assert '"mines_removed"' in body
