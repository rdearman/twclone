-- Preserve destroyed StarPort rows for historical references. A nullable
-- generated sector key permits any number of inactive rows but only one
-- active port per sector.
DELIMITER $$
DROP PROCEDURE IF EXISTS migrate_active_starport_sector_uniqueness$$
CREATE PROCEDURE migrate_active_starport_sector_uniqueness()
BEGIN
    DECLARE v_count INT DEFAULT 0;

    IF NOT EXISTS (
        SELECT 1 FROM information_schema.columns
         WHERE table_schema = DATABASE() AND table_name = 'ports'
           AND column_name = 'is_active'
    ) THEN
        ALTER TABLE ports ADD COLUMN is_active BOOLEAN NOT NULL DEFAULT TRUE;
    END IF;

    IF NOT EXISTS (
        SELECT 1 FROM information_schema.columns
         WHERE table_schema = DATABASE() AND table_name = 'ports'
           AND column_name = 'active_sector_id'
    ) THEN
        ALTER TABLE ports ADD COLUMN active_sector_id BIGINT
            GENERATED ALWAYS AS
            (CASE WHEN is_active THEN sector_id ELSE NULL END) STORED;
    END IF;

    SELECT COUNT(*) INTO v_count FROM (
        SELECT sector_id FROM ports WHERE is_active = TRUE
         GROUP BY sector_id HAVING COUNT(*) > 1
    ) AS duplicate_active_sectors;
    IF v_count > 0 THEN
        SIGNAL SQLSTATE '45000'
            SET MESSAGE_TEXT = 'Cannot enforce one active StarPort per sector: duplicate active ports exist';
    END IF;

    IF EXISTS (
        SELECT 1 FROM information_schema.statistics
         WHERE table_schema = DATABASE() AND table_name = 'ports'
           AND index_name = 'idx_ports_loc_number'
    ) THEN
        ALTER TABLE ports DROP INDEX idx_ports_loc_number;
    END IF;
    IF EXISTS (
        SELECT 1 FROM information_schema.statistics
         WHERE table_schema = DATABASE() AND table_name = 'ports'
           AND index_name = 'idx_ports_loc'
    ) THEN
        ALTER TABLE ports DROP INDEX idx_ports_loc;
    END IF;
    CREATE UNIQUE INDEX idx_ports_loc ON ports (active_sector_id);
END$$
CALL migrate_active_starport_sector_uniqueness()$$
DROP PROCEDURE migrate_active_starport_sector_uniqueness$$
DELIMITER ;
