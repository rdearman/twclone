-- FedSpace Warp Connections (Sectors 1-10)
-- These are the canonical FedSpace network connections.
-- They must be seeded BEFORE bigbang generates random warps.
-- This ensures the FedSpace region is always properly connected.

INSERT IGNORE INTO sector_warps (from_sector, to_sector) VALUES
    -- Sector 1 connections
    (1, 2), (1, 3), (1, 4), (1, 5), (1, 6), (1, 7),
    -- Sector 2 connections  
    (2, 3), (2, 7), (2, 8), (2, 9), (2, 10),
    -- Sector 3 connections
    (3, 1), (3, 4),
    -- Sector 4 connections
    (4, 1), (4, 5),
    -- Sector 5 connections
    (5, 1), (5, 4), (5, 6),
    -- Sector 7 connections
    (7, 2), (7, 8),
    -- Sector 9 connections
    (9, 2), (9, 10);
