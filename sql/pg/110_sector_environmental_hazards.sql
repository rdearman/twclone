-- Typed environmental hazards used by the sector-entry combat resolver.
-- Keep the IF NOT EXISTS guard so this migration also follows fresh installs
-- that already include the table in 000_tables.sql.
CREATE TABLE IF NOT EXISTS sector_hazards (
    sector_id integer NOT NULL REFERENCES sectors (sector_id) ON DELETE CASCADE,
    hazard_type text NOT NULL CHECK (hazard_type IN ('volcanic', 'nebula', 'radiation')),
    severity integer NOT NULL CHECK (severity BETWEEN 1 AND 1000),
    PRIMARY KEY (sector_id, hazard_type)
);

CREATE INDEX IF NOT EXISTS idx_sector_hazards_type
    ON sector_hazards (hazard_type, sector_id);
