#ifndef DB_MYSQL_H
#define DB_MYSQL_H

/* MySQL backend (Phase 6A skeleton).
 *
 * Phase 6A constraints:
 * - Backend may be stub (ERR_NOT_IMPLEMENTED) but must compile/link.
 * - No gameplay logic changes.
 */

#include "db_api.h" /* db_t, db_config_t, db_error_t */

#ifdef __cplusplus
extern "C" {
#endif

/* Internal open hook called by db_open() in db_api.c.
 * Returns an opaque impl pointer stored in db->impl, or NULL on failure.
 * On success, MUST set db->vt to a valid vtable (even if stubs).
 */
void *db_mysql_open_internal(db_t *db, const db_config_t *cfg, db_error_t *err);

#ifdef __cplusplus
}
#endif

#endif /* DB_MYSQL_H */
