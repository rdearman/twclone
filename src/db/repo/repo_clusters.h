#ifndef REPO_CLUSTERS_H
#define REPO_CLUSTERS_H

#include "db/db_api.h"
#include <stdint.h>

int repo_clusters_get_sector_count(db_t *db, int *count_out);
int repo_clusters_get_cluster_for_sector(db_t *db, int sector_id, int *cluster_id_out);
int repo_clusters_create(db_t *db, const char *name, const char *role, const char *kind, int center_sector, int alignment, int law_severity, int *cluster_id_out);
int repo_clusters_add_sector(db_t *db, int cluster_id, int sector_id);
db_res_t* repo_clusters_get_warps(db_t *db, int from_sector, db_error_t *err);
int repo_clusters_check_sector_in_any_cluster(db_t *db, int sector_id, int *exists_out);
int repo_clusters_is_initialized(db_t *db, int *inited_out);
int repo_clusters_get_planet_sector(db_t *db, int num, int *sector_out);
int repo_clusters_get_clustered_count(db_t *db, int *count_out);
int repo_clusters_pick_random_unclustered_sector(db_t *db, int *sector_id_out);
db_res_t* repo_clusters_get_all(db_t *db, db_error_t *err);
int repo_clusters_get_avg_price(db_t *db, int cluster_id, const char *commodity, double *avg_price_out);
int repo_clusters_update_commodity_index(db_t *db, int cluster_id, const char *commodity, int mid_price);
int repo_clusters_drift_port_prices(db_t *db, int mid_price, const char *commodity, int cluster_id);
int repo_clusters_get_player_banned(db_t *db, int cluster_id, int player_id, int *banned_out);
int repo_clusters_get_player_suspicion_wanted(db_t *db, int cluster_id, int player_id, int *suspicion_out, int *wanted_out);
int repo_clusters_upsert_player_status(db_t *db, int cluster_id, int player_id, int susp_inc, int busted);
db_res_t* repo_clusters_get_all_ports(db_t *db, db_error_t *err);
int repo_clusters_get_alignment(db_t *db, int sector_id, int *alignment_out);
int repo_clusters_upsert_port_stock(db_t *db, int port_id, const char *commodity, int quantity, int64_t now_s);

#endif // REPO_CLUSTERS_H
