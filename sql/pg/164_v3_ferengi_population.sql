-- V3 Ferengi population: retain the existing roster model, add a scalable
-- density setting and expand the candidate roster without resetting a game.
BEGIN;

INSERT INTO config (key, value, type)
VALUES ('ferengi.population.percent', '2', 'int')
ON CONFLICT (key) DO NOTHING;

COMMIT;
