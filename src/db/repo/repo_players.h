#ifndef REPO_PLAYERS_H
#define REPO_PLAYERS_H

#include "db/db_api.h"

int repo_players_set_pref(db_t *db, int player_id, const char *key, const char *type, const char *value);
int repo_players_upsert_bookmark(db_t *db, int player_id, const char *name, int sector_id);
int repo_players_delete_bookmark(db_t *db, int player_id, const char *name);
int repo_players_add_avoid(db_t *db, int player_id, int sector_id);
int repo_players_delete_avoid(db_t *db, int player_id, int sector_id);
int repo_players_disable_subscription(db_t *db, int player_id, const char *topic);
int repo_players_upsert_subscription(db_t *db, int player_id, const char *topic, const char *delivery, const char *filter);
db_res_t* repo_players_get_prefs(db_t *db, int player_id, db_error_t *err);
db_res_t* repo_players_get_bookmarks(db_t *db, int player_id, db_error_t *err);
db_res_t* repo_players_get_avoids(db_t *db, int player_id, db_error_t *err);
db_res_t* repo_players_get_subscriptions(db_t *db, int player_id, db_error_t *err);
db_res_t* repo_players_get_my_info(db_t *db, int player_id, db_error_t *err);
db_res_t* repo_players_get_title_info(db_t *db, int player_id, db_error_t *err);
int repo_players_send_mail(db_t *db, int sender_id, int recipient_id, const char *subject, const char *message);
int repo_players_get_cargo_free(db_t *db, int player_id, int *free_out);
int repo_players_is_npc(db_t *db, int player_id, int *is_npc_out);
int repo_players_get_shiptype_by_name(db_t *db, const char *name, int *id, int *holds, int *fighters, int *shields);
int repo_players_insert_ship(db_t *db, const char *name, int type_id, int holds, int fighters, int shields, int sector_id, int *ship_id_out);
int repo_players_set_ship_ownership(db_t *db, int ship_id, int player_id);
int repo_players_update_ship_and_sector(db_t *db, int player_id, int ship_id, int sector_id);
int repo_players_update_podded_status(db_t *db, int player_id, const char *status);
int repo_players_get_credits(db_t *db, int player_id, long long *credits_out);
int repo_players_deduct_credits_returning(db_t *db, int player_id, long long amount, long long *new_balance_out);
int repo_players_add_credits_returning(db_t *db, int player_id, long long amount, long long *new_balance_out);
int repo_players_get_turns(db_t *db, int player_id, int *turns_out);
int repo_players_consume_turns(db_t *db, int player_id, int turns);
int repo_players_get_align_exp(db_t *db, int player_id, int *align_out, long long *exp_out);
int repo_players_update_align_exp(db_t *db, int player_id, int align, long long exp);
int repo_players_get_sector(db_t *db, int player_id, int *sector_out);
int repo_players_update_credits_safe(db_t *db, int player_id, long long delta, long long *new_balance_out);
int repo_players_check_exists(db_t *db, int player_id, int *exists_out);

/* Computer System / Knowledge */
typedef struct {
    int port_a_id;
    int port_b_id;
    int sector_a_id;
    int sector_b_id;
    char *commodity;
    int hops_between;
    int hops_from_player;
    int is_two_way;
} trade_route_t;

int repo_players_record_port_knowledge(db_t *db, int player_id, int port_id);
int repo_players_record_visit(db_t *db, int player_id, int sector_id);
int repo_players_get_recommended_routes(db_t *db, int player_id, int current_sector_id,
                                        int max_hops_between, int max_hops_from_player,
                                        int require_two_way,
                                        trade_route_t **out_routes, int *out_count,
                                        int *out_truncated, int *out_pairs_checked);
void repo_players_free_routes(trade_route_t *routes, int count);

#endif // REPO_PLAYERS_H
