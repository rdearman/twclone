/**
 * @file sql_driver_test.c
 * @brief Minimal harness to print sql_driver output for visual verification.
 *
 * Build: gcc -I../src -o sql_driver_test sql_driver_test.c ../src/db/sql_driver.c -DDB_BACKEND_PG
 * Run:   ./sql_driver_test
 */

#include <stdio.h>
#include <string.h>
#include "db/sql_driver.h"

/* Stub db_backend() for standalone build */
db_backend_t db_backend(const db_t *db) {
    (void)db;
    return DB_BACKEND_POSTGRES;
}

int main(void) {
    char buf[512];
    int rc;

    printf("=== sql_driver PostgreSQL Output ===\n\n");

    /* Timestamp functions */
    printf("--- Timestamps ---\n");
    printf("sql_now_timestamptz:          %s\n", sql_now_timestamptz(NULL));
    printf("sql_epoch_now:                %s\n", sql_epoch_now(NULL));
    printf("sql_epoch_to_timestamptz_fmt: %s\n", sql_epoch_to_timestamptz_fmt(NULL));
    printf("sql_epoch_param_to_timestamptz: %s\n", sql_epoch_param_to_timestamptz(NULL));
    printf("\n");

    /* Conflict handling */
    printf("--- Conflict Handling ---\n");
    printf("sql_insert_ignore_clause:     %s\n", sql_insert_ignore_clause(NULL));
    printf("sql_conflict_target_fmt:      %s\n", sql_conflict_target_fmt(NULL));
    printf("\n");

    /* Locking */
    printf("--- Locking ---\n");
    printf("sql_for_update_skip_locked:   \"%s\"\n", sql_for_update_skip_locked(NULL));
    printf("\n");

    /* JSON functions */
    printf("--- JSON ---\n");
    printf("sql_json_object_fn:           %s\n", sql_json_object_fn(NULL));
    printf("sql_json_arrayagg_fn:         %s\n", sql_json_arrayagg_fn(NULL));
    printf("\n");

    /* Upsert DO UPDATE - all known intents */
    printf("--- Upsert DO UPDATE (all intents) ---\n");
    const char *intents[] = {
        "player_id",
        "player_id,key",
        "player_id,event_type",
        "player_id,name",
        "player_id,topic",
        "player_id,scope,key",
        "planet_id",
        "planet_id,commodity",
        "key",
        "port_id,player_id",
        "cluster_id,player_id",
        "corp_id,player_id",
        "notice_id,player_id",
        "draw_date",
        "entity_stock",
        "engine_deadletter",
        NULL
    };

    const char *sample_update = "value = EXCLUDED.value";

    for (int i = 0; intents[i] != NULL; i++) {
        rc = sql_upsert_do_update(NULL, intents[i], sample_update, buf, sizeof(buf));
        if (rc > 0) {
            printf("  %-25s -> %s\n", intents[i], buf);
        } else {
            printf("  %-25s -> ERROR (rc=%d)\n", intents[i], rc);
        }
    }

    /* Test unknown intent */
    printf("\n--- Unknown Intent (should fail) ---\n");
    rc = sql_upsert_do_update(NULL, "unknown_intent", sample_update, buf, sizeof(buf));
    printf("  %-25s -> %s (rc=%d)\n", "unknown_intent", rc < 0 ? "FAIL (expected)" : buf, rc);

    printf("\n=== Done ===\n");
    return 0;
}
