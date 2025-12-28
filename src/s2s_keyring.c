#include <time.h>
#include <openssl/evp.h>        // EVP_DecodeBlock
#include <ctype.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
// local includes
#include "s2s_keyring.h"
#include "server_log.h"
#include "db/db_api.h"
#include "common.h"


/*
 * Generates and inserts a new S2S key into the database if none exists.
 * NOTE: Key ID and Key B64 should be securely generated in a production environment.
 */


int
s2s_keyring_generate_key (db_t *db,
                          const char *key_id_in,
                          const char *key_b64_in)
{
  /* Postgres uses $1, $2 syntax.
     We replace sqlite's strftime('%s') with a bound time(NULL) integer
     assuming 'created_ts' is a BIGINT/INTEGER column. */
  const char *SQL_INSERT =
    "INSERT INTO s2s_keys (key_id, key_b64, active, created_ts, is_default_tx) "
    "VALUES ($1, $2, 1, $3, 1);";

  db_bind_t params[] = {
    db_bind_text (key_id_in),
    db_bind_text (key_b64_in),
    db_bind_i64 ((long long)time (NULL))
  };

  db_error_t err;
  if (!db_exec (db, SQL_INSERT, params, 3, &err))
    {
      LOGE ("S2S_GEN: Failed to execute insert: %s\n", err.message);
      return -1;
    }

  return 0; // Success
}


static void
b64_compact (const char *in, char *out, size_t out_cap)
{
  size_t w = 0;
  for (const char *p = in; *p && w + 1 < out_cap; ++p)
    {
      if (!isspace ((unsigned char) *p))
        {
          out[w++] = *p;
        }
    }
  out[w] = '\0';
}


static int
b64_decode_strict (const char *in, unsigned char *out, size_t out_cap,
                   size_t *out_len)
{
  char tmp[512];
  b64_compact (in, tmp, sizeof (tmp));
  size_t in_len = strlen (tmp);


  if (in_len == 0 || (in_len & 3))
    {
      return -1;
    }
  int wrote =
    EVP_DecodeBlock (out, (const unsigned char *) tmp, (int) in_len);


  if (wrote < 0)
    {
      // fprintf (stderr, "[s2s] EVP_DecodeBlock failed for input: %s\n", tmp);
      return -1;
    }
  size_t pad = (in_len >= 1 && tmp[in_len - 1] == '=') + (in_len >= 2
                                                          && tmp[in_len -
                                                                 2] == '=');
  size_t actual = (size_t) wrote - pad;


  if (actual > out_cap)
    {
      return -1;
    }
  if (out_len)
    {
      *out_len = actual;
    }
  return 0;
}


int
s2s_load_default_key (db_t *db, s2s_key_t *out_key)
{
  if (!db || !out_key)
    {
      return -1;
    }

  /* ENV override for easy testing / container secrets */
  const char *env_id = getenv ("S2S_KEY_ID");
  const char *env_b64 = getenv ("S2S_KEY_B64");


  if (env_b64 && *env_b64)
    {
      memset (out_key, 0, sizeof(*out_key));
      strncpy (out_key->key_id,
               (env_id && *env_id) ? env_id : "env0",
               sizeof(out_key->key_id) - 1);

      size_t key_len = 0;


      if (b64_decode_strict (env_b64,
                             out_key->key,
                             sizeof(out_key->key),
                             &key_len) != 0 || key_len == 0)
        {
          return -1;
        }

      out_key->key_len = key_len;
      return 0;
    }

  /* DB lookup */
  const char *sql = "SELECT key_id, key_b64 FROM s2s_keys WHERE active = TRUE ORDER BY created_ts DESC LIMIT 1";


  for (int attempt = 0; attempt < 2; attempt++)
    {
      db_error_t err;


      db_error_clear (&err);

      db_res_t *res = NULL;


      if (!db_query (db, sql, NULL, 0, &res, &err))
        {
          LOGE ("s2s_load_default_key: query failed: %s (code=%d backend=%d)",
                err.message, err.code, err.backend_code);
          return -1;
        }

      if (db_res_step (res, &err))
        {
          /* Found a row */
          const char *kid = db_res_col_text (res, 0, &err);
          const char *kb64 = db_res_col_text (res, 1, &err);


          if (!kid || !kb64)
            {
              db_res_finalize (res);
              return -1;
            }

          memset (out_key, 0, sizeof(*out_key));
          strncpy (out_key->key_id, kid, sizeof(out_key->key_id) - 1);

          size_t key_len = 0;


          if (b64_decode_strict (kb64,
                                 out_key->key,
                                 sizeof(out_key->key),
                                 &key_len) != 0 || key_len == 0)
            {
              LOGE ("[s2s] base64 decode failed for key_id='%s'\n",
                    out_key->key_id);
              db_res_finalize (res);
              return -1;
            }

          out_key->key_len = key_len;
          db_res_finalize (res);
          return 0;
        }

      /* No row OR step error */
      if (err.code != 0)
        {
          LOGE ("s2s_load_default_key: step failed: %s (code=%d backend=%d)",
                err.message, err.code, err.backend_code);
          db_res_finalize (res);
          return -1;
        }

      /* No active key found */
      db_res_finalize (res);

      if (attempt == 0)
        {
          /* Attempt generation/recovery once */
          const char *new_key_id = "default_auto_gen_1";
          const char *new_key_b64 =
            "MTIzNDU2Nzg5MDEyMzQ1Njc4OTAxMjM0NTY3ODkwMTI=";


          if (s2s_keyring_generate_key (db, new_key_id, new_key_b64) != 0)
            {
              LOGE (
                "[s2s] FATAL: S2S key generation failed. Cannot proceed.\n");
              return -1;
            }

          /* loop and retry */
          continue;
        }

      /* Second attempt also found nothing */
      return -1;
    }

  return -1;
}


int
s2s_install_default_key (db_t *db)
{
  s2s_key_t k;
  if (s2s_load_default_key (db, &k) != 0)
    {
      return -1;
    }
  s2s_set_keyring (&k, 1);
  return 0;
}

