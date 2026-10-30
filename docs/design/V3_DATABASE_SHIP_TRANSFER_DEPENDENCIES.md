# V3 PostgreSQL ship transfer dependencies

The database now enforces one primary owner per ship (`126_ship_primary_ownership.sql`). A sale or transfer repository routine must lock the ship row and its primary ownership row, verify the source player owns it, reject the player’s active ship unless the canonical command explicitly permits that operation, and verify both parties are in the required location before changing ownership.

Credit settlement must occur in the same transaction as ownership updates. The server contract must provide source player, target player/corporation, ship ID, agreed price, and location/port context. No generic transfer routine is added until those canonical rules are supplied.
