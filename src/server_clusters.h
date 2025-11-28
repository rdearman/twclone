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

/* Public API */

/**
 * @brief Initializes the cluster system if it hasn't been already.
 * 
 * Checks for existing clusters. If none, generates Faction clusters
 * (Federation, Ferrengi, Orion) and a limited number of Random clusters
 * (approx 15% of total sectors).
 * 
 * @param db Database handle.
 * @return 0 on success (or already initialized), -1 on error.
 */
int clusters_init(sqlite3 *db);

/**
 * @brief Periodically runs the cluster economy logic.
 * 
 * Updates cluster-wide mid-prices for commodities and drifts individual
 * port prices towards the average.
 * 
 * @param db Database handle.
 * @param now_s Current timestamp.
 * @return 0 on success.
 */
int cluster_economy_step(sqlite3 *db, int64_t now_s);

/**
 * @brief Checks if a player is allowed to trade/dock at a port in the given sector.
 * 
 * @param db Database handle.
 * @param sector_id The sector where the port is located.
 * @param player_id The player attempting to dock.
 * @return 1 if allowed (or not in a cluster), 0 if banned.
 */
int cluster_can_trade(sqlite3 *db, int sector_id, int player_id);

/**
 * @brief Calculates the bust chance modifier for a player in a sector.
 * 
 * @param db Database handle.
 * @param sector_id The sector.
 * @param player_id The player.
 * @return A probability modifier (0.0 to 1.0+) based on wanted level/suspicion.
 */
double cluster_get_bust_modifier(sqlite3 *db, int sector_id, int player_id);

/**
 * @brief Records a crime event (robbery attempt/success/bust).
 * 
 * Updates suspicion, bust counts, and potentially bans the player in the cluster.
 * 
 * @param db Database handle.
 * @param sector_id The sector where the event occurred.
 * @param player_id The player involved.
 * @param success 1 if the robbery was successful (increases suspicion).
 * @param busted 1 if the player was busted (increases bust count/wanted level).
 */
void cluster_on_crime(sqlite3 *db, int sector_id, int player_id, int success, int busted);

/**
 * @brief Seeds illegal commodity stocks in ports based on cluster alignment.
 *
 * This function should be called once during server startup/initialization.
 * 
 * @param db Database handle.
 * @return 0 on success, -1 on error.
 */
int clusters_seed_illegal_goods(sqlite3 *db);

/**
 * @brief Periodically runs the black market economy logic for illegal commodities.
 * 
 * Adjusts prices and potentially stock for illegal goods in evil clusters.
 * 
 * @param db Database handle.
 * @param now_s Current timestamp.
 * @return 0 on success.
 */
int cluster_black_market_step(sqlite3 *db, int64_t now_s);

#endif /* SERVER_CLUSTERS_H */
