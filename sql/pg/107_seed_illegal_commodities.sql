-- Phase 10.7: Seed illegal commodities for evil cluster ports
-- This MUST run AFTER clusters are generated (after 106_phase9_2_cluster_pressure.sql)
-- Because illegal commodities should only be in evil cluster ports

/*
 * Seed illegal goods for:
 * 1. ALL ports in evil clusters (alignment < -250)
 * 2. Black market ports (type = 10) that are NOT in good clusters
 */
INSERT INTO entity_stock (entity_type, entity_id, commodity_code, quantity, price)
SELECT
    'port' AS entity_type,
    p.port_id AS entity_id,
    c.code AS commodity_code,
    GREATEST(10, (p.size * 1000 * (0.20 + random() * 0.30))::int) AS quantity,
    c.base_price AS price
FROM ports p
CROSS JOIN commodities c
WHERE c.illegal = TRUE
  AND (
    /* Condition 1: Port is in any non-good cluster (alignment <= 0) */
    EXISTS (
      SELECT 1 FROM cluster_sectors cs
      JOIN clusters cl ON cs.cluster_id = cl.clusters_id
      WHERE cs.sector_id = p.sector_id AND cl.alignment <= 0
    )
    OR
    /* Condition 2: Port is type 10 AND NOT in a good cluster */
    (p.type = 10 AND NOT EXISTS (
      SELECT 1 FROM cluster_sectors cs
      JOIN clusters cl ON cs.cluster_id = cl.clusters_id
      WHERE cs.sector_id = p.sector_id AND cl.alignment > 0
    ))
  )
ON CONFLICT DO NOTHING;

-- Verify seeding
DO $$
DECLARE
    evil_count int;
    type10_count int;
BEGIN
    SELECT COUNT(DISTINCT p.port_id) INTO evil_count
    FROM entity_stock es
    JOIN ports p ON es.entity_id = p.port_id AND es.entity_type = 'port'
    JOIN commodities c ON es.commodity_code = c.code
    WHERE c.illegal = TRUE
      AND EXISTS (
        SELECT 1 FROM cluster_sectors cs
        JOIN clusters cl ON cs.cluster_id = cl.clusters_id
        WHERE cs.sector_id = p.sector_id AND cl.alignment <= 0
      );
    
    SELECT COUNT(DISTINCT p.port_id) INTO type10_count
    FROM entity_stock es
    JOIN ports p ON es.entity_id = p.port_id AND es.entity_type = 'port'
    JOIN commodities c ON es.commodity_code = c.code
    WHERE c.illegal = TRUE AND p.type = 10;
    
    RAISE NOTICE 'Phase 10.7: Seeded illegal commodities in % evil cluster ports and % type-10 ports',
        evil_count, type10_count;
END $$;
