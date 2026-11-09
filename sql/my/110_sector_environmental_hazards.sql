-- Typed environmental hazards used by sector-entry gameplay.
-- Inline indexes and constraints make CREATE TABLE IF NOT EXISTS replay-safe.
CREATE TABLE IF NOT EXISTS sector_hazards (
    sector_id BIGINT NOT NULL,
    hazard_type VARCHAR(16) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    severity INT NOT NULL,
    PRIMARY KEY (sector_id, hazard_type),
    KEY idx_sector_hazards_type (hazard_type, sector_id),
    CONSTRAINT sector_hazards_sector_fk FOREIGN KEY (sector_id) REFERENCES sectors (sector_id) ON DELETE CASCADE,
    CONSTRAINT sector_hazards_type_check CHECK
        (hazard_type IN ('volcanic', 'nebula', 'radiation')),
    CONSTRAINT sector_hazards_severity_check CHECK
        (severity BETWEEN 1 AND 1000));
