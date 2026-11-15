BEGIN;

-- Trade idempotency keys are scoped to an authenticated player. Keeping the
-- key alone unique made one player's stable client key block every other
-- player who used the same key.
ALTER TABLE trade_idempotency
    DROP CONSTRAINT IF EXISTS trade_idempotency_pkey;

ALTER TABLE trade_idempotency
    ADD CONSTRAINT trade_idempotency_pkey PRIMARY KEY (key, player_id);

COMMIT;
