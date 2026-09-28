#ifndef REPO_TRADE_OFFERS_H
#define REPO_TRADE_OFFERS_H

#include "db/db_api.h"
#include <stdint.h>

typedef struct
{
  int64_t offer_id;
  int sender_player_id;
  int recipient_player_id;
  char commodity_code[16];
  char mode[8];
  int quantity;
  int64_t unit_price;
  char status[16];
  char created_at[40];
  char expires_at[40];
} trade_offer_t;

int repo_trade_offer_create (db_t *db, int sender_player_id,
                             int recipient_player_id,
                             const char *commodity_code, const char *mode,
                             int quantity, int64_t unit_price,
                             int64_t expires_at, const char *idempotency_key,
                             trade_offer_t *offer_out, int *created_out);
int repo_trade_offer_get (db_t *db, int64_t offer_id, int for_update,
                          trade_offer_t *offer_out);
int repo_trade_offer_find_idempotent (db_t *db, int sender_player_id,
                                      int recipient_player_id,
                                      const char *commodity_code,
                                      const char *mode, int quantity,
                                      int64_t unit_price,
                                      const char *idempotency_key,
                                      trade_offer_t *offer_out);
int repo_trade_offer_transition (db_t *db, int64_t offer_id,
                                 const char *status, int64_t now_s,
                                 int *changed_out);

#endif
