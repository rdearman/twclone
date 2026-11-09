-- V3-SHIP-02: expose primary-owner semantic drift for lifecycle acceptance.
-- A ship may have several non-primary memberships, but an owned ship must have
-- exactly one non-null primary owner. This is observational so upgrades do not
-- rewrite or reject legacy rows before they are repaired.
BEGIN;

CREATE OR REPLACE VIEW ship_primary_owner_semantics_snapshot AS
SELECT
  (SELECT count(*)
     FROM (
       SELECT o.ship_id
         FROM ship_ownership o
        GROUP BY o.ship_id
       HAVING count(*) FILTER (WHERE o.is_primary IS TRUE) = 0
     ) missing_primary) AS owned_ships_without_primary,
  (SELECT count(*)
     FROM ship_ownership
    WHERE is_primary IS NULL) AS null_primary_flags,
  (SELECT count(*)
     FROM (
       SELECT o.ship_id
         FROM ship_ownership o
        WHERE o.is_primary IS TRUE
        GROUP BY o.ship_id
       HAVING count(*) > 1
     ) duplicate_primary) AS duplicate_primary_owners;

COMMIT;
