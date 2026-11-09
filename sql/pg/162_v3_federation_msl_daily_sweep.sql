-- V3 NPC/Federation phase 1: durable daily MSL sweep.
-- BigBang owns the immutable msl_sectors geography; this migration only
-- changes cleanup cadence and records successful UTC-day executions.

CREATE TABLE IF NOT EXISTS fedspace_sweep_runs (
    sweep_date_utc date PRIMARY KEY,
    started_at timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP,
    completed_at timestamptz,
    status text NOT NULL DEFAULT 'running'
        CHECK (status IN ('running', 'completed', 'failed')),
    removed_fighters bigint NOT NULL DEFAULT 0,
    removed_mines bigint NOT NULL DEFAULT 0,
    removed_beacons bigint NOT NULL DEFAULT 0
);

CREATE INDEX IF NOT EXISTS idx_fedspace_sweep_runs_status
    ON fedspace_sweep_runs(status, sweep_date_utc);

INSERT INTO cron_tasks(name, schedule, last_run_at, next_due_at, enabled, payload)
VALUES ('fedspace_cleanup', 'daily@06:00Z', NULL,
        CASE WHEN CURRENT_TIMESTAMP < (CURRENT_DATE AT TIME ZONE 'UTC' + INTERVAL '6 hours')
             THEN (CURRENT_DATE AT TIME ZONE 'UTC' + INTERVAL '6 hours')
             ELSE ((CURRENT_DATE + 1) AT TIME ZONE 'UTC' + INTERVAL '6 hours')
        END,
        TRUE, NULL)
ON CONFLICT (name) DO UPDATE
SET schedule = EXCLUDED.schedule,
    next_due_at = EXCLUDED.next_due_at,
    enabled = TRUE;
