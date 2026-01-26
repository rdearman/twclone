#ifndef REPO_SYSOP_H
#define REPO_SYSOP_H

#include "db/db_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * repo_sysop_audit
 *
 * Writes an entry to the engine_audit table.
 *
 * @param db          Database handle
 * @param actor_id    ID of the player performing the action
 * @param cmd_type    The command name (e.g. "sysop.config.set")
 * @param payload     JSON payload of the command (redacted/sanitized if needed)
 * @param note        Optional note provided by the admin
 * @param new_id_out  Optional pointer to receive the new audit ID
 * @return            0 on success, or DB error code
 */
int repo_sysop_audit(db_t *db, int actor_id, const char *cmd_type, const char *details_param, int64_t *new_id_out);

/*
 * repo_sysop_audit_tail
 *
 * Fetches recent audit logs.
 *
 * @param db          Database handle
 * @param limit       Max rows to return
 * @param err         Error pointer
 * @return            Result set or NULL
 */
db_res_t* repo_sysop_audit_tail(db_t *db, int limit, db_error_t *err);

/*
 * repo_sysop_search_players
 *
 * Searches for players by name (partial match).
 *
 * @param db          Database handle
 * @param query       Partial name to search for
 * @param limit       Max rows to return
 * @param err         Error pointer
 * @return            Result set (id, name, type, loggedin, sector_id) or NULL
 */
db_res_t* repo_sysop_search_players(db_t *db, const char *query, int limit, db_error_t *err);

/*
 * repo_sysop_get_player_basic
 *
 * Gets basic info for a player (for SysOp view).
 *
 * @param db          Database handle
 * @param player_id   Player ID
 * @param err         Error pointer
 * @return            Result set (id, name, credits, turns, sector, ship_id, type, is_npc, loggedin) or NULL
 */
db_res_t* repo_sysop_get_player_basic(db_t *db, int player_id, db_error_t *err);

/*
 * repo_sysop_get_player_sessions
 *
 * Gets session info for a player.
 * Since we don't have a full sessions history table, this queries the active 'sessions' table.
 *
 * @param db          Database handle
 * @param player_id   Player ID
 * @param limit       Max rows (though usually 1 per player if single session)
 * @param err         Error pointer
 * @return            Result set (token (masked), created, expires, ip_addr?) or NULL
 */
db_res_t* repo_sysop_get_player_sessions(db_t *db, int player_id, int limit, db_error_t *err);

/*
 * repo_sysop_get_universe_summary
 *
 * Returns aggregate counts for sectors, warps, ports, planets, players, ships.
 */
db_res_t* repo_sysop_get_universe_summary(db_t *db, db_error_t *err);

/* Phase 3: Engine & Jobs */

/*
 * repo_sysop_get_engine_status
 * 
 * Returns result set with engine offsets and stats.
 */
db_res_t* repo_sysop_get_engine_status(db_t *db, db_error_t *err);

/*
 * repo_sysop_list_jobs
 */
db_res_t* repo_sysop_list_jobs(db_t *db, int limit, db_error_t *err);

/*
 * repo_sysop_get_job
 */
db_res_t* repo_sysop_get_job(db_t *db, int64_t job_id, db_error_t *err);

/*
 * repo_sysop_retry_job
 */
int repo_sysop_retry_job(db_t *db, int64_t job_id);

/*
 * repo_sysop_cancel_job
 */
int repo_sysop_cancel_job(db_t *db, int64_t job_id);

#ifdef __cplusplus
}
#endif

#endif /* REPO_SYSOP_H */
