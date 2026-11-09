from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def test_concurrent_stale_due_order_is_skipped_after_row_lock():
    source = (ROOT / "src/server_bank.c").read_text(encoding="utf-8")
    processor = source.split("db_bank_process_orders (db_t *db)", 1)[1].split(
        "cmd_bank_orders_create", 1
    )[0]

    row_lock = processor.index("FROM bank_orders WHERE %s={1} AND enabled=TRUE FOR UPDATE")
    occurrence_validation = processor.index("occurrence <= 0")
    stale_due_guard = processor.index("if (occurrence > now_s)")
    transfer = processor.index("h_deduct_credits", stale_due_guard)
    validation_end = processor.index("/* Another scheduler worker", occurrence_validation)

    assert row_lock < occurrence_validation < stale_due_guard < transfer
    assert "occurrence > now_s" not in processor[occurrence_validation:validation_end]
    assert "db_tx_rollback (db, NULL); continue;" in processor[
        stale_due_guard : transfer
    ]
