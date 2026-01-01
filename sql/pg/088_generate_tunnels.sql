-- Generate proper linear tunnel chains before random warp generation
-- This creates isolated tunnel networks that can later be connected to the main universe

CREATE OR REPLACE FUNCTION generate_tunnels (
    p_min_tunnels int DEFAULT 15,
    p_min_tunnel_len int DEFAULT 4
)
    RETURNS bigint
    LANGUAGE plpgsql
    AS $$
DECLARE
    v_tunnels_created bigint := 0;
    v_next_sector int;
    v_tunnel_len int;
    v_i int;
    v_prev_sector int;
    v_curr_sector int;
    v_max_sector int;
    v_total_needed int;
    v_tunnel_start int;
    v_tunnel_end int;
BEGIN
    -- Get the current max sector
    SELECT COALESCE(MAX(sector_id), 10) INTO v_max_sector FROM sectors;
    
    -- Calculate how many sectors we'll need for tunnels
    v_total_needed := (p_min_tunnels + 5) * (p_min_tunnel_len + 2);
    
    -- Insert missing sectors
    INSERT INTO sectors (sector_id, name, nebulae)
    SELECT gs, 'Tunnel Sector ' || gs::text, NULL
    FROM generate_series(v_max_sector + 1, v_max_sector + v_total_needed) AS gs
    WHERE NOT EXISTS (SELECT 1 FROM sectors WHERE sector_id = gs)
    ON CONFLICT (sector_id) DO NOTHING;
    
    -- Create table to track tunnel endpoints
    CREATE TABLE IF NOT EXISTS longest_tunnels (
        tunnel_id serial PRIMARY KEY,
        entry_sector int,
        exit_sector int,
        tunnel_path text,
        tunnel_length_edges int
    );
    TRUNCATE longest_tunnels;
    
    v_next_sector := v_max_sector + 1;
    
    -- Create min_tunnels + up to 5 extra tunnels
    WHILE v_tunnels_created < (p_min_tunnels + (random() * 5)::int) LOOP
        -- Random tunnel length between min_tunnel_len and min_tunnel_len + 4
        v_tunnel_len := p_min_tunnel_len + (random() * 5)::int;
        v_prev_sector := NULL;
        v_tunnel_start := NULL;
        v_tunnel_end := NULL;
        
        -- Create the tunnel chain
        FOR v_i IN 1..v_tunnel_len LOOP
            v_curr_sector := v_next_sector;
            v_next_sector := v_next_sector + 1;
            
            -- Track tunnel start and end
            IF v_tunnel_start IS NULL THEN
                v_tunnel_start := v_curr_sector;
            END IF;
            v_tunnel_end := v_curr_sector;
            
            -- Create bidirectional warp between previous and current
            IF v_prev_sector IS NOT NULL THEN
                INSERT INTO sector_warps (from_sector, to_sector)
                    VALUES (v_prev_sector, v_curr_sector),
                           (v_curr_sector, v_prev_sector)
                ON CONFLICT DO NOTHING;
            END IF;
            
            v_prev_sector := v_curr_sector;
        END LOOP;
        
        -- Record this tunnel's endpoints
        INSERT INTO longest_tunnels (entry_sector, exit_sector, tunnel_path, tunnel_length_edges)
        VALUES (v_tunnel_start, v_tunnel_end, v_tunnel_start::text || '-...-' || v_tunnel_end::text, v_tunnel_len);
        
        v_tunnels_created := v_tunnels_created + 1;
    END LOOP;
    
    RETURN v_tunnels_created;
END;
$$;
