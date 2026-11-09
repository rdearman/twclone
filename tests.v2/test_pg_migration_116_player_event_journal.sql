-- Run on a disposable PostgreSQL database after migrations through 115.
\set ON_ERROR_STOP on

\ir ../sql/pg/116_player_event_journal.sql
\ir ../sql/pg/116_player_event_journal.sql

BEGIN;

DO $test$
DECLARE
    v_player_id integer;
    v_stream_id uuid;
    v_event_id uuid;
BEGIN
    INSERT INTO players (name, passwd)
    VALUES ('event-journal-migration-test', 'not-a-login')
    RETURNING player_id INTO v_player_id;

    INSERT INTO player_event_streams (player_id)
    VALUES (v_player_id)
    RETURNING stream_id INTO v_stream_id;

    IF (SELECT last_sequence FROM player_event_streams
         WHERE player_id = v_player_id) <> 0
       OR (SELECT pruned_through_sequence FROM player_event_streams
            WHERE player_id = v_player_id) <> 0 THEN
        RAISE EXCEPTION 'new stream must start with empty high-water and prune cursors';
    END IF;

    INSERT INTO player_events (player_id, stream_id, sequence, event_type,
                               topic, payload)
    VALUES (v_player_id, v_stream_id, 1, 'test.event', 'test.topic', '{"v":1}')
    RETURNING event_id INTO v_event_id;

    IF v_event_id IS NULL OR (SELECT payload FROM player_events
                               WHERE event_id = v_event_id) <> '{"v":1}'::jsonb THEN
        RAISE EXCEPTION 'event UUID and JSON payload must persist';
    END IF;

    -- Cursor paging is exclusive and ordered by the per-player sequence.
    INSERT INTO player_events (player_id, stream_id, sequence, event_type, payload)
    VALUES (v_player_id, v_stream_id, 2, 'test.event', '{"v":2}');
    IF (SELECT array_agg(sequence ORDER BY sequence)
          FROM player_events
         WHERE player_id = v_player_id AND sequence > 1 AND sequence <= 2)
       IS DISTINCT FROM ARRAY[2::bigint] THEN
        RAISE EXCEPTION 'event sequence must support exclusive ordered resume pages';
    END IF;
    UPDATE player_event_streams SET last_sequence = 2
     WHERE player_id = v_player_id;

    -- A duplicate cursor sequence is rejected.
    BEGIN
        INSERT INTO player_events (player_id, stream_id, sequence, event_type, payload)
        VALUES (v_player_id, v_stream_id, 1, 'test.duplicate', '{}');
        RAISE EXCEPTION 'duplicate player sequence unexpectedly accepted';
    EXCEPTION WHEN unique_violation THEN
        NULL;
    END;

    -- A cursor cannot attach an event to a different stream identity.
    BEGIN
        INSERT INTO player_events (player_id, stream_id, sequence, event_type, payload)
        VALUES (v_player_id, gen_random_uuid(), 3, 'test.foreign', '{}');
        RAISE EXCEPTION 'foreign stream identity unexpectedly accepted';
    EXCEPTION WHEN foreign_key_violation THEN
        NULL;
    END;

    -- The pruning watermark cannot be ahead of allocated sequence state.
    BEGIN
        UPDATE player_event_streams SET pruned_through_sequence = 3
         WHERE player_id = v_player_id;
        RAISE EXCEPTION 'pruning watermark ahead of high-water unexpectedly accepted';
    EXCEPTION WHEN check_violation THEN
        NULL;
    END;

    IF NOT EXISTS (
        SELECT 1 FROM pg_indexes
         WHERE schemaname = current_schema()
           AND tablename = 'player_events'
           AND indexname = 'idx_player_events_expiry'
    ) THEN
        RAISE EXCEPTION 'event retention index is missing';
    END IF;
END
$test$;

ROLLBACK;

\echo 'Player event journal migration replay/constraint checks passed.'
