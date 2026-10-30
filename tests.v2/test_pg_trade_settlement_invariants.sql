\set ON_ERROR_STOP on
BEGIN;
CREATE SCHEMA trade_invariants_125_test;
SET LOCAL search_path TO trade_invariants_125_test, pg_catalog;
CREATE TABLE bank_transactions (bank_transactions_id bigint PRIMARY KEY);
CREATE TABLE commodity_orders (
  commodity_orders_id integer PRIMARY KEY, quantity integer NOT NULL CHECK (quantity > 0),
  filled_quantity integer NOT NULL DEFAULT 0 CHECK (filled_quantity >= 0)
);
CREATE TABLE commodity_trades (
  id serial PRIMARY KEY, settlement_tx_buy bigint, settlement_tx_sell bigint
);
INSERT INTO bank_transactions VALUES (10), (11), (12);
\ir ../sql/pg/125_trade_settlement_invariants.sql
SET search_path TO trade_invariants_125_test, pg_catalog;
INSERT INTO commodity_orders VALUES (1, 5, 5);
INSERT INTO commodity_trades VALUES (DEFAULT, 10, 11);
DO $test$
BEGIN
  BEGIN
    UPDATE commodity_orders SET filled_quantity = 6 WHERE commodity_orders_id = 1;
    RAISE EXCEPTION 'order overfill accepted';
  EXCEPTION WHEN check_violation THEN NULL;
  END;
  BEGIN
    INSERT INTO commodity_trades VALUES (DEFAULT, 10, 12);
    RAISE EXCEPTION 'buy receipt reused';
  EXCEPTION WHEN unique_violation THEN NULL;
  END;
  BEGIN
    INSERT INTO commodity_trades VALUES (DEFAULT, 999, NULL);
    SET CONSTRAINTS ALL IMMEDIATE;
    RAISE EXCEPTION 'unknown settlement receipt accepted';
  EXCEPTION WHEN foreign_key_violation THEN NULL;
  END;
END
$test$;
ROLLBACK;
\echo 'PostgreSQL trade settlement invariant checks passed.'
