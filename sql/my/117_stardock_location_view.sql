-- Bring upgraded MySQL installations in line with sql/my/020_views.sql.
-- PostgreSQL already defines this view in its base view script.
CREATE OR REPLACE VIEW stardock_location AS
SELECT port_id, number, name, sector_id
FROM ports
WHERE type = 9 OR name LIKE '%Stardock%';
