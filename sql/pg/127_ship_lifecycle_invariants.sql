-- V3-SHIP-02: enforce ship lifecycle invariants at the PostgreSQL boundary.
-- Existing rows are intentionally left for the snapshot view to report; NOT VALID
-- makes these checks safe to introduce on a populated database while enforcing
-- every subsequent write.
BEGIN;

-- Fresh BigBang installs still use the legacy fleet seed functions, whose
-- INSERT lists intentionally omit optional resource columns.  Give those
-- canonical empty resources an explicit zero default before enforcing the V3
-- non-null invariant.  Existing rows are not rewritten by these defaults.
ALTER TABLE ships
  ALTER COLUMN mines SET DEFAULT 0,
  ALTER COLUMN limpets SET DEFAULT 0,
  ALTER COLUMN genesis SET DEFAULT 0,
  ALTER COLUMN photons SET DEFAULT 0,
  ALTER COLUMN beacons SET DEFAULT 0,
  ALTER COLUMN colonists SET DEFAULT 0,
  ALTER COLUMN equipment SET DEFAULT 0,
  ALTER COLUMN organics SET DEFAULT 0,
  ALTER COLUMN ore SET DEFAULT 0,
  ALTER COLUMN cloaking_devices SET DEFAULT 0;

DO $$
BEGIN
  IF NOT EXISTS (
    SELECT 1 FROM pg_constraint WHERE conname = 'ships_lifecycle_nonnull_resources'
  ) THEN
    ALTER TABLE ships ADD CONSTRAINT ships_lifecycle_nonnull_resources CHECK (
      holds IS NOT NULL AND holds >= 0 AND
      hull IS NOT NULL AND hull BETWEEN 0 AND 100 AND
      mines IS NOT NULL AND mines >= 0 AND
      limpets IS NOT NULL AND limpets >= 0 AND
      fighters IS NOT NULL AND fighters >= 0 AND
      genesis IS NOT NULL AND genesis >= 0 AND
      detonators IS NOT NULL AND detonators >= 0 AND
      probes IS NOT NULL AND probes >= 0 AND
      photons IS NOT NULL AND photons >= 0 AND
      shields IS NOT NULL AND shields >= 0 AND
      installed_shields IS NOT NULL AND installed_shields >= 0 AND
      beacons IS NOT NULL AND beacons >= 0 AND
      colonists IS NOT NULL AND colonists >= 0 AND
      equipment IS NOT NULL AND equipment >= 0 AND
      organics IS NOT NULL AND organics >= 0 AND
      ore IS NOT NULL AND ore >= 0 AND
      slaves IS NOT NULL AND slaves >= 0 AND
      weapons IS NOT NULL AND weapons >= 0 AND
      drugs IS NOT NULL AND drugs >= 0 AND
      cloaking_devices IS NOT NULL AND cloaking_devices >= 0
    ) NOT VALID;
  END IF;
END $$;

-- A clean database gets the strongest native enforcement.  Populated databases
-- may already contain duplicate active references; do not make the migration
-- fail before the snapshot can report those legacy violations.  In that case,
-- enforce every subsequent write with a deferred guard until the rows are
-- repaired and the unique index can be installed by a later maintenance step.
DO $$
BEGIN
  IF NOT EXISTS (
    SELECT 1
      FROM players
     WHERE ship_id IS NOT NULL
     GROUP BY ship_id
    HAVING count(*) > 1
  ) THEN
    CREATE UNIQUE INDEX IF NOT EXISTS ux_players_one_active_ship
      ON players (ship_id) WHERE ship_id IS NOT NULL;
  END IF;
END $$;

CREATE OR REPLACE FUNCTION enforce_one_active_player_ship()
RETURNS trigger
LANGUAGE plpgsql
AS $$
BEGIN
  IF NEW.ship_id IS NOT NULL AND EXISTS (
    SELECT 1
      FROM players p
     WHERE p.ship_id = NEW.ship_id
       AND p.player_id <> NEW.player_id
  ) THEN
    RAISE EXCEPTION 'SHIP_ACTIVE_DUPLICATE:%', NEW.ship_id
      USING ERRCODE = '23514';
  END IF;
  RETURN NEW;
END;
$$;

DROP TRIGGER IF EXISTS players_one_active_ship_guard ON players;
CREATE CONSTRAINT TRIGGER players_one_active_ship_guard
AFTER INSERT OR UPDATE OF ship_id ON players
DEFERRABLE INITIALLY DEFERRED
FOR EACH ROW EXECUTE FUNCTION enforce_one_active_player_ship();

CREATE OR REPLACE FUNCTION enforce_ship_cargo_capacity()
RETURNS trigger
LANGUAGE plpgsql
AS $$
DECLARE
  v_holds bigint;
  v_load bigint;
