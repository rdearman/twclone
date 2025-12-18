#pragma once
#include <sqlite3.h>
#include "s2s_transport.h"

/* Loads the default active key from DB (or env override) and installs it.
   Returns 0 on success, -1 on error. */
int s2s_install_default_key (sqlite3 * db);
/* If you want the raw key (without installing): */
int s2s_load_default_key (sqlite3 * db, s2s_key_t * out_key);
int s2s_keyring_generate_key (sqlite3 * db, const char *key_id_in,
			      const char *key_b64_in);
