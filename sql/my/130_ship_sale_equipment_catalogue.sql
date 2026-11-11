-- V3-SHIP-002: canonical standard TradeWars 2002 consumable equipment values.
-- INSERT IGNORE preserves operator-customized catalogue rows.
INSERT IGNORE INTO hardware_items
    (code, name, price, requires_stardock, sold_in_class0, max_per_ship,
     category, enabled)
VALUES
    ('MINES',   'Armid Mine',             1000,  TRUE, FALSE, NULL, 'SPECIAL', TRUE),
    ('LIMPETS', 'Limpet Mine',           10000,  TRUE, FALSE, NULL, 'SPECIAL', TRUE),
    ('PROBE',   'Subspace Ether Probe',   3000,  TRUE, FALSE, NULL, 'SPECIAL', TRUE),
    ('PHOTONS', 'Photon Missile',        40000,  TRUE, FALSE, NULL, 'SPECIAL', TRUE),
    ('BEACONS', 'Marker Beacon',           100,  TRUE, FALSE, NULL, 'SPECIAL', TRUE);
