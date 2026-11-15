-- Trade idempotency keys are scoped to an authenticated player.
ALTER TABLE trade_idempotency
    DROP PRIMARY KEY,
    ADD PRIMARY KEY (`key`, player_id);
