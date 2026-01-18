#pragma once
#include "s2s_transport.h"
#include "db/db_api.h"


/* Loads the default active key from DB (or env override) and installs it.
   Returns 0 on success, -1 on error. */
int s2s_install_default_key (db_t * db);
/* If you want the raw key (without installing): */
int s2s_load_default_key (db_t * db, s2s_key_t * out_key);
int s2s_keyring_generate_key (db_t * db, const char *key_id_in,
			      const char *key_b64_in);
