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
