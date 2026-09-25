-- Phase 1 Migration: Initialize ship_cargo table and dual-write from legacy columns
-- This migration is only needed if upgrading an existing database that was created before
-- ship_cargo table was added to 000_tables.sql
--
-- For new databases: ship_cargo is created directly in 000_tables.sql
-- For existing databases: Run this migration to populate ship_cargo from legacy columns

START TRANSACTION;

-- Insert existing cargo from legacy columns into ship_cargo
-- Commodity codes: ORE, ORG, EQU, COL, SLV, WPN, DRG
INSERT IGNORE INTO ship_cargo (ship_id, commodity_code, quantity)
SELECT ship_id, 'ORE', COALESCE(ore, 0) FROM ships WHERE ore > 0
UNION ALL
SELECT ship_id, 'ORG', COALESCE(organics, 0) FROM ships WHERE organics > 0
UNION ALL
SELECT ship_id, 'EQU', COALESCE(equipment, 0) FROM ships WHERE equipment > 0
UNION ALL
SELECT ship_id, 'COL', COALESCE(colonists, 0) FROM ships WHERE colonists > 0
UNION ALL
SELECT ship_id, 'SLV', COALESCE(slaves, 0) FROM ships WHERE slaves > 0
UNION ALL
SELECT ship_id, 'WPN', COALESCE(weapons, 0) FROM ships WHERE weapons > 0
UNION ALL
SELECT ship_id, 'DRG', COALESCE(drugs, 0) FROM ships WHERE drugs > 0;

COMMIT;
