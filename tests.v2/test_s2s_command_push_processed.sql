-- Run after test_s2s_command_push_processed_setup.sql and after starting the
-- server/engine against the same disposable PostgreSQL database.
DO $test$
DECLARE
  v_type text;
  v_status text;
  v_attempts integer;
BEGIN
  SELECT type, status, attempts
    INTO v_type, v_status, v_attempts
    FROM engine_commands
   WHERE idem_key = 'tests.v2:s2s-command-push-processed';

  IF NOT FOUND THEN
    RAISE EXCEPTION 'test-owned engine command fixture was not persisted';
  END IF;
  IF v_type <> 'broadcast.create' OR v_status <> 'done' OR v_attempts <> 0 THEN
    RAISE EXCEPTION 'test-owned command did not complete successfully: type=%, status=%, attempts=%',
      v_type, v_status, v_attempts;
  END IF;
  IF NOT EXISTS (
    SELECT 1 FROM system_notice
     WHERE scope = 'global'
       AND title = 'V2 test-owned S2S command fixture'
       AND body = 'Command processed by the engine regression fixture'
  ) THEN
    RAISE EXCEPTION 'processed command did not create its system notice';
  END IF;
END
$test$;

\echo 'S2S command processing integration passed: test-owned command completed.'
