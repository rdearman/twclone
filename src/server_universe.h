/* src/server_universe.h */
#ifndef SERVER_UNIVERSE_H
#define SERVER_UNIVERSE_H
#include <jansson.h>
#include <stdbool.h>
#include "common.h"
#include "db/db_api.h"

int universe_init (void);
void universe_shutdown (void);

void fer_attach_db (db_t *db);
int fer_init_once (void);
void fer_tick (db_t *db, int64_t now_ms);

void ori_attach_db (db_t *db);
int ori_init_once (void);
void ori_tick (int64_t now_ms);

void iss_init (db_t *db);
void iss_tick (db_t *db, int64_t now_ms);
int iss_init_once (void);

int no_zero_ship (db_t *db, int set_sector, int ship_id);
int nav_next_hop (db_t *db, int start, int goal);
int nav_random_neighbor (db_t *db, int sector);
int h_warp_exists (db_t *db, int from, int to);
int h_check_interdiction (db_t *db, int sector_id, int player_id, int corp_id);
int sector_has_port (db_t *db, int sector);
int db_pick_adjacent (db_t *db, int sector);

int cmd_move_describe_sector (client_ctx_t *ctx, json_t *root);
int cmd_move_warp (client_ctx_t *ctx, json_t *root);
int cmd_move_pathfind (client_ctx_t *ctx, json_t *root);
int cmd_move_scan (client_ctx_t *ctx, json_t *root);
int cmd_move_transwarp (client_ctx_t *ctx, json_t *root);
void cmd_sector_scan (client_ctx_t *ctx, json_t *root);
void cmd_sector_scan_density (void *ctx_in, json_t *root);
void cmd_sector_info (client_ctx_t *ctx,
                      int fd,
                      json_t *root,
                      int sector_id,
                      int player_id);
int cmd_sector_search (client_ctx_t *ctx, json_t *root);
int cmd_sector_set_beacon (client_ctx_t *ctx, json_t *root);

json_t *build_sector_info_json (db_t *db, int sector_id);
json_t *build_sector_scan_json (db_t *db,
                                int sector_id,
                                int player_id,
                                bool holo_scanner_active);
json_t *make_player_object (int64_t player_id);

#endif