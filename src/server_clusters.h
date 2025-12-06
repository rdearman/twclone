#ifndef SERVER_CLUSTERS_H
#define SERVER_CLUSTERS_H
#include <sqlite3.h>
/* Constants */
#define CLUSTER_PRICE_ALPHA 0.1
#define CLUSTER_BAN_THRESHOLD 2
#define CLUSTER_SUSPICION_DECAY 0.9
#define CLUSTER_GOOD_MIN_ALIGN 1
#define CLUSTER_EVIL_MAX_ALIGN -1
#define PLAYER_EVIL_ALIGNMENT_THRESHOLD -100
/* Law Enforcement Status Structure */
typedef struct {
  int cluster_id;
  int player_id;
  int suspicion;
  int bust_count;
  int wanted_level;
  int banned;
} cluster_status_t;
int clusters_init (sqlite3 *db);
int cluster_economy_step (sqlite3 *db, int64_t now_s);
double cluster_get_bust_modifier (sqlite3 *db, int sector_id, int player_id);
void cluster_on_crime (sqlite3 *db,
                       int sector_id,
                       int player_id,
                       int success,
                       int busted);
int clusters_seed_illegal_goods (sqlite3 *db);
int cluster_black_market_step (sqlite3 *db, int64_t now_s);
int cluster_economy_step (sqlite3 *db, int64_t now_s);
int cluster_can_trade (sqlite3 *db, int sector_id, int player_id);
#endif /* SERVER_CLUSTERS_H */
