-- Run against a disposable PostgreSQL database before starting the server.
-- This explicit fixture replaces the removed player-42 startup demo.
DELETE FROM engine_commands
 WHERE idem_key = 'tests.v2:s2s-command-push-processed';
DELETE FROM system_notice
 WHERE title = 'V2 test-owned S2S command fixture';

INSERT INTO engine_commands (type, payload, due_at, idem_key, status)
VALUES (
  'broadcast.create',
  '{"title":"V2 test-owned S2S command fixture","body":"Command processed by the engine regression fixture","severity":"info"}',
  CURRENT_TIMESTAMP,
  'tests.v2:s2s-command-push-processed',
  'ready'
);

\echo 'Queued test-owned S2S command processing fixture.'
