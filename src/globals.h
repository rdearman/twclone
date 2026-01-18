#ifndef GLOBALS_H
#define GLOBALS_H
#include <time.h>
#include <stdatomic.h>
#include "common.h"

extern int threadid;
extern int threadcount;
extern int msgidin;
extern int msgidout;
extern int shutdown_flag;
extern time_t next_process;
extern armid_mine_config_t g_armid_config;


typedef struct
{
  int trade_xp_ratio;
  int ship_destroy_xp_multiplier;
  int illegal_base_align_divisor;
  double illegal_align_factor_good;
  double illegal_align_factor_evil;
} xp_align_config_t;

extern xp_align_config_t g_xp_align;
extern atomic_int_fast64_t g_server_tick;
extern atomic_int_fast64_t g_warps_performed;

#define HASH_LENGTH 500
enum listtype
{ player, planet, port, ship };
struct list
{
  void *item;
  enum listtype type;
  struct list *listptr;
};

#endif
