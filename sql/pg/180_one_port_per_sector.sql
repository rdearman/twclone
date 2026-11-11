-- V3 StarPort invariant: each sector has zero or one port.
-- Fail explicitly rather than silently deleting or relocating existing assets.
BEGIN;

DO $$
BEGIN
  IF EXISTS (
      SELECT 1
        FROM ports
       GROUP BY sector_id
      HAVING COUNT(*) > 1
  ) THEN
    RAISE EXCEPTION 'Cannot enforce one StarPort per sector: duplicate port sectors exist';
  END IF;
END;
$$;

DROP INDEX IF EXISTS idx_ports_loc;
CREATE UNIQUE INDEX idx_ports_loc ON ports (sector_id);

COMMIT;
