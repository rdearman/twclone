#ifndef ERRORS_H
#define ERRORS_H

/* ------------------------------------------------------------------
   Canonical error catalogue for envelopes.
   Use:
     - send_enveloped_error(..., CODE, "message")
     - send_enveloped_refused(..., CODE, "message", meta_optional)
   ------------------------------------------------------------------ */
/* 1100 — System / General */
#define ERR_UNKNOWN              	1100
#define ERR_NOT_IMPLEMENTED      	1101
#define ERR_SERVICE_UNAVAILABLE  	1102
#define ERR_MAINTENANCE_MODE     	1103
#define ERR_TIMEOUT              	1104
#define ERR_DUPLICATE_REQUEST    	1105   /* idempotency / unique constraint */
#define ERR_SERIALIZATION        	1106
#define ERR_VERSION_NOT_SUPPORTED 	1107
#define ERR_MESSAGE_TOO_LARGE    	1108
#define ERR_RATE_LIMITED         	1109
#define ERR_INSUFFICIENT_TURNS   	1110
#define ERR_PERMISSION_DENIED    	1111
#define ERR_NOT_FOUND            	1112
#define ERR_DB                   	1113
#define ERR_OOM                  	1114
#define ERR_DB_QUERY_FAILED      	1115
#define ERR_SERVER_ERROR         	1116
#define ERROR_INTERNAL           	1117
#define ERR_MEMORY               	1118

/* 1200 — Auth / Player */
#define ERR_NOT_AUTHENTICATED    	1200   /* "auth required" */
#define ERR_INVALID_TOKEN        	1201
#define ERR_TOKEN_EXPIRED        	1202
#define ERR_SESSION_REVOKED      	1203
#define ERR_USER_NOT_FOUND       	1204
#define ERR_NAME_TAKEN           	1205
#define ERR_WEAK_PASSWORD        	1206
#define ERR_MFA_REQUIRED         	1207
#define ERR_MFA_INVALID          	1208
#define ERR_NOT_IN_CORP          	1209
#define ERR_IS_NPC               	1210
#define ERR_PLAYER_BANNED        	1211
#define ERR_ALIGNMENT_RESTRICTED 	1212
#define ERR_REF_BIG_SLEEP        	1213
#define ERR_INVALID_CRED         	1214
#define ERR_REGISTRATION_DISABLED 	1215

/* 1300 — Validation / Quota */
#define ERR_INVALID_SCHEMA       	1300   /* bad top-level shape */
#define ERR_MISSING_FIELD        	1301
#define ERR_INVALID_ARG          	1302   /* bad value/enum/type/range */
#define ERR_OUT_OF_RANGE         	1303
#define ERR_LIMIT_EXCEEDED       	1304   /* quota/cap reached */
#define ERR_TOO_MANY_BULK_ITEMS  	1305
#define ERR_CURSOR_INVALID       	1306
#define ERR_BAD_REQUEST          	1307
#define ERR_REF_NO_TURNS         	1308
#define ERR_CONFIRMATION_REQUIRED 	1309
#define REF_INSUFFICIENT_CAPACITY 	1310

/* 1400 — Movement / Rules */
#define REF_NOT_IN_SECTOR        	1400   /* REFUSED */
#define ERR_SECTOR_NOT_FOUND     	1401
#define REF_NO_WARP_LINK         	1402   /* REFUSED */
#define REF_TURN_COST_EXCEEDS    	1403   /* REFUSED */
#define REF_AUTOPILOT_RUNNING    	1404   /* REFUSED */
#define ERR_AUTOPILOT_PATH_INVALID 	1405
#define REF_SAFE_ZONE_ONLY       	1406   /* REFUSED */
#define REF_BLOCKED_BY_MINES     	1407   /* REFUSED */
#define REF_TRANSWARP_UNAVAILABLE 	1408  /* REFUSED */
#define ERR_BAD_STATE             	1409  /* Current Sector Unknown */
#define REF_CANNOT_TRANSWARP_WHILE_TOWING 1410 /* REFUSED */
#define REF_SHIP_NOT_OWNED_OR_PILOTED 	1411 /* REFUSED */
#define REF_ALREADY_TOWING        	1412 /* REFUSED */
#define REF_ALREADY_BEING_TOWED   	1413 /* REFUSED */
#define REF_TARGET_SHIP_INVALID   	1414 /* REFUSED */
#define REF_TERRITORY_UNSAFE         	1415
#define ERR_FOREIGN_LIMPETS_PRESENT 	1416
#define REF_DEPLOY_TYPE_FIGHTERS    	1417
#define ERR_SECTOR_FIGHTER_CAP      	1418
#define TURN_CONSUME_ERROR_INVALID_AMOUNT 1419

/* 1500 — Planet / Citadel */
#define ERR_PLANET_NOT_FOUND     	1500
#define REF_NOT_PLANET_OWNER     	1501   /* REFUSED */
#define REF_LANDING_REFUSED      	1502   /* defences */
#define ERR_CITADEL_REQUIRED     	1503
#define ERR_CITADEL_MAX_LEVEL    	1504
#define REF_INSUFFICIENT_RES     	1505   /* ore/organics/equipment */
#define REF_TRANSFER_NOT_PERMITTED 	1506
#define REF_GENESIS_DISABLED     	1507

