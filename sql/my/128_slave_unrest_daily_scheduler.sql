INSERT IGNORE INTO cron_tasks(name,schedule,last_run_at,next_due_at,enabled,payload)
VALUES ('daily_slave_unrest','daily@04:15Z',NULL,CURRENT_TIMESTAMP,TRUE,NULL);
