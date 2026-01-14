#ifndef REPO_WARP_H
#define REPO_WARP_H

#include "db/db_api.h"

int repo_warp_get_high_degree_sector(db_t *db, int *sector_id_out);
int repo_warp_get_tunnel_end(db_t *db, int start_sector, int *end_sector_out);
int repo_warp_get_tunnels(db_t *db, int *start_sector1_out, int *start_sector2_out);
int repo_warp_delete_warp(db_t *db, int from_sector, int to_sector);
int repo_warp_insert_warp(db_t *db, int from_sector, int to_sector);
int repo_warp_get_bidirectional_warp(db_t *db, int *from_out, int *to_out);
int repo_warp_get_low_degree_sector(db_t *db, int *sector_id_out);
int repo_warp_delete_warps_from_sector(db_t *db, int from_sector);

#endif // REPO_WARP_H
