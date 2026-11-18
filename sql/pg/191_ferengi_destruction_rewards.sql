BEGIN;

ALTER TABLE ships
  ADD COLUMN IF NOT EXISTS destruction_sequence integer NOT NULL DEFAULT 0;

DO $$
BEGIN
  IF NOT EXISTS (SELECT 1 FROM pg_constraint
      WHERE conname='ships_destruction_sequence_nonnegative'
        AND conrelid='ships'::regclass) THEN
    ALTER TABLE ships ADD CONSTRAINT ships_destruction_sequence_nonnegative
      CHECK (destruction_sequence >= 0);
  END IF;
END $$;

-- System-generated reward; each durable destruction sequence is settled once.
CREATE TABLE IF NOT EXISTS ferengi_destruction_rewards (
  ship_id integer NOT NULL REFERENCES ships(ship_id) ON DELETE RESTRICT,
  destruction_sequence integer NOT NULL CHECK (destruction_sequence > 0),
  recipient_player_id integer NOT NULL REFERENCES players(player_id) ON DELETE RESTRICT,
  amount bigint NOT NULL CHECK (amount = 5000),
  source text NOT NULL CHECK (source = 'system_generated'),
  event_id bigserial UNIQUE NOT NULL,
  created_at timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (ship_id, destruction_sequence)
);

COMMIT;
