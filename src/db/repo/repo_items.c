#include "repo_items.h"
#include "db/db_api.h"
#include "../../../src/errors.h"
#include <string.h>
#include <limits.h>

bool
repo_items_get_by_code (db_t * db, const char *code, item_t * out_item)
{
  if (!db || !out_item || !code)
    return false;

  db_error_t err = { 0 };
  db_res_t *res = NULL;

  if (!db_query (db, "SELECT hardware_items_id, code, name, category, is_illegal, "
		      "COALESCE(min_alignment, -999999) AS min_align, "
		      "COALESCE(max_alignment, 999999) AS max_align "
		      "FROM hardware_items WHERE code = {1} AND enabled = true;",
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

  out_item->item_id = (int)db_res_col_i64 (res, 0, &err);
  strncpy (out_item->code, (const char *)db_res_col_text (res, 1, &err), sizeof (out_item->code) - 1);
  out_item->code[sizeof (out_item->code) - 1] = '\0';

  strncpy (out_item->name, (const char *)db_res_col_text (res, 2, &err), sizeof (out_item->name) - 1);
  out_item->name[sizeof (out_item->name) - 1] = '\0';

  strncpy (out_item->category, (const char *)db_res_col_text (res, 3, &err), sizeof (out_item->category) - 1);
  out_item->category[sizeof (out_item->category) - 1] = '\0';

  out_item->is_illegal = db_res_col_i64 (res, 4, &err) != 0;

  int min_align = (int)db_res_col_i64 (res, 5, &err);
  int max_align = (int)db_res_col_i64 (res, 6, &err);
  
  out_item->min_alignment = (min_align == -999999) ? INT_MIN : min_align;
  out_item->max_alignment = (max_align == 999999) ? INT_MAX : max_align;

  db_res_finalize (res);
  return true;
}

bool
repo_items_get_by_id (db_t * db, int item_id, item_t * out_item)
{
  if (!db || !out_item || item_id <= 0)
    return false;

  db_error_t err = { 0 };
  db_res_t *res = NULL;

  if (!db_query (db, "SELECT hardware_items_id, code, name, category, is_illegal, "
		      "COALESCE(min_alignment, -999999) AS min_align, "
		      "COALESCE(max_alignment, 999999) AS max_align "
		      "FROM hardware_items WHERE hardware_items_id = {1} AND enabled = true;",
		 (db_bind_t[]){ db_bind_i64 (item_id) }, 1, &res, &err))
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

  out_item->item_id = (int)db_res_col_i64 (res, 0, &err);
  strncpy (out_item->code, (const char *)db_res_col_text (res, 1, &err), sizeof (out_item->code) - 1);
  out_item->code[sizeof (out_item->code) - 1] = '\0';

  strncpy (out_item->name, (const char *)db_res_col_text (res, 2, &err), sizeof (out_item->name) - 1);
  out_item->name[sizeof (out_item->name) - 1] = '\0';

  strncpy (out_item->category, (const char *)db_res_col_text (res, 3, &err), sizeof (out_item->category) - 1);
  out_item->category[sizeof (out_item->category) - 1] = '\0';

  out_item->is_illegal = db_res_col_i64 (res, 4, &err) != 0;

  int min_align = (int)db_res_col_i64 (res, 5, &err);
  int max_align = (int)db_res_col_i64 (res, 6, &err);
  
  out_item->min_alignment = (min_align == -999999) ? INT_MIN : min_align;
  out_item->max_alignment = (max_align == 999999) ? INT_MAX : max_align;

  db_res_finalize (res);
  return true;
}

bool
repo_items_is_available_at_porttype (db_t * db, int item_id, int porttype_id,
                                      bool * out_can_buy, bool * out_can_sell)
{
  if (!db || item_id <= 0 || porttype_id <= 0)
    return false;

  db_error_t err = { 0 };
  db_res_t *res = NULL;

  if (!db_query (db, "SELECT can_buy, can_sell FROM porttype_items "
		      "WHERE hardware_items_id = {1} AND porttype_id = {2};",
		 (db_bind_t[]){ db_bind_i64 (item_id), db_bind_i64 (porttype_id) }, 2, &res, &err))
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

  if (out_can_buy)
    *out_can_buy = db_res_col_i64 (res, 0, &err) != 0;
  if (out_can_sell)
    *out_can_sell = db_res_col_i64 (res, 1, &err) != 0;

  db_res_finalize (res);
  return true;
}

int
repo_items_validate_legality (db_t * db, int item_id, 
                               int player_alignment,
                               int cluster_alignment)
{
  if (!db || item_id <= 0)
    return ERR_ITEM_NOT_FOUND;

  item_t item;
  if (!repo_items_get_by_id (db, item_id, &item))
    return ERR_ITEM_NOT_FOUND;

  /* If item is illegal, require evil alignment */
  if (item.is_illegal)
    {
      /* Allow if player is evil (alignment < 0) OR cluster allows illegal trade */
      /* For now: simple rule - only evil players can trade illegal items */
      if (player_alignment >= 0)
        return ERR_ITEM_ILLEGAL;
    }

  return 0;
}

int
repo_items_validate_alignment (db_t * db, int item_id, int player_alignment)
{
  if (!db || item_id <= 0)
    return ERR_ITEM_NOT_FOUND;

  item_t item;
  if (!repo_items_get_by_id (db, item_id, &item))
    return ERR_ITEM_NOT_FOUND;

  if (player_alignment < item.min_alignment || player_alignment > item.max_alignment)
    return ERR_ITEM_ALIGNMENT_RESTRICTED;

  return 0;
}
