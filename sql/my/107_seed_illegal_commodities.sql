-- Seed illegal goods for evil-cluster ports and black markets outside good
-- clusters. Run only after cluster_sectors has been populated.
INSERT INTO entity_stock (entity_type, entity_id, commodity_code, quantity, price)
SELECT
    'port',
    p.port_id,
    c.code,
    GREATEST(10, ROUND(p.size * 1000 * (0.20 + RAND() * 0.30))),
    c.base_price
FROM ports p
CROSS JOIN commodities c
WHERE c.illegal = TRUE
  AND (
    EXISTS (
      SELECT 1 FROM cluster_sectors cs
      JOIN clusters cl ON cs.cluster_id = cl.clusters_id
      WHERE cs.sector_id = p.sector_id AND cl.alignment <= 0
    )
    OR
    (p.`type` = 10 AND NOT EXISTS (
      SELECT 1 FROM cluster_sectors cs
      JOIN clusters cl ON cs.cluster_id = cl.clusters_id
      WHERE cs.sector_id = p.sector_id AND cl.alignment > 0
    ))
  )
ON DUPLICATE KEY UPDATE entity_id = entity_stock.entity_id;
