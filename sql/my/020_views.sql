-- Generated from PostgreSQL 020_views.sql -> MySQL
-- Basic sector views (needed for game logic)
CREATE OR REPLACE VIEW sector_degrees AS
WITH outdeg AS (
    SELECT
        s.sector_id,
        COUNT(w.to_sector) AS outdeg
    FROM
        sectors s
        LEFT JOIN sector_warps w ON w.from_sector = s.sector_id
    GROUP BY
        s.sector_id
),
indeg AS (
    SELECT
        s.sector_id,
        COUNT(w.from_sector) AS indeg
    FROM
        sectors s
        LEFT JOIN sector_warps w ON w.to_sector = s.sector_id
    GROUP BY
        s.sector_id
)
SELECT
    o.sector_id,
    o.outdeg,
    i.indeg
FROM
    outdeg o
    JOIN indeg i USING (sector_id);

CREATE OR REPLACE VIEW world_summary AS
WITH a AS (
    SELECT
        COUNT(*) AS sectors
    FROM
        sectors
),
b AS (
    SELECT
        COUNT(*) AS warps
    FROM
        sector_warps
),
c AS (
    SELECT
        COUNT(*) AS ports
    FROM
        ports
),
d AS (
    SELECT
        COUNT(*) AS planets
    FROM
        planets
),
e AS (
    SELECT
        COUNT(*) AS players
    FROM
        players
),
f AS (
    SELECT
        COUNT(*) AS ships
    FROM
        ships
)
SELECT
    a.sectors,
    b.warps,
    c.ports,
    d.planets,
    e.players,
    f.ships
FROM
    a,
    b,
    c,
    d,
    e,
    f;


-- Longest tunnels view for NPC homeworld placement
CREATE OR REPLACE VIEW longest_tunnels AS
WITH RECURSIVE all_sectors AS (
    SELECT from_sector AS id FROM sector_warps
    UNION
    SELECT to_sector AS id FROM sector_warps
),
outdeg AS (
    SELECT a.id, COALESCE(COUNT(w.to_sector), 0) AS deg
    FROM all_sectors a
    LEFT JOIN sector_warps w ON w.from_sector = a.id
    GROUP BY a.id
),
edges AS (
    SELECT from_sector, to_sector FROM sector_warps
),
entry AS (
    SELECT e.from_sector AS entry, e.to_sector AS next
    FROM edges e
    JOIN outdeg df ON df.id = e.from_sector AND df.deg > 1
    JOIN outdeg dn ON dn.id = e.to_sector AND dn.deg = 1
),
rec AS (
    SELECT entry, next AS curr, CAST(CONCAT(entry, '->', next) AS CHAR) AS path, 1 AS steps
    FROM entry
    UNION ALL
    SELECT r.entry, e.to_sector, CONCAT(r.path, '->', e.to_sector), r.steps + 1
    FROM rec r
    JOIN edges e ON e.from_sector = r.curr
    JOIN outdeg d ON d.id = r.curr AND d.deg = 1
    WHERE LOCATE(CONCAT('->', r.curr, '->'), r.path) = 0
)
SELECT r.entry AS entry_sector, r.curr AS exit_sector, r.path AS tunnel_path, r.steps AS tunnel_length_edges
FROM rec r
JOIN outdeg d_exit ON d_exit.id = r.curr
WHERE d_exit.deg <> 1 AND r.steps >= 2
ORDER BY r.steps DESC, r.entry, r.curr;
