#pragma once
#include "db/db_api.h"
#include <time.h>

typedef struct {
  char peer_id[128];
  char host[256];
  int port;
  int enabled;
  char shared_key_id[128];
  time_t last_seen_at;  /* 0 if never seen */
  char notes[512];
  time_t created_at;
} s2s_peer_t;

/* List all peers */
int repo_s2s_peer_list(db_t *db, s2s_peer_t **out_peers, int *out_count);

/* Get a specific peer by peer_id */
int repo_s2s_peer_get(db_t *db, const char *peer_id, s2s_peer_t *out_peer);

/* Upsert (create or update) a peer */
int repo_s2s_peer_upsert(db_t *db, const s2s_peer_t *peer);

/* Set enabled flag */
int repo_s2s_peer_set_enabled(db_t *db, const char *peer_id, int enabled);

/* Touch last_seen_at to current time */
int repo_s2s_peer_touch_last_seen(db_t *db, const char *peer_id);

/* Nonce tracking: check if nonce was seen, then insert if not */
/* Returns 0 if nonce is new (inserted), -1 if already exists (replay) */
int repo_s2s_nonce_check_and_insert(db_t *db, const char *peer_id,
                                     const char *nonce, time_t msg_ts);

/* Clean up old nonces (older than age_seconds) */
int repo_s2s_nonce_cleanup(db_t *db, int age_seconds);
