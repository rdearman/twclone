-- Run against a disposable PostgreSQL database after starting the server and
-- engine. The engine's startup smoke push uses this stable idempotency key;
-- the fixture must contain player 42 so notice.publish can complete.
DO $test$
DECLARE
  v_type text;
  v_status text;
  v_attempts integer;
BEGIN
  SELECT type, status, attempts
    INTO v_type, v_status, v_attempts
    FROM engine_commands
   WHERE idem_key = 'player:42:hello:001';

  IF NOT FOUND THEN
    RAISE EXCEPTION 'engine S2S command.push was not persisted';
  END IF;
  IF v_type <> 'notice.publish' OR v_status <> 'done' OR v_attempts <> 0 THEN
    RAISE EXCEPTION 'engine S2S command did not complete successfully: type=%, status=%, attempts=%',
      v_type, v_status, v_attempts;
  END IF;
  IF NOT EXISTS (
    SELECT 1 FROM system_notice
     WHERE scope = 'player' AND player_id = 42 AND body = 'Hello captain!'
  ) THEN
    RAISE EXCEPTION 'processed S2S command did not publish its notice';
  END IF;
END
$test$;

\echo 'S2S command.push database integration passed: persisted and processed.'
