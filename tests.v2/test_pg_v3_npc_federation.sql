-- V3 NPC/Federation migration and invariant checks.
-- Run with: psql "$TEST_DATABASE_URL" -v ON_ERROR_STOP=1 -f this_file.sql

BEGIN;

DO $$
BEGIN
  IF to_regclass('public.fedspace_sweep_runs') IS NULL THEN
    RAISE EXCEPTION 'fedspace_sweep_runs is missing';
  END IF;
  IF to_regclass('public.npc_action_log') IS NULL THEN
    RAISE EXCEPTION 'npc_action_log is missing';
  END IF;
  IF to_regclass('public.npc_security_incidents') IS NULL THEN
    RAISE EXCEPTION 'npc_security_incidents is missing';
  END IF;
  IF to_regclass('public.npc_intelligence') IS NULL THEN
    RAISE EXCEPTION 'npc_intelligence is missing';
  END IF;
END $$;

DO $$
DECLARE schedule_value text;
BEGIN
  SELECT schedule INTO schedule_value
  FROM cron_tasks WHERE name = 'fedspace_cleanup';
  IF schedule_value <> 'daily@06:00Z' THEN
    RAISE EXCEPTION 'unexpected FedSpace schedule: %', schedule_value;
  END IF;
END $$;

DO $$
BEGIN
  IF NOT EXISTS (SELECT 1 FROM msl_sectors) THEN
    RAISE EXCEPTION 'MSL topology is empty';
  END IF;
  IF NOT EXISTS (
      SELECT 1 FROM config
      WHERE key = 'npc.iss_incident_lifetime_seconds'
        AND value = '86400') THEN
    RAISE EXCEPTION 'ISS incident lifetime configuration is missing';
  END IF;
END $$;

ROLLBACK;