/* 1600 — Port / Stardock */
#define ERR_PORT_NOT_FOUND       	1600
#define REF_PORT_OUT_OF_STOCK    	1601
#define REF_PRICE_SLIPPAGE       	1602
#define REF_DOCKING_REFUSED      	1603
#define ERR_LICENSE_REQUIRED     	1604
#define REF_PORT_BLACKLISTED     	1605
#define REF_PORT_CLOSED          	1606

/* 1700 — Trade */
#define ERR_COMMODITY_UNKNOWN    	1700
#define REF_NOT_ENOUGH_HOLDS     	1701
#define ERR_INSUFFICIENT_FUNDS   	1702
#define ERR_OFFER_NOT_FOUND      	1703
#define REF_OFFER_EXPIRED        	1704
#define REF_OFFER_NOT_YOURS      	1705
#define REF_TRADE_WINDOW_CLOSED  	1706
#define ERR_COMMODITY_NOT_SOLD   	1707
#define ERR_PRICE_INVALID        	1708
#define ERR_TRADE_ILLEGAL_GOODS  	1709

/* 1800 — Ship / Combat */
#define ERR_SHIP_NOT_FOUND       	1800
#define ERR_TARGET_INVALID       	1801
#define REF_FRIENDLY_FIRE_BLOCKED 	1802
#define REF_COMBAT_DISALLOWED    	1803
#define REF_AMMO_DEPLETED        	1804
#define REF_HULL_CRITICAL        	1805
#define REF_MINE_LIMIT_EXCEEDED  	1806
#define REF_DESTROYED_TERMINAL   	1807

/* 1900 - Hardware Purchase Errors */
#define ERR_HARDWARE_NOT_AVAILABLE          1901
#define ERR_HARDWARE_INVALID_ITEM           1902
#define ERR_HARDWARE_INSUFFICIENT_FUNDS     1903
#define ERR_HARDWARE_CAPACITY_EXCEEDED      1904
#define ERR_HARDWARE_NOT_SUPPORTED_BY_SHIP  1905
#define ERR_HARDWARE_QUANTITY_INVALID       1906
#define ERR_NO_ACTIVE_SHIP                  1907 // NEW
#define ERR_NOT_AT_SHIPYARD                 1908
#define ERR_SHIPYARD_REQUIREMENTS_NOT_MET   1909
#define ERR_SHIPYARD_INSUFFICIENT_FUNDS     1910
#define ERR_SHIPYARD_INVALID_SHIP_TYPE      1911
#define ERR_SHIPYARD_SHIP_TYPE_NOT_AVAILABLE_HERE 1912
#define ERR_SHIPYARD_CAPACITY_MISMATCH      1913

/* 2000 - Tavern Errors */
#define ERR_NOT_AT_TAVERN                   2000
#define ERR_TAVERN_BET_TOO_HIGH             2001
#define ERR_TAVERN_DAILY_WAGER_EXCEEDED     2002
#define ERR_TAVERN_DYNAMIC_WAGER_EXCEEDED   2003
#define ERR_TAVERN_BET_ON_SELF              2004
#define ERR_TAVERN_PLAYER_NOT_FOUND         2005
#define ERR_TAVERN_TOO_HONORABLE            2006
#define ERR_TAVERN_LOAN_SHARK_DISABLED      2007
#define ERR_TAVERN_NO_LOAN                  2008
#define ERR_TAVERN_LOAN_OUTSTANDING         2009

/* 2100 — Chat / Mail / Comms */
#define ERR_RECIPIENT_NOT_FOUND  		2100
#define REF_MUTED_OR_BLOCKED     		2101
#define REF_BROADCAST_FORBIDDEN  		2102
#define REF_INBOX_FULL           		2103
#define ERR_MESSAGE_TOO_LONG     		2104

/* 2200 — S2S / Admin */
#define ERR_REPLICATION_LAG      	2200
#define ERR_S2S_CONFLICT         	2201
#define REF_ADMIN_ONLY           	2202
#define ERR_SHARD_UNAVAILABLE    	2203
#define ERR_CAPABILITY_DISABLED  	2204
#define ERR_LIMPETS_DISABLED     	2205
#define ERR_LIMPET_SWEEP_DISABLED 	2206
#define ERR_LIMPET_ATTACK_DISABLED 	2207

/* 2300 - Sector */
#define SECTOR_ERR              	2300
#define ERR_SECTOR_OVERCROWDED  	2301
#define ERR_FORBIDDEN_IN_SECTOR   	2302

/* 2400 - fedspace towing */
#define REASON_EVIL_ALIGN       	2400
#define REASON_EXCESS_FIGHTERS  	2401
#define REASON_HIGH_EXP         	2402
#define REASON_NO_OWNER         	2403
#define REASON_OVERCROWDING     	2404

/* 2500 - corporation errors */
#define ERR_INVALID_CORP_STATE  	2500

/* 2600 - Genesis Torpedo / Planet Creation */
#define ERR_GENESIS_DISABLED          2600
#define ERR_GENESIS_MSL_PROHIBITED    2601
#define ERR_GENESIS_SECTOR_FULL       2602
#define ERR_NO_GENESIS_TORPEDO        2603
#define ERR_INVALID_PLANET_NAME_LENGTH 2604
#define ERR_INVALID_OWNER_TYPE        2605
#define ERR_NO_CORPORATION            2606
#define ERR_UNIVERSE_FULL             2607

/* 9000+ — Vendor/private extensions available */


#endif /* ERRORS_H */
