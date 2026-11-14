BEGIN;

CREATE UNIQUE INDEX IF NOT EXISTS ports_one_active_per_sector
    ON ports(sector_id) WHERE is_active=TRUE;

COMMIT;
