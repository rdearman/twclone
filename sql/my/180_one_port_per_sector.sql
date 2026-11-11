-- V3 StarPort invariant: each sector has zero or one port.
-- Fail explicitly rather than silently deleting or relocating existing assets.
DELIMITER $$
DROP PROCEDURE IF EXISTS migrate_one_port_per_sector$$
CREATE PROCEDURE migrate_one_port_per_sector()
BEGIN
    DECLARE v_non_unique INT DEFAULT NULL;
    IF EXISTS (
        SELECT 1 FROM ports GROUP BY sector_id HAVING COUNT(*) > 1
    ) THEN
        SIGNAL SQLSTATE '45000'
            SET MESSAGE_TEXT = 'Cannot enforce one StarPort per sector: duplicate port sectors exist';
    END IF;

    SELECT MAX(NON_UNIQUE) INTO v_non_unique
      FROM information_schema.statistics
     WHERE table_schema = DATABASE()
       AND table_name = 'ports'
       AND index_name = 'idx_ports_loc'
     LIMIT 1;

    IF v_non_unique = 1 THEN
        ALTER TABLE ports DROP INDEX idx_ports_loc;
        CREATE UNIQUE INDEX idx_ports_loc ON ports (sector_id);
    ELSEIF v_non_unique IS NULL THEN
        CREATE UNIQUE INDEX idx_ports_loc ON ports (sector_id);
    END IF;
END$$
CALL migrate_one_port_per_sector()$$
DROP PROCEDURE migrate_one_port_per_sector$$
DELIMITER ;
