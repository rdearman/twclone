# Sector notice and player trade database design

This document records the database support needed if the server adds persistent
sector notices or trade offers. It is guidance for server/API work; the SQL
below is a migration proposal and is not installed or numbered in the release
chain until the command contracts are settled.

## Current server/schema state

- `system_notice` and `notice_seen` persist global notices and per-player
  acknowledgements. There is no sector ID on a notice.
- `sector.notice` currently appears in topic allowlists, but the inspected
  server/repository code has no DB-backed sector-notice producer or query.
- `trade.offer`, `trade.accept`, and `trade.cancel` are registered but their
  handlers return `ERR_NOT_IMPLEMENTED`. Their current schemas do not define
  an offer payload (the offer schema only accepts `trade_id`). There is no
  trade-offer table.

Consequently, no migration is required by the current implemented behavior.
Do not ship unused columns/tables until server command semantics are agreed.

## Persistent sector notices

If sector notices need history, reconnect delivery, or acknowledgements, reuse
`system_notice` so title/body/severity/expiry and `notice_seen` stay in one
model. Add a nullable sector reference: `NULL` means global, a sector ID means
sector-scoped. Existing notice queries and push delivery must filter by the
player's current sector and include global rows where appropriate.

Proposed PostgreSQL migration (candidate version 109):

```sql
BEGIN;

ALTER TABLE system_notice
  ADD COLUMN IF NOT EXISTS sector_id integer;

DO $$
BEGIN
  IF NOT EXISTS (
    SELECT 1 FROM pg_constraint
    WHERE conrelid = 'system_notice'::regclass
      AND conname = 'system_notice_sector_id_fkey'
  ) THEN
    ALTER TABLE system_notice
      ADD CONSTRAINT system_notice_sector_id_fkey
      FOREIGN KEY (sector_id) REFERENCES sectors(sector_id) ON DELETE CASCADE;
  END IF;
END;
$$;

CREATE INDEX IF NOT EXISTS idx_system_notice_sector_active
  ON system_notice (sector_id, expires_at, created_at DESC)
  WHERE sector_id IS NOT NULL;

COMMIT;
```

This keeps current global notices unchanged and makes sector notices cascade
when a sector is removed. If notices should be ephemeral broadcasts only, keep
them out of the database and use the event topic without this migration.

## Player trade offers

Do not persist an offer until the protocol defines sender, recipient, offered
assets, requested assets, expiry, and retry/idempotency behavior. A narrow
initial model can store immutable versioned JSON payloads while keeping the
offer lifecycle and query keys relational. Validate every payload in the
server; JSON storage does not replace amount/type validation.

Proposed PostgreSQL migration (candidate version 110, after protocol review):

```sql
BEGIN;

CREATE TABLE IF NOT EXISTS trade_offers (
  trade_offer_id bigserial PRIMARY KEY,
  sender_player_id integer NOT NULL REFERENCES players(player_id) ON DELETE CASCADE,
  recipient_player_id integer NOT NULL REFERENCES players(player_id) ON DELETE CASCADE,
  status text NOT NULL DEFAULT 'pending'
    CHECK (status IN ('pending', 'accepted', 'cancelled', 'expired')),
  offer_payload jsonb NOT NULL,
  request_payload jsonb NOT NULL,
  idempotency_key text,
  created_at timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP,
  expires_at timestamptz NOT NULL,
  resolved_at timestamptz,
  CHECK (sender_player_id <> recipient_player_id),
  CHECK (expires_at > created_at),
  CHECK ((status = 'pending' AND resolved_at IS NULL)
      OR (status <> 'pending' AND resolved_at IS NOT NULL))
);

CREATE INDEX IF NOT EXISTS idx_trade_offers_recipient_pending
  ON trade_offers (recipient_player_id, expires_at, trade_offer_id)
  WHERE status = 'pending';
CREATE INDEX IF NOT EXISTS idx_trade_offers_sender_pending
  ON trade_offers (sender_player_id, expires_at, trade_offer_id)
  WHERE status = 'pending';
CREATE UNIQUE INDEX IF NOT EXISTS ux_trade_offers_sender_idempotency
  ON trade_offers (sender_player_id, idempotency_key)
  WHERE idempotency_key IS NOT NULL;

COMMIT;
```

Acceptance must run in one DB transaction: lock the offer row, verify the
recipient, pending state and expiry, verify that both players still have the
offered/requested assets, perform all transfers, and transition the offer to
accepted. Cancellation must be sender-authorized and conditional on pending
state. If an offer promises assets are reserved when created, add explicit
escrow/reservation records and release them on cancel/expiry; the proposal
above intentionally stores an intent and does not reserve assets.

Before implementing the migration, confirm whether payload contents require
normalized offer-line rows for per-asset constraints, whether money transfers
must use bank ledger entries, and whether cancel/expiry history is retained.
Add isolated PostgreSQL migration tests for duplicate idempotency keys,
participant FKs, status transitions, expiry, and concurrent acceptance.
