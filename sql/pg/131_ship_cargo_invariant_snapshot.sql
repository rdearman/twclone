-- V3-SHIP-02: expose canonical ship-cargo conservation and legacy-mirror drift.
-- ship_cargo is authoritative; the legacy columns remain compatibility mirrors
-- until their callers are retired. This view is intentionally read-only so
-- restart/concurrency harnesses can assert a zero-violation snapshot.
BEGIN;

CREATE OR REPLACE VIEW ship_cargo_invariant_snapshot AS
SELECT
  (SELECT count(*)
     FROM ship_cargo sc
    WHERE sc.quantity IS NULL OR sc.quantity < 0)
    AS negative_cargo_quantities,
  (SELECT count(*)
     FROM (
       SELECT sc.ship_id
         FROM ship_cargo sc
         JOIN ships s ON s.ship_id = sc.ship_id
        GROUP BY sc.ship_id, s.holds
       HAVING COALESCE(SUM(sc.quantity), 0) > COALESCE(s.holds, 0)
     ) overfull) AS over_capacity_ships,
  (SELECT count(*)
     FROM ships s
    WHERE COALESCE(s.ore, 0) IS DISTINCT FROM
          COALESCE((SELECT sc.quantity FROM ship_cargo sc
                    WHERE sc.ship_id = s.ship_id AND sc.commodity_code = 'ORE'), 0)
       OR COALESCE(s.organics, 0) IS DISTINCT FROM
          COALESCE((SELECT sc.quantity FROM ship_cargo sc
                    WHERE sc.ship_id = s.ship_id AND sc.commodity_code = 'ORG'), 0)
       OR COALESCE(s.equipment, 0) IS DISTINCT FROM
          COALESCE((SELECT sc.quantity FROM ship_cargo sc
                    WHERE sc.ship_id = s.ship_id AND sc.commodity_code = 'EQU'), 0)
       OR COALESCE(s.colonists, 0) IS DISTINCT FROM
          COALESCE((SELECT sc.quantity FROM ship_cargo sc
                    WHERE sc.ship_id = s.ship_id AND sc.commodity_code = 'COL'), 0)
       OR COALESCE(s.slaves, 0) IS DISTINCT FROM
          COALESCE((SELECT sc.quantity FROM ship_cargo sc
                    WHERE sc.ship_id = s.ship_id AND sc.commodity_code = 'SLV'), 0)
       OR COALESCE(s.weapons, 0) IS DISTINCT FROM
          COALESCE((SELECT sc.quantity FROM ship_cargo sc
                    WHERE sc.ship_id = s.ship_id AND sc.commodity_code = 'WPN'), 0)
       OR COALESCE(s.drugs, 0) IS DISTINCT FROM
          COALESCE((SELECT sc.quantity FROM ship_cargo sc
                    WHERE sc.ship_id = s.ship_id AND sc.commodity_code = 'DRG'), 0)
  ) AS legacy_mirror_mismatches;

COMMIT;
