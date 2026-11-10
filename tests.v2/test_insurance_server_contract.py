"""Server-side contract checks for PostgreSQL-backed insurance settlement."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def test_insurance_handlers_delegate_money_and_claim_state_to_database():
    source = (ROOT / "src/server_loop.c").read_text(encoding="utf-8")
    assert "insurance_buy_policy" in source
    assert "insurance_settle_claim" in source
    assert "Policy purchased" not in source
    assert "json_integer (12345)" not in source
    assert "insurance_subject_owned" in source
    assert "insurance_claim_owned" in source


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
