#ifndef ERRORS_H
#define ERRORS_H

/* Schema / client */
#define ERR_INVALID_SCHEMA     1300
#define ERR_BAD_REQUEST        1301
#define ERR_SCHEMA_NOT_FOUND   1306
#define ERR_DUPLICATE_REQUEST  1105

/* Auth */
#define ERR_NOT_AUTHENTICATED  1401	/* REFUSED if action requires auth */
#define ERR_INVALID_CREDENTIAL 1220
#define AUTH_ERR_IS_NPC	       1221	

/* Movement / rules */
#define REF_NO_WARP_LINK       1402	/* REFUSED */
#define REF_SAFE_ZONE_DENY     1403	/* REFUSED */
#define REF_NOT_ENOUGH_TURNS   1404	/* REFUSED */

/* Trade / rules */
#define REF_NOT_ENOUGH_CREDITS 1410	/* REFUSED */
#define REF_NOT_ENOUGH_HOLDS   1411	/* REFUSED */
#define REF_PORT_CLOSED        1412	/* REFUSED */

/* System */
#define ERR_DB                 1500
#define ERR_SERVICE_UNAVAILABLE 1102

/* Other */
#define ERR_NO_MEM              1501
#define ERR_BAD_STATE 	 	1502
#define ERR_DATABASE		1503
#endif
