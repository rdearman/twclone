#include <string.h>
#include <jansson.h>
#include "schemas.h"

/* ----- Capabilities ----- */
json_t *capabilities_build(void) {
    /* Keep it simple & static for now; you can wire real versions later */
    return json_pack("{s:s, s:{s:s, s:s, s:s}, s:o, s:o, s:o}",
        "server", "twclone/0.1-dev",
        "protocol", "version", "1.0", "min", "1.0", "max", "1.x",
        "namespaces", json_pack("[s,s,s,s,s,s,s,s,s,s,s,s,s,s]",
            "system","auth","player","move","trade","ship",
            "sector","combat","planet","citadel","chat","mail",
            "subscribe","bulk"),
        "limits", json_pack("{s:i, s:i}", "max_frame_bytes", 65536, "max_bulk", 50),
        "features", json_pack("{s:b, s:b, s:b, s:b, s:b}",
            "subscriptions", 1, "bulk", 1, "partial", 1, "idempotency", 1, "schemas", 1)
    );
}

/* ----- Minimal JSON Schemas (illustrative) ----- */

static json_t *schema_envelope(void) {
    return json_pack(
        "{s:s, s:s, s:s, s:{s:s}, s:s, s:o, s:o, s:o, s:o}",
        "$id", "ge://schema/envelope.json",
        "$schema", "https://json-schema.org/draft/2020-12/schema",
        "type", "object",
        "properties", "ts", "string",
        "oneOf", "[]",           /* keep super-minimal */
        "required", json_pack("[s]", "ts"),
        "additionalProperties", json_true(),
        "description", "Minimal envelope (server validates more internally)"
    );
}

static json_t *schema_auth_login(void) {
    return json_pack(
        "{s:s, s:s, s:s, s:{s:o}, s:o}",
        "$id", "ge://schema/auth.login.json",
        "$schema", "https://json-schema.org/draft/2020-12/schema",
        "type", "object",
        "properties", "data", json_pack("{s:{s:s, s:s}}",
            "properties",
                "user_name", "string",
                "password",  "string"
        ),
        "required", json_pack("[s,s]", "command", "data")
    );
}

static json_t *schema_move_warp(void) {
    return json_pack(
        "{s:s, s:s, s:s, s:{s:o}, s:o}",
        "$id", "ge://schema/move.warp.json",
        "$schema", "https://json-schema.org/draft/2020-12/schema",
        "type", "object",
        "properties", "data", json_pack("{s:{s:s}}",
            "properties",
                "to_sector_id", "integer"
        ),
        "required", json_pack("[s,s]", "command", "data")
    );
}

static json_t *schema_trade_buy(void) {
    return json_pack(
        "{s:s, s:s, s:s, s:{s:o}, s:o}",
        "$id", "ge://schema/trade.buy.json",
        "$schema", "https://json-schema.org/draft/2020-12/schema",
        "type", "object",
        "properties", "data", json_pack("{s:{s:s, s:s, s:s}}",
            "properties",
                "port_id",   "integer",
                "commodity", "string",
                "quantity",  "integer"
        ),
        "required", json_pack("[s,s]", "command", "data")
    );
}

/* Registry */
json_t *schema_get(const char *key) {
    if (!key) return NULL;
    if (strcmp(key, "envelope")    == 0) return schema_envelope();
    if (strcmp(key, "auth.login")  == 0) return schema_auth_login();
    if (strcmp(key, "move.warp")   == 0) return schema_move_warp();
    if (strcmp(key, "trade.buy")   == 0) return schema_trade_buy();
    /* Add more as you implement them */
    return NULL;
}

json_t *schema_keys(void) {
    return json_pack("[s,s,s,s]",
        "envelope","auth.login","move.warp","trade.buy");
}
