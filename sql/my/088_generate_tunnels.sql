-- Generate proper linear tunnel chains before random warp generation
-- This creates isolated tunnel networks that can later be connected to the main universe

DELIMITER $$

CREATE PROCEDURE generate_tunnels (
    IN p_min_tunnels INT,
    IN p_min_tunnel_len INT
)
READS SQL DATA
BEGIN
    DECLARE v_tunnels_created INT DEFAULT 0;
    DECLARE v_next_sector INT;
    DECLARE v_tunnel_len INT;
    DECLARE v_i INT;
    DECLARE v_j INT;
    DECLARE v_prev_sector INT;
    DECLARE v_curr_sector INT;
    DECLARE v_max_sector INT;
    DECLARE v_head_sector INT;
    
    -- Get the current max sector
    SELECT COALESCE(MAX(sector_id), 10) INTO v_max_sector FROM sectors;
    SET v_next_sector = v_max_sector + 1;
    
    -- Create tunnel_heads table for storing entry points
    DROP TEMPORARY TABLE IF EXISTS tunnel_heads;
    CREATE TEMPORARY TABLE tunnel_heads (
        tunnel_id INT AUTO_INCREMENT PRIMARY KEY,
        head_sector INT
    );
    
    -- Create min_tunnels + up to 5 extra tunnels
    WHILE v_tunnels_created < (p_min_tunnels + FLOOR(RAND() * 5)) DO
        -- Random tunnel length between min_tunnel_len and min_tunnel_len + 4
        SET v_tunnel_len = p_min_tunnel_len + FLOOR(RAND() * 5);
        SET v_prev_sector = NULL;
        SET v_head_sector = NULL;
        
        -- Create the tunnel chain
        SET v_j = 0;
        WHILE v_j < v_tunnel_len DO
            SET v_curr_sector = v_next_sector;
            SET v_next_sector = v_next_sector + 1;
            
            -- Create bidirectional warp between previous and current
            IF v_prev_sector IS NOT NULL THEN
                INSERT IGNORE INTO sector_warps (from_sector, to_sector)
                    VALUES (v_prev_sector, v_curr_sector),
                           (v_curr_sector, v_prev_sector);
            END IF;
            
            SET v_prev_sector = v_curr_sector;
            IF v_head_sector IS NULL THEN
                SET v_head_sector = v_curr_sector;
            END IF;
            SET v_j = v_j + 1;
        END WHILE;
        
        -- Store the "head" (entry point) of this tunnel
        IF v_head_sector IS NOT NULL THEN
            INSERT INTO tunnel_heads (head_sector) VALUES (v_head_sector);
        END IF;
        
        SET v_tunnels_created = v_tunnels_created + 1;
    END WHILE;
END$$

DELIMITER ;
