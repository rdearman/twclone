-- V3 illegal-goods access policy.
-- Neutral access is enabled, but a player must have reached the configured XP
-- threshold before illegal commodities become visible/tradeable.
BEGIN;

INSERT INTO config (key, value, type)
VALUES ('illegal_allowed_neutral', '1', 'bool')
ON CONFLICT (key) DO NOTHING;

INSERT INTO config (key, value, type)
VALUES ('illegal_min_experience', '400', 'int')
ON CONFLICT (key) DO NOTHING;

COMMIT;
