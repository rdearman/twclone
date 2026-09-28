-- Ship personality precedence: ship override, corporation default, ship-type
-- default, then the engine's balanced fallback.
ALTER TABLE ships
    ADD COLUMN IF NOT EXISTS personality text
    CHECK (personality IS NULL OR personality IN
      ('balanced', 'offensive', 'defensive', 'looter', 'blockader'));

ALTER TABLE corporations
    ADD COLUMN IF NOT EXISTS ship_personality text
    CHECK (ship_personality IS NULL OR ship_personality IN
      ('balanced', 'offensive', 'defensive', 'looter', 'blockader'));

ALTER TABLE shiptypes
    ADD COLUMN IF NOT EXISTS default_personality text
    CHECK (default_personality IS NULL OR default_personality IN
      ('balanced', 'offensive', 'defensive', 'looter', 'blockader'));

-- Give the existing Orion fleet distinct persisted defaults. NULL means no
-- curated default and remains eligible for the balanced runtime fallback.
UPDATE shiptypes
   SET default_personality = CASE name
     WHEN 'Orion Heavy Fighter Patrol' THEN 'offensive'
     WHEN 'Orion Scout/Looter' THEN 'looter'
     WHEN 'Orion Contraband Runner' THEN 'looter'
     WHEN 'Orion Smuggler''s Kiss' THEN 'looter'
     WHEN 'Orion Black Market Guard' THEN 'blockader'
   END
 WHERE default_personality IS NULL
   AND name IN ('Orion Heavy Fighter Patrol', 'Orion Scout/Looter',
                'Orion Contraband Runner', 'Orion Smuggler''s Kiss',
                'Orion Black Market Guard');
