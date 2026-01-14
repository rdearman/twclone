#ifndef REPO_MAIN_H
#define REPO_MAIN_H

#include "db/db_api.h"
#include <stdint.h>

int repo_main_get_sector_count(db_t *db, int32_t *out_count);
int repo_main_get_warp_count(db_t *db, int32_t *out_count);
int repo_main_get_port_count(db_t *db, int32_t *out_count);

#endif
