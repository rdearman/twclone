"""Server-side contract checks for PostgreSQL-backed insurance settlement."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def test_insurance_handlers_delegate_money_and_claim_state_to_database():
    source = (ROOT / "src/server_loop.c").read_text(encoding="utf-8")
    schema = (ROOT / "src/schemas.c").read_text(encoding="utf-8")
    assert "repo_ships_insurance_buy_policy" in source
    assert "repo_ships_insurance_settle_claim" in source
    assert "SELECT * FROM insurance_buy_policy" not in source
    assert "SELECT * FROM insurance_settle_claim" not in source
    assert "Policy purchased" not in source
    assert "json_integer (12345)" not in source
    assert "insurance_subject_owned" in source
    assert "insurance_claim_owned" in source
    buy_handler = source.rsplit("cmd_insurance_policies_buy (client_ctx_t *ctx", 1)[1].split(
        "cmd_insurance_claim_file", 1
    )[0]
    assert 'strcmp (subject_type, "ship") != 0' in buy_handler
    buy_schema = schema.rsplit("schema_insurance_policies_buy (void)", 1)[1].split(
        "return data_schema;", 1
    )[0]
    assert 'json_string ("ship")' in buy_schema
    assert 'json_string ("cargo")' not in buy_schema
    assert 'json_string ("planet")' not in buy_schema


def test_insurance_json_suite_rejects_non_hull_coverage():
    suite = (ROOT / "tests.v2/suite_economy_insurance.json").read_text(
        encoding="utf-8"
    )
    assert '"subject_type": "cargo"' in suite
    assert "Cargo insurance is not supported" in suite


def test_insurance_policy_and_claim_transactions_are_backend_portable():
    repo = (ROOT / "src/db/repo/repo_ships.c").read_text(encoding="utf-8")
    assert "repo_ships_insurance_buy_policy" in repo
    assert "repo_ships_insurance_settle_claim" in repo
    assert '"bank_accounts_id"' in repo and '? "bank_accounts_id" : "id"' in repo
    assert "INSURANCE_GUARANTEE" in repo
    assert "db_tx_begin" in repo and "db_tx_commit" in repo and "db_tx_rollback" in repo
    assert "UPDATE insurance_claims SET status='paid'" in repo


def test_insurance_sql_fixture_covers_success_refusal_and_idempotency():
    fixture = (ROOT / "tests.v2/test_pg_insurance_settlement.sql").read_text(
        encoding="utf-8"
    )
    assert "premium accounting mismatch" in fixture
    assert "insufficient premium was not atomic" in fixture
    assert "claim idempotency failed" in fixture


def test_destruction_guarantees_hull_payout_when_insurer_fund_is_short():
    source = (ROOT / "src/db/repo/repo_ships.c").read_text(encoding="utf-8")
    settlement = source.split("settle_destruction_insurance", 1)[1].split(
        "int repo_ships_handle_destruction", 1
    )[0]
    assert "INSURANCE_GUARANTEE" in settlement
    assert 'insurer_funded ? "ADJUSTMENT" : "INSURANCE_GUARANTEE"' in settlement
    assert "UPDATE insurance_claims SET status='paid'" in settlement
    assert "if (!has_fund || fund_balance < amount)\n        continue" not in settlement
    migration = (ROOT / "sql/pg/174_galactic_exchange_accounting.sql").read_text(
        encoding="utf-8"
    )
    assert "'INSURANCE_GUARANTEE'" in migration
