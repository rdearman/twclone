-- Correct the earlier population migration without deleting live traders,
-- ships, cargo, credits, offers or interaction history.
BEGIN;

CREATE TEMP TABLE ferengi_named_roster ON COMMIT DROP AS
WITH first_names(name) AS (
    VALUES ('Brak'), ('Drex'), ('Gaila'), ('Grilka'), ('Jaxa'), ('Krell'),
           ('Lorsa'), ('Mog'), ('Moogie'), ('Narek'), ('Odoz'), ('Prax'),
           ('Quark'), ('Riva'), ('Sarek'), ('Torga'), ('Ulek'), ('Vek'),
           ('Wex'), ('Xala'), ('Yorin'), ('Zek'), ('Arix'), ('Brel'),
           ('Crix'), ('Dala'), ('Erix'), ('Fara'), ('Grix'), ('Hava'),
           ('Irix'), ('Jora'), ('Kava'), ('Luro'), ('Mira'), ('Naxa'),
           ('Orik'), ('Pela'), ('Rax'), ('Sula')
), roles(code, label) AS (
    VALUES ('broker', 'the Broker'), ('arbitrageur', 'the Arbitrageur'),
           ('factor', 'the Factor'), ('merchant', 'the Merchant'),
           ('trader', 'the Trader'), ('dealmaker', 'the Dealmaker'),
           ('quartermaster', 'the Quartermaster')
)
SELECT row_number() OVER (ORDER BY f.name, r.code)::integer AS roster_number,
       'ferengi_' || lower(f.name) || '_' || r.code AS trader_code,
       f.name || ' ' || r.label AS display_name
FROM first_names f CROSS JOIN roles r
ORDER BY f.name, r.code
LIMIT 247;

-- Migration 164 may already have created legacy placeholder rows.  Replace
-- their definitions with named definitions while retaining the existing
-- ferengi_trader_id values and all history keyed to those IDs.
CREATE TEMP TABLE ferengi_legacy_map ON COMMIT DROP AS
SELECT old.trader_code AS old_code,
       named.trader_code AS new_code,
       named.display_name AS new_name
FROM (
    SELECT trader_code,
           row_number() OVER (ORDER BY trader_code)::integer AS roster_number
    FROM ferengi_trader_definitions
    WHERE trader_code LIKE 'roster_%'
) old
JOIN ferengi_named_roster named USING (roster_number);

INSERT INTO ferengi_trader_definitions
    (trader_code, display_name, ship_type_id, active)
SELECT m.new_code, m.new_name, d.ship_type_id, d.active
FROM ferengi_legacy_map m
JOIN ferengi_trader_definitions d ON d.trader_code = m.old_code
ON CONFLICT (trader_code) DO NOTHING;

UPDATE ferengi_trader_definition_cargo c
SET trader_code = m.new_code
FROM ferengi_legacy_map m
WHERE c.trader_code = m.old_code;

UPDATE ferengi_traders t
SET trader_code = m.new_code,
    display_name = m.new_name,
    updated_at = CURRENT_TIMESTAMP
FROM ferengi_legacy_map m
WHERE t.trader_code = m.old_code;

DELETE FROM ferengi_trader_definitions d
USING ferengi_legacy_map m
WHERE d.trader_code = m.old_code;

-- Fresh databases, and databases where 164 was corrected before application,
-- still receive the same named roster through the canonical definitions table.
INSERT INTO ferengi_trader_definitions (trader_code, display_name, ship_type_id)
SELECT r.trader_code, r.display_name, st.shiptypes_id
FROM ferengi_named_roster r
JOIN shiptypes st ON st.name = 'Merchant Freighter'
ON CONFLICT (trader_code) DO NOTHING;

-- Copy the canonical starting cargo used by the original named traders.
INSERT INTO ferengi_trader_definition_cargo (trader_code, commodity_code, quantity)
SELECT r.trader_code, c.commodity_code, c.quantity
FROM ferengi_named_roster r
JOIN ferengi_trader_definition_cargo c ON c.trader_code = 'gaila'
ON CONFLICT (trader_code, commodity_code) DO NOTHING;

-- Newly instantiated traders are distributed across eligible sectors by the
-- server.  Rebalance only the legacy placeholder population here; preserve
-- the locations of the original three named traders.
UPDATE ships s
SET sector_id = (
    SELECT sector_id FROM sectors
    WHERE sector_id > 10
    ORDER BY random()
    LIMIT 1
)
FROM ferengi_traders t
JOIN ferengi_named_roster r ON r.trader_code = t.trader_code
WHERE s.ship_id = t.ship_id
  AND t.active = TRUE;

-- Correct only values that exceed the ship type's authoritative capacities.
-- This does not refill lost equipment or overwrite valid gameplay state.
UPDATE ships s
SET fighters = LEAST(COALESCE(s.fighters, 0), COALESCE(st.maxfighters, 0)),
    mines = LEAST(COALESCE(s.mines, 0), COALESCE(st.maxmines, 0)),
    shields = LEAST(COALESCE(s.shields, 0), COALESCE(st.maxshields, 0))
FROM ferengi_traders t, shiptypes st
WHERE t.ship_id = s.ship_id
  AND (COALESCE(s.fighters, 0) > COALESCE(st.maxfighters, 0)
       OR COALESCE(s.mines, 0) > COALESCE(st.maxmines, 0)
       OR COALESCE(s.shields, 0) > COALESCE(st.maxshields, 0));

COMMIT;
