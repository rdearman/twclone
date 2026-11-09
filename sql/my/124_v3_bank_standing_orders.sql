-- Durable calendar-based standing-order state and exactly-once occurrences.
ALTER TABLE bank_orders
  ADD COLUMN frequency VARCHAR(16) NULL,
  ADD COLUMN utc_hour SMALLINT NULL,
  ADD COLUMN utc_minute SMALLINT NULL,
  ADD COLUMN weekday SMALLINT NULL,
  ADD COLUMN month_day SMALLINT NULL,
  ADD COLUMN last_occurrence_at TIMESTAMP NULL;

ALTER TABLE bank_orders
  ADD CONSTRAINT chk_bank_orders_frequency
    CHECK (frequency IS NULL OR frequency IN ('daily','weekly','monthly')),
  ADD CONSTRAINT chk_bank_orders_time
    CHECK ((utc_hour IS NULL OR utc_hour BETWEEN 0 AND 23)
       AND (utc_minute IS NULL OR utc_minute BETWEEN 0 AND 59)),
  ADD CONSTRAINT chk_bank_orders_calendar
    CHECK ((frequency <> 'weekly' OR weekday BETWEEN 1 AND 7)
       AND (frequency <> 'monthly' OR month_day BETWEEN 1 AND 31));

CREATE INDEX idx_bank_orders_due ON bank_orders (enabled, next_run_at);

CREATE TABLE bank_order_occurrences (
  id BIGINT AUTO_INCREMENT PRIMARY KEY,
  bank_orders_id BIGINT NOT NULL,
  scheduled_for TIMESTAMP NOT NULL,
  outcome VARCHAR(40) NOT NULL CHECK (outcome IN ('paid','skipped_insufficient_funds','cancelled_invalid_recipient')),
  amount BIGINT NOT NULL CHECK (amount > 0),
  created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  UNIQUE KEY uq_bank_order_occurrence (bank_orders_id, scheduled_for),
  FOREIGN KEY (bank_orders_id) REFERENCES bank_orders(id) ON DELETE CASCADE
);

INSERT INTO cron_tasks (name,schedule,last_run_at,next_due_at,enabled,payload)
VALUES ('bank_standing_orders_tick','every:1m',NULL,CURRENT_TIMESTAMP,TRUE,NULL)
ON DUPLICATE KEY UPDATE schedule=VALUES(schedule), enabled=TRUE;
