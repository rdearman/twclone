#ifndef DB_INT_H
#define DB_INT_H

#include "db_api.h"

/* Internal vtable structure */
typedef struct db_vt {
  void (*close)(db_t*);
  int  (*ping)(db_t*, db_err_t*);
  int  (*tx_begin)(db_t*, db_tx_mode_t, db_err_t*);
  int  (*tx_commit)(db_t*, db_err_t*);
  int  (*tx_rollback)(db_t*, db_err_t*);
  int  (*exec_params)(db_t*, const char*, const db_param_t*, int, db_err_t*);
} db_vt_t;

/* The opaque db handle implementation */
struct db {
  const db_vt_t *vt;
  void *impl;
  db_cfg_t cfg;
};

#endif /* DB_INT_H */
