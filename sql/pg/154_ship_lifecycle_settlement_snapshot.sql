-- REQUIREMENT_ID: V3-SHIP-02
-- Stable before/after evidence for ship sale/transfer lifecycle transactions.
-- This view is observational: it does not repair legacy rows or alter the
-- ownership, cargo, or tow guards installed by earlier migrations.
BEGIN;

CREATE OR REPLACE VIEW ship_lifecycle_settlement_snapshot AS
WITH active AS (
  SELECT p.player_id, p.ship_id
    FROM players p
   WHERE p.ship_id IS NOT NULL
), primary_active AS (
  SELECT DISTINCT a.ship_id, a.player_id
    FROM active a
    JOIN ship_ownership o
      ON o.ship_id = a.ship_id
     AND o.player_id = a.player_id
     AND o.is_primary IS TRUE
), legacy_cargo AS (
  SELECT COALESCE(SUM(
           COALESCE(s.colonists, 0) + COALESCE(s.ore, 0) +
           COALESCE(s.organics, 0) + COALESCE(s.equipment, 0) +
           COALESCE(s.slaves, 0) + COALESCE(s.weapons, 0) +
           COALESCE(s.drugs, 0)), 0)::bigint AS units
    FROM ships s
), dynamic_cargo AS (
  SELECT COALESCE(SUM(sc.quantity), 0)::bigint AS units
    FROM ship_cargo sc
)
SELECT
  (SELECT count(*)::bigint FROM active) AS active_ship_count,
  (SELECT count(*)::bigint FROM primary_active) AS active_primary_owner_count,
  legacy_cargo.units AS fleet_legacy_cargo_units,
  dynamic_cargo.units AS fleet_dynamic_cargo_units,
  (SELECT count(*)::bigint
     FROM active a
     JOIN ships s ON s.ship_id = a.ship_id
    WHERE COALESCE(s.towing_ship_id, 0) <> 0
       OR COALESCE(s.is_being_towed_by, 0) <> 0) AS active_tow_link_count;

COMMIT;
