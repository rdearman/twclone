#ifndef GLOBALS_H
#define GLOBALS_H
#include <time.h>               // You need this for the time_t type
#include "common.h"             // Include common.h for armid_mine_config_t
extern int threadid;
extern int threadcount;
extern int msgidin;
extern int msgidout;
extern int shutdown_flag;
extern time_t next_process;
extern armid_mine_config_t g_armid_config;
// Struct for XP/Alignment configuration parameters
typedef struct {
  int trade_xp_ratio;                 // xp.trade_ratio
  int ship_destroy_xp_multiplier;     // xp.ship_destroy_multiplier
  int illegal_base_align_divisor;     // align.illegal_base_divisor
  double illegal_align_factor_good;   // align.illegal_band_factor_good
  double illegal_align_factor_evil;   // align.illegal_band_factor_evil
  // Add other XP/Alignment related config here as needed
} xp_align_config_t;
extern xp_align_config_t g_xp_align;
#define HASH_LENGTH 500
enum listtype
{
  player,
  planet,
  port,
  ship
};
struct list
{
  void *item;
  enum listtype type;
  struct list *listptr;
};
#endif // GLOBALS_H
