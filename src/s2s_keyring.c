#include "s2s_keyring.h"
#include <openssl/evp.h>	// EVP_DecodeBlock
#include <ctype.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static void
b64_compact (const char *in, char *out, size_t out_cap)
{
  size_t w = 0;
  for (const char *p = in; *p && w + 1 < out_cap; ++p)
    if (!isspace ((unsigned char) *p))
      out[w++] = *p;
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
    return -1;
  int wrote =
    EVP_DecodeBlock (out, (const unsigned char *) tmp, (int) in_len);
  if (wrote < 0)
    return -1;
  size_t pad = (in_len >= 1 && tmp[in_len - 1] == '=') + (in_len >= 2
							  && tmp[in_len -
								 2] == '=');
  size_t actual = (size_t) wrote - pad;
  if (actual > out_cap)
    return -1;
  if (out_len)
    *out_len = actual;
  return 0;
}

int
s2s_load_default_key (sqlite3 *db, s2s_key_t *out_key)
{
  if (!db || !out_key)
    return -1;

  /* ENV override for easy testing / container secrets */
  const char *env_id = getenv ("S2S_KEY_ID");
  const char *env_b64 = getenv ("S2S_KEY_B64");
  if (env_b64 && *env_b64)
    {
      memset (out_key, 0, sizeof (*out_key));
      strncpy (out_key->key_id, env_id
	       && *env_id ? env_id : "env0", sizeof (out_key->key_id) - 1);
      size_t key_len = 0;
      if (b64_decode_strict
	  (env_b64, out_key->key, sizeof (out_key->key), &key_len) != 0
	  || key_len == 0)
	{
	  fprintf (stderr, "[s2s] ENV base64 decode failed\n");
	  return -1;
	}
      out_key->key_len = key_len;
      return 0;
    }

  /* DB lookup */
  static const char *SQL =
    "SELECT key_id, key_b64 "
    "FROM s2s_keys WHERE active=1 "
    "ORDER BY is_default_tx DESC, created_ts ASC LIMIT 1;";
  sqlite3_stmt *st = NULL;
  int rc = sqlite3_prepare_v2 (db, SQL, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      fprintf (stderr, "[s2s] key prepare failed: %s\n", sqlite3_errmsg (db));
      return -1;
    }
  rc = sqlite3_step (st);
  if (rc != SQLITE_ROW)
    {
      fprintf (stderr, "[s2s] no active key in s2s_keys\n");
      sqlite3_finalize (st);
      return -1;
    }
  const unsigned char *kid = sqlite3_column_text (st, 0);
  const unsigned char *kb64 = sqlite3_column_text (st, 1);
  if (!kid || !kb64)
    {
      sqlite3_finalize (st);
      return -1;
    }

  memset (out_key, 0, sizeof (*out_key));
  strncpy (out_key->key_id, (const char *) kid, sizeof (out_key->key_id) - 1);
  size_t key_len = 0;
  if (b64_decode_strict
      ((const char *) kb64, out_key->key, sizeof (out_key->key),
       &key_len) != 0 || key_len == 0)
    {
      fprintf (stderr, "[s2s] base64 decode failed for key_id='%s'\n",
	       out_key->key_id);
      sqlite3_finalize (st);
      return -1;
    }
  out_key->key_len = key_len;
  sqlite3_finalize (st);
  return 0;
}

int
s2s_install_default_key (sqlite3 *db)
{
  s2s_key_t k;
  if (s2s_load_default_key (db, &k) != 0)
    return -1;
  s2s_set_keyring (&k, 1);
  return 0;
}
