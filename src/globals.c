/* src/globals.c */
#include "globals.h"
#include <time.h>
#include <stdatomic.h>

int threadid = 0;
int threadcount = 0;
int msgidin = 0;
int msgidout = 0;
int shutdown_flag = 0;
time_t next_process = 0;
armid_mine_config_t g_armid_config = { 0 };
xp_align_config_t g_xp_align = {.trade_xp_ratio = 10 };

atomic_int_fast64_t g_server_tick = 0;
atomic_int_fast64_t g_warps_performed = 0;
