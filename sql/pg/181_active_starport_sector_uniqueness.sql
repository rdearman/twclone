-- Keep destroyed port rows available to trade/audit history while allowing
-- their sectors to be reused. Only active ports occupy a sector.
BEGIN;

ALTER TABLE ports
  ADD COLUMN IF NOT EXISTS is_active boolean NOT NULL DEFAULT TRUE;

DO $$
BEGIN
  IF EXISTS (
      SELECT 1 FROM ports
       WHERE is_active = TRUE
       GROUP BY sector_id
      HAVING COUNT(*) > 1
  ) THEN
    RAISE EXCEPTION 'Cannot enforce one active StarPort per sector: duplicate active ports exist';
  END IF;
END;
$$;

DROP INDEX IF EXISTS idx_ports_loc_number;
DROP INDEX IF EXISTS idx_ports_loc;
CREATE UNIQUE INDEX idx_ports_loc ON ports (sector_id) WHERE is_active = TRUE;

COMMIT;
