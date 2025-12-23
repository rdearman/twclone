#ifndef DB_PG_H
#define DB_PG_H

#include "db_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* PG-specific config helpers and open function */
db_t *db_pg_open(const db_cfg_t *cfg, db_err_t *err);

#ifdef __cplusplus
}
#endif

#endif /* DB_PG_H */
