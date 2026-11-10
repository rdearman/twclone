-- Daily evaluator is always scheduled. Its persisted policy values are zero
-- until configured, so it only performs expiry cleanup and schedules no losses.
INSERT INTO cron_tasks(name,schedule,last_run_at,next_due_at,enabled,payload)
VALUES ('daily_slave_unrest','daily@04:15Z',NULL,now(),TRUE,NULL)
ON CONFLICT (name) DO UPDATE SET schedule=EXCLUDED.schedule,enabled=TRUE;
