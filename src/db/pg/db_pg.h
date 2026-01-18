#ifndef DB_PG_H
#define DB_PG_H

#include "../db_api.h"

void *db_pg_open_internal(db_t *db, const db_config_t *cfg, db_error_t *err);

#endif /* DB_PG_H */
