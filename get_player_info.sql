SELECT
    p.id AS player_id,
    p.name AS player_name,
    p.credits AS credits_on_hand,
    COALESCE(ba.balance, 0) AS bank_credits,
    s.name AS current_sector_name,
    sh.sector AS current_sector_id,
    sh.holds AS cargo_current_holds,
    st.maxholds AS cargo_max_holds,
    sh.ore AS cargo_ore,
    sh.organics AS cargo_organics,
    sh.equipment AS cargo_equipment,
    sh.colonists AS cargo_colonists,
    sh.fighters AS cargo_fighters,
    sh.mines AS cargo_mines,
    sh.limpets AS cargo_limpets,
    sh.genesis AS cargo_genesis,
    sh.photons AS cargo_photons,
    sh.beacons AS cargo_beacons,
    CASE
        WHEN sh.ported = 1 THEN 'Docked at Port'
        WHEN sh.onplanet = 1 THEN 'Landed on Planet'
        WHEN sh.sector IS NOT NULL THEN 'In Space'
        ELSE 'Unknown'
    END AS ship_location_status
FROM
    players AS p
LEFT JOIN
    ships AS sh ON p.ship = sh.id
LEFT JOIN
    shiptypes AS st ON sh.type_id = st.id
LEFT JOIN
    bank_accounts AS ba ON p.id = ba.owner_id AND ba.owner_type = 'player'
LEFT JOIN
    sectors AS s ON sh.sector = s.id;