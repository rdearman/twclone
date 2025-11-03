#ifndef ERRORS_H
#define ERRORS_H

/* ------------------------------------------------------------------
   Canonical error catalogue for envelopes.
   Use:
     - send_enveloped_error(..., CODE, "message")
     - send_enveloped_refused(..., CODE, "message", meta_optional)
   ------------------------------------------------------------------ */

/* 1100 — System / General */
#define ERR_UNKNOWN              1100
#define ERR_NOT_IMPLEMENTED      1101
#define ERR_SERVICE_UNAVAILABLE  1102
#define ERR_MAINTENANCE_MODE     1103
#define ERR_TIMEOUT              1104
#define ERR_DUPLICATE_REQUEST    1105	/* idempotency / unique constraint */
#define ERR_SERIALIZATION        1106
#define ERR_VERSION_NOT_SUPPORTED 1107
#define ERR_MESSAGE_TOO_LARGE    1108
#define ERR_RATE_LIMITED         1109
#define ERR_INSUFFICIENT_TURNS   1110
#define ERR_PERMISSION_DENIED    1111
#define ERR_NOT_FOUND	         1112
#define ERR_DB			 1113
#define ERR_OOM			 1114
#define ERR_DB_QUERY_FAILED	 1115
#define ERROR_INTERNAL		 1116


/* 1200 — Auth / Player */
#define ERR_NOT_AUTHENTICATED    1200	/* "auth required" */
#define ERR_INVALID_TOKEN        1201
#define ERR_TOKEN_EXPIRED        1202
#define ERR_SESSION_REVOKED      1203
#define ERR_USER_NOT_FOUND       1204
#define ERR_NAME_TAKEN           1205
#define ERR_WEAK_PASSWORD        1206
#define ERR_MFA_REQUIRED         1207
#define ERR_MFA_INVALID          1208
#define ERR_PLAYER_BANNED        1210
#define ERR_ALIGNMENT_RESTRICTED 1211
#define ERR_INVALID_CREDENTIAL   1220
#define ERR_REGISTRATION_DISABLED 1221
#define ERR_IS_NPC		 1222

/* 1300 — Validation / Quota */
#define ERR_INVALID_SCHEMA       1300	/* bad top-level shape */
#define ERR_MISSING_FIELD        1301
#define ERR_INVALID_ARG          1302	/* bad value/enum/type/range */
#define ERR_OUT_OF_RANGE         1303
#define ERR_LIMIT_EXCEEDED       1304	/* quota/cap reached */
#define ERR_TOO_MANY_BULK_ITEMS  1305
#define ERR_CURSOR_INVALID       1306
#define ERR_BAD_REQUEST          1306
#define ERR_REF_NO_TURNS         1307
#define ERR_CONFIRMATION_REQUIRED 1308

/* 1400 — Movement / Rules */
#define REF_NOT_IN_SECTOR        1400	/* REFUSED */
#define ERR_SECTOR_NOT_FOUND     1401
#define REF_NO_WARP_LINK         1402	/* REFUSED */
#define REF_TURN_COST_EXCEEDS    1403	/* REFUSED */
#define REF_AUTOPILOT_RUNNING    1404	/* REFUSED */
#define ERR_AUTOPILOT_PATH_INVALID 1405
#define REF_SAFE_ZONE_ONLY       1406	/* REFUSED */
#define REF_BLOCKED_BY_MINES     1407	/* REFUSED */
#define REF_TRANSWARP_UNAVAILABLE 1408	/* REFUSED */
#define ERR_BAD_STATE             1409	/* Current Sector Unknown */

/* 1500 — Planet / Citadel */
#define ERR_PLANET_NOT_FOUND     1500
#define REF_NOT_PLANET_OWNER     1501	/* REFUSED */
#define REF_LANDING_REFUSED      1502	/* defences */
#define ERR_CITADEL_REQUIRED     1503
#define ERR_CITADEL_MAX_LEVEL    1504
#define REF_INSUFFICIENT_RES     1505	/* ore/organics/equipment */
#define REF_TRANSFER_NOT_PERMITTED 1506
#define REF_GENESIS_DISABLED     1507

/* 1600 — Port / Stardock */
#define ERR_PORT_NOT_FOUND       1600
#define REF_PORT_OUT_OF_STOCK    1601
#define REF_PRICE_SLIPPAGE       1602
#define REF_DOCKING_REFUSED      1603
#define ERR_LICENSE_REQUIRED     1604
#define REF_PORT_BLACKLISTED     1605
#define REF_PORT_CLOSED          1606

/* 1700 — Trade */
#define ERR_COMMODITY_UNKNOWN    1700
#define REF_NOT_ENOUGH_HOLDS     1701
#define REF_NOT_ENOUGH_CREDITS   1702
#define ERR_OFFER_NOT_FOUND      1703
#define REF_OFFER_EXPIRED        1704
#define REF_OFFER_NOT_YOURS      1705
#define REF_TRADE_WINDOW_CLOSED  1706

/* 1800 — Ship / Combat */
#define ERR_SHIP_NOT_FOUND       1800
#define ERR_TARGET_INVALID       1801
#define REF_FRIENDLY_FIRE_BLOCKED 1802
#define REF_COMBAT_DISALLOWED    1803
#define REF_AMMO_DEPLETED        1804
#define REF_HULL_CRITICAL        1805
#define REF_MINE_LIMIT_EXCEEDED  1806
#define REF_DESTROYED_TERMINAL   1810

/* 1900 — Chat / Mail / Comms */
#define ERR_RECIPIENT_NOT_FOUND  1900
#define REF_MUTED_OR_BLOCKED     1901
#define REF_BROADCAST_FORBIDDEN  1902
#define REF_INBOX_FULL           1903
#define ERR_MESSAGE_TOO_LONG     1904

/* 2000 — S2S / Admin */
#define ERR_REPLICATION_LAG      2000
#define ERR_S2S_CONFLICT         2001
#define REF_ADMIN_ONLY           2002
#define ERR_SHARD_UNAVAILABLE    2003
#define ERR_CAPABILITY_DISABLED  2004

/* 3000 - Sector */
#define SECTOR_ERR		3000
#define ERR_SECTOR_OVERCROWDED 	3001
#define ERR_FORBIDDEN_IN_SECTOR   3002

/* 4000 - fedspace towing */
#define REASON_EVIL_ALIGN	4000
#define REASON_EXCESS_FIGHTERS	4001
#define REASON_HIGH_EXP		4002
#define REASON_NO_OWNER		4003
#define REASON_OVERCROWDING	4004


/* 9000+ — Vendor/private extensions available */

#endif /* ERRORS_H */