BEGIN
  SELECT holds INTO v_holds FROM ships WHERE ship_id = NEW.ship_id FOR UPDATE;
  IF NOT FOUND THEN
    RAISE EXCEPTION 'SHIP_NOT_FOUND';
  END IF;

  SELECT COALESCE(SUM(quantity), 0) INTO v_load
    FROM ship_cargo WHERE ship_id = NEW.ship_id;
  IF v_load > v_holds THEN
    RAISE EXCEPTION 'SHIP_CARGO_EXCEEDS_HOLDS';
  END IF;
  RETURN NEW;
END;
$$;

DROP TRIGGER IF EXISTS ship_cargo_capacity_guard ON ship_cargo;
CREATE CONSTRAINT TRIGGER ship_cargo_capacity_guard
AFTER INSERT OR UPDATE ON ship_cargo
DEFERRABLE INITIALLY DEFERRED
FOR EACH ROW EXECUTE FUNCTION enforce_ship_cargo_capacity();

DROP TRIGGER IF EXISTS ship_holds_capacity_guard ON ships;
CREATE CONSTRAINT TRIGGER ship_holds_capacity_guard
AFTER UPDATE OF holds ON ships
DEFERRABLE INITIALLY DEFERRED
FOR EACH ROW EXECUTE FUNCTION enforce_ship_cargo_capacity();

-- This view is a durable, read-only evidence surface for restart/concurrency
-- tests. A zero in every *_violations column is the lifecycle invariant.
CREATE OR REPLACE VIEW ship_lifecycle_invariant_snapshot AS
SELECT
  (SELECT count(*) FROM ships s
    WHERE s.holds IS NULL OR s.holds < 0
       OR s.hull IS NULL OR s.hull < 0 OR s.hull > 100
       OR s.mines IS NULL OR s.mines < 0
       OR s.limpets IS NULL OR s.limpets < 0
       OR s.fighters IS NULL OR s.fighters < 0
       OR s.genesis IS NULL OR s.genesis < 0
       OR s.detonators IS NULL OR s.detonators < 0
       OR s.probes IS NULL OR s.probes < 0
       OR s.photons IS NULL OR s.photons < 0
       OR s.shields IS NULL OR s.shields < 0
       OR s.installed_shields IS NULL OR s.installed_shields < 0
       OR s.beacons IS NULL OR s.beacons < 0
       OR s.colonists IS NULL OR s.colonists < 0
       OR s.equipment IS NULL OR s.equipment < 0
       OR s.organics IS NULL OR s.organics < 0
       OR s.ore IS NULL OR s.ore < 0
       OR s.slaves IS NULL OR s.slaves < 0
       OR s.weapons IS NULL OR s.weapons < 0
       OR s.drugs IS NULL OR s.drugs < 0
       OR s.cloaking_devices IS NULL OR s.cloaking_devices < 0) AS resource_violations,
  (SELECT count(*) FROM ships s
    WHERE COALESCE(s.colonists, 0) + COALESCE(s.ore, 0) +
          COALESCE(s.organics, 0) + COALESCE(s.equipment, 0) +
          COALESCE(s.slaves, 0) + COALESCE(s.weapons, 0) +
          COALESCE(s.drugs, 0) > COALESCE(s.holds, 0)) AS legacy_cargo_violations,
  (SELECT count(*) FROM (
    SELECT sc.ship_id FROM ship_cargo sc
    JOIN ships s ON s.ship_id = sc.ship_id
    GROUP BY sc.ship_id, s.holds
    HAVING COALESCE(SUM(sc.quantity), 0) > COALESCE(s.holds, 0)
  ) overfull) AS dynamic_cargo_violations,
  (SELECT count(*) FROM ship_cargo sc
    WHERE sc.quantity IS NULL OR sc.quantity < 0
  ) AS dynamic_cargo_quantity_violations,
  (SELECT count(*) FROM (
    SELECT p.ship_id FROM players p
    WHERE p.ship_id IS NOT NULL
    GROUP BY p.ship_id HAVING count(*) > 1
  ) duplicate_active_ships) AS active_ship_duplicates,
  (SELECT count(*) FROM ships s
    LEFT JOIN ship_ownership o ON o.ship_id = s.ship_id AND o.is_primary = TRUE
    WHERE o.ship_id IS NULL AND EXISTS (SELECT 1 FROM players p WHERE p.ship_id = s.ship_id)
  ) AS active_without_primary_owner,
  (SELECT count(*) FROM (
    SELECT ship_id
      FROM ship_ownership
     WHERE is_primary = TRUE
     GROUP BY ship_id
    HAVING count(*) > 1
  ) duplicate_primary_owners) AS primary_owner_duplicates,
  (SELECT count(*)
     FROM ship_ownership o
     LEFT JOIN players p ON p.player_id = o.player_id
    WHERE o.is_primary = TRUE AND p.player_id IS NULL
  ) AS orphan_primary_owners,
  (SELECT count(*) FROM players p
    JOIN ships s ON s.ship_id = p.ship_id
    WHERE p.sector_id IS DISTINCT FROM s.sector_id) AS active_sector_mismatches;

COMMIT;
