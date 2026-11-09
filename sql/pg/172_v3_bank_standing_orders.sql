-- Durable calendar-based standing-order state and exactly-once occurrences.
ALTER TABLE bank_orders
  ADD COLUMN frequency text,
  ADD COLUMN utc_hour smallint,
  ADD COLUMN utc_minute smallint,
  ADD COLUMN weekday smallint,
  ADD COLUMN month_day smallint,
  ADD COLUMN last_occurrence_at timestamptz;

ALTER TABLE bank_orders
  ADD CONSTRAINT bank_orders_frequency_check
    CHECK (frequency IS NULL OR frequency IN ('daily','weekly','monthly')),
  ADD CONSTRAINT bank_orders_time_check
    CHECK ((utc_hour IS NULL OR utc_hour BETWEEN 0 AND 23)
       AND (utc_minute IS NULL OR utc_minute BETWEEN 0 AND 59)),
  ADD CONSTRAINT bank_orders_calendar_check
    CHECK ((frequency <> 'weekly' OR weekday BETWEEN 1 AND 7)
       AND (frequency <> 'monthly' OR month_day BETWEEN 1 AND 31));

CREATE INDEX idx_bank_orders_due
  ON bank_orders (enabled, next_run_at);

CREATE TABLE bank_order_occurrences (
  bank_order_occurrences_id bigserial PRIMARY KEY,
  bank_orders_id integer NOT NULL REFERENCES bank_orders(bank_orders_id) ON DELETE CASCADE,
  scheduled_for timestamptz NOT NULL,
  outcome text NOT NULL CHECK (outcome IN ('paid','skipped_insufficient_funds','cancelled_invalid_recipient')),
  amount bigint NOT NULL CHECK (amount > 0),
  created_at timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP,
  UNIQUE (bank_orders_id, scheduled_for)
);

INSERT INTO cron_tasks (name,schedule,last_run_at,next_due_at,enabled,payload)
VALUES ('bank_standing_orders_tick','every:1m',NULL,CURRENT_TIMESTAMP,TRUE,NULL)
ON CONFLICT (name) DO NOTHING;
