#ifndef REPO_CLUSTERS_H
#define REPO_CLUSTERS_H

#include "db/db_api.h"
#include <stdint.h>

/* ========================================================================
   Cluster Info Structure (for Police Phase D)
   ======================================================================== */

typedef struct {
  int cluster_id;
  char role[64];           /* "FED", "RANDOM", "ORION", "FERENGI", etc. */
  int law_severity;
} cluster_info_t;

/* Crime type constants */
#define CRIME_ATTACK_PORT     1
#define CRIME_CONTRABAND      2

/* Enforcement thresholds */
#define ENFORCE_WANTED_THRESHOLD 2    /* Block dock if wanted_level >= 2 */
#define SUSPICION_WANTED_THRESHOLD 3  /* Promote to wanted if suspicion >= 3 (Phase B) */

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
int repo_clusters_check_incident_active(db_t *db, int cluster_id, int player_id, int *has_incident_out);
int repo_clusters_promote_wanted_from_suspicion(db_t *db, int cluster_id, int player_id);
int repo_clusters_clear_incident_state(db_t *db, int cluster_id, int player_id);
int repo_clusters_reduce_incident_by_tier(db_t *db, int cluster_id, int player_id);
int repo_clusters_apply_bribe_success(db_t *db, int cluster_id, int player_id);
int repo_clusters_apply_bribe_failure(db_t *db, int cluster_id, int player_id);
db_res_t* repo_clusters_get_all_ports(db_t *db, db_error_t *err);
int repo_clusters_get_alignment(db_t *db, int sector_id, int *alignment_out);
int repo_clusters_upsert_port_stock(db_t *db, int port_id, const char *commodity, int quantity, int64_t now_s);

/* ========================================================================
   Police Phase D: Crime Recording & Cluster Resolution
   ======================================================================== */

int repo_clusters_get_cluster_info_for_sector(db_t *db, int sector_id, cluster_info_t *info_out);

int repo_clusters_record_crime(db_t *db, int player_id, int sector_id, int crime_type, int severity_points);

#endif // REPO_CLUSTERS_H
