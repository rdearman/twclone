-- Durable per-player replay journal for the V3 gateway event bridge.
-- Replayable events receive a monotonically increasing sequence within a
-- player's immutable stream identity. Cleanup advances the pruning watermark
-- only over a contiguous prefix of expired sequences while locking the stream.

-- PostgreSQL 12 provides gen_random_uuid() through pgcrypto; newer versions
-- expose the same function in core and keep this extension declaration safe.
CREATE EXTENSION IF NOT EXISTS pgcrypto;

BEGIN;

CREATE TABLE IF NOT EXISTS player_event_streams (
    player_id integer PRIMARY KEY
        REFERENCES players (player_id) ON DELETE CASCADE,
    stream_id uuid NOT NULL DEFAULT gen_random_uuid(),
    last_sequence bigint NOT NULL DEFAULT 0 CHECK (last_sequence >= 0),
    pruned_through_sequence bigint NOT NULL DEFAULT 0
        CHECK (pruned_through_sequence >= 0
           AND pruned_through_sequence <= last_sequence),
    created_at timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP,
    UNIQUE (player_id, stream_id)
);

CREATE TABLE IF NOT EXISTS player_events (
    event_id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    player_id integer NOT NULL,
    stream_id uuid NOT NULL,
    sequence bigint NOT NULL CHECK (sequence > 0),
    event_type text NOT NULL CHECK (event_type <> ''),
    topic text,
    payload jsonb NOT NULL,
    created_at timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP,
    expires_at timestamptz NOT NULL DEFAULT (CURRENT_TIMESTAMP + INTERVAL '7 days'),
    UNIQUE (player_id, sequence),
    FOREIGN KEY (player_id, stream_id)
        REFERENCES player_event_streams (player_id, stream_id)
        ON DELETE CASCADE,
    CHECK (expires_at > created_at)
);

-- The unique (player_id, sequence) index is also the resume page index. Keep
-- expiry separate so cleanup can find expired rows without scanning streams.
CREATE INDEX IF NOT EXISTS idx_player_events_expiry
    ON player_events (expires_at, player_id, sequence);

COMMENT ON TABLE player_event_streams IS
    'Per-player event cursor identity and sequence high-water/pruning watermark.';
COMMENT ON COLUMN player_event_streams.last_sequence IS
    'Highest sequence allocated in this player stream; sequence allocation locks this row.';
COMMENT ON COLUMN player_event_streams.pruned_through_sequence IS
    'Highest contiguous sequence removed by retention cleanup; cursors below it require resync, while equality can continue safely.';
COMMENT ON TABLE player_events IS
    'Replayable player event deliveries, persisted before live socket publication.';
COMMENT ON COLUMN player_events.sequence IS
    'Strictly increasing per-player ordering key; event timestamps are not ordering keys.';
COMMENT ON COLUMN player_events.expires_at IS
    'Retention deadline; cleanup deletes expired rows and advances only a contiguous stream prefix.';

COMMIT;
