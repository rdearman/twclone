-- REQUIREMENT_ID: V3-SHIP-02
-- ship_ownership.role_id is part of the lifecycle identity and must refer to
-- a defined ship role.  Keep legacy drift observable while rejecting new
-- orphan role references during an upgrade.
BEGIN;

DO $$
BEGIN
  IF NOT EXISTS (
    SELECT 1
      FROM pg_constraint
     WHERE conname = 'ship_ownership_role_fk'
       AND conrelid = 'ship_ownership'::regclass
  ) THEN
    ALTER TABLE ship_ownership
      ADD CONSTRAINT ship_ownership_role_fk
      FOREIGN KEY (role_id) REFERENCES ship_roles (role_id)
      NOT VALID;
  END IF;
END $$;

CREATE INDEX IF NOT EXISTS ix_ship_ownership_role
  ON ship_ownership (role_id);

-- Upgrade-safe evidence: existing invalid rows remain visible instead of
-- making a populated-database migration fail before it can be diagnosed.
CREATE OR REPLACE VIEW ship_ownership_role_invariant_snapshot AS
SELECT count(*) AS orphan_role_references
  FROM ship_ownership o
 WHERE NOT EXISTS (
   SELECT 1
     FROM ship_roles r
    WHERE r.role_id = o.role_id
 );

COMMIT;
