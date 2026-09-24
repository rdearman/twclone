#include "repo_porttypes.h"
#include "db/db_api.h"
#include <string.h>

bool
repo_porttypes_get_by_id (db_t * db, int porttype_id, porttype_t * out_porttype)
{
  if (!db || !out_porttype || porttype_id <= 0)
    return false;

  db_error_t err = { 0 };
  db_res_t *res = NULL;

  if (!db_query (db, "SELECT porttype_id, code, description, can_buy, can_sell, is_stardock, is_black_market "
		      "FROM porttypes WHERE porttype_id = {1};",
		 (db_bind_t[]){ db_bind_i64 (porttype_id) }, 1, &res, &err))
    {
      if (res)
        db_res_finalize (res);
      return false;
    }

  if (!res || db_res_step (res, &err) != 0)
    {
      if (res)
        db_res_finalize (res);
      return false;
    }

  out_porttype->porttype_id = (int)db_res_col_i64 (res, 0, &err);
  strncpy (out_porttype->code, (const char *)db_res_col_text (res, 1, &err), sizeof (out_porttype->code) - 1);
  out_porttype->code[sizeof (out_porttype->code) - 1] = '\0';

  const char *desc = (const char *)db_res_col_text (res, 2, &err);
  if (desc)
    strncpy (out_porttype->description, desc, sizeof (out_porttype->description) - 1);
  out_porttype->description[sizeof (out_porttype->description) - 1] = '\0';

  out_porttype->can_buy = db_res_col_i64 (res, 3, &err) != 0;
  out_porttype->can_sell = db_res_col_i64 (res, 4, &err) != 0;
  out_porttype->is_stardock = db_res_col_i64 (res, 5, &err) != 0;
  out_porttype->is_black_market = db_res_col_i64 (res, 6, &err) != 0;

  db_res_finalize (res);
  return true;
}

bool
repo_porttypes_get_by_code (db_t * db, const char *code, porttype_t * out_porttype)
{
  if (!db || !out_porttype || !code)
    return false;

  db_error_t err = { 0 };
  db_res_t *res = NULL;

  if (!db_query (db, "SELECT porttype_id, code, description, can_buy, can_sell, is_stardock, is_black_market "
		      "FROM porttypes WHERE code = {1};",
		 (db_bind_t[]){ db_bind_text (code) }, 1, &res, &err))
    {
      if (res)
        db_res_finalize (res);
      return false;
    }

  if (!res || db_res_step (res, &err) != 0)
    {
      if (res)
        db_res_finalize (res);
      return false;
    }

  out_porttype->porttype_id = (int)db_res_col_i64 (res, 0, &err);
  strncpy (out_porttype->code, (const char *)db_res_col_text (res, 1, &err), sizeof (out_porttype->code) - 1);
  out_porttype->code[sizeof (out_porttype->code) - 1] = '\0';

  const char *desc = (const char *)db_res_col_text (res, 2, &err);
  if (desc)
    strncpy (out_porttype->description, desc, sizeof (out_porttype->description) - 1);
  out_porttype->description[sizeof (out_porttype->description) - 1] = '\0';

  out_porttype->can_buy = db_res_col_i64 (res, 3, &err) != 0;
  out_porttype->can_sell = db_res_col_i64 (res, 4, &err) != 0;
  out_porttype->is_stardock = db_res_col_i64 (res, 5, &err) != 0;
  out_porttype->is_black_market = db_res_col_i64 (res, 6, &err) != 0;

  db_res_finalize (res);
  return true;
}

bool
repo_porttypes_is_stardock (db_t * db, int porttype_id)
{
  porttype_t pt;
  if (!repo_porttypes_get_by_id (db, porttype_id, &pt))
    return false;
  return pt.is_stardock;
}

bool
repo_porttypes_is_black_market (db_t * db, int porttype_id)
{
  porttype_t pt;
  if (!repo_porttypes_get_by_id (db, porttype_id, &pt))
    return false;
  return pt.is_black_market;
}

bool
repo_porttypes_get_capabilities (db_t * db, int porttype_id,
                                  bool * out_can_buy, bool * out_can_sell)
{
  porttype_t pt;
  if (!repo_porttypes_get_by_id (db, porttype_id, &pt))
    return false;

  if (out_can_buy)
    *out_can_buy = pt.can_buy;
  if (out_can_sell)
    *out_can_sell = pt.can_sell;

  return true;
}
