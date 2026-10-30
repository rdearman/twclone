"""Concurrent debit regression; set TWCLONE_TEST_PG_DSN to run."""
import os
import subprocess
import time
import uuid

import pytest

DSN = os.environ.get("TWCLONE_TEST_PG_DSN")
pytestmark = pytest.mark.skipif(not DSN, reason="TWCLONE_TEST_PG_DSN is not set")


def psql(sql):
    return subprocess.run(["psql", DSN, "-X", "-qAt", "-v", "ON_ERROR_STOP=1", "-c", sql],
                          check=True, text=True, capture_output=True).stdout.strip()


def test_concurrent_debits_cannot_overdraw_account(tmp_path):
    schema = "bank_lock_" + uuid.uuid4().hex
    migration = os.path.join(os.path.dirname(__file__), "..", "sql", "pg", "121_bank_debit_lock.sql")
    setup = f'''CREATE SCHEMA {schema}; SET search_path TO {schema}, pg_catalog;
        CREATE TABLE bank_accounts (bank_accounts_id integer PRIMARY KEY, balance bigint NOT NULL CHECK (balance >= 0));
        CREATE TABLE bank_transactions (bank_transactions_id serial PRIMARY KEY, account_id integer NOT NULL REFERENCES bank_accounts,
          direction text NOT NULL, amount bigint NOT NULL CHECK (amount > 0));
        {open(migration).read()}
        CREATE TRIGGER bank_debit_guard BEFORE INSERT ON bank_transactions FOR EACH ROW EXECUTE FUNCTION fn_bank_transactions_before_insert();
        CREATE OR REPLACE FUNCTION bank_after_insert() RETURNS trigger LANGUAGE plpgsql AS $$ BEGIN
          UPDATE bank_accounts SET balance = CASE WHEN NEW.direction='DEBIT' THEN balance-NEW.amount ELSE balance+NEW.amount END
           WHERE bank_accounts_id=NEW.account_id; RETURN NEW; END $$;
        CREATE TRIGGER bank_balance_update AFTER INSERT ON bank_transactions FOR EACH ROW EXECUTE FUNCTION bank_after_insert();
        INSERT INTO bank_accounts VALUES (1,100);'''
    psql(setup)
    first = None
    try:
        first = subprocess.Popen(["psql", DSN, "-X", "-qAt", "-v", "ON_ERROR_STOP=1", "-c",
            f"SET search_path TO {schema}, pg_catalog; BEGIN; INSERT INTO bank_transactions(account_id,direction,amount) VALUES (1,'DEBIT',100); SELECT pg_sleep(2); COMMIT;"],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            if psql(f"SELECT count(*) FROM pg_stat_activity WHERE query LIKE '%pg_sleep(2)%' AND state='active'") == "1":
                break
            if first.poll() is not None:
                raise AssertionError(first.communicate())
            time.sleep(0.05)
        result = subprocess.run(["psql", DSN, "-X", "-qAt", "-v", "ON_ERROR_STOP=1", "-c",
            f"SET search_path TO {schema}, pg_catalog; INSERT INTO bank_transactions(account_id,direction,amount) VALUES (1,'DEBIT',100);"],
            text=True, capture_output=True)
        out, err = first.communicate(timeout=5)
        assert first.returncode == 0, err
        assert result.returncode != 0
        assert "BANK_INSUFFICIENT_FUNDS" in result.stderr
        assert psql(f"SELECT balance FROM {schema}.bank_accounts WHERE bank_accounts_id=1") == "0"
        assert psql(f"SELECT count(*) FROM {schema}.bank_transactions") == "1"
    finally:
        if first and first.poll() is None:
            first.kill()
            first.communicate()
        psql(f"DROP SCHEMA {schema} CASCADE")
