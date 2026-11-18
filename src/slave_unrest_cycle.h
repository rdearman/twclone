#ifndef SLAVE_UNREST_CYCLE_H
#define SLAVE_UNREST_CYCLE_H

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
  int level_after;
  int probability_percent;
  int roll_percent;
  bool disrupted;
} slave_unrest_cycle_t;

/* Stable per-planet/per-production-cycle mixing keeps retries and concurrent
 * scheduler processes from drawing a different outcome for the same tick. */
static inline uint64_t
slave_unrest_cycle_mix (uint64_t value)
{
  value += UINT64_C (0x9e3779b97f4a7c15);
  value = (value ^ (value >> 30)) * UINT64_C (0xbf58476d1ce4e5b9);
  value = (value ^ (value >> 27)) * UINT64_C (0x94d049bb133111eb);
  return value ^ (value >> 31);
}

static inline slave_unrest_cycle_t
slave_unrest_cycle_evaluate (int level_before, bool slave_labor,
                             int64_t planet_id, int64_t tick_id)
{
  slave_unrest_cycle_t result = {0};
  if (level_before < 0) level_before = 0;
  if (level_before > 100) level_before = 100;
  result.level_after = level_before + (slave_labor ? 2 : -1);
  if (result.level_after < 0) result.level_after = 0;
  if (result.level_after > 100) result.level_after = 100;
  result.probability_percent = result.level_after / 10;
  if (result.probability_percent > 10) result.probability_percent = 10;
  uint64_t seed = ((uint64_t) planet_id << 32) ^ (uint64_t) tick_id;
  result.roll_percent = (int) (slave_unrest_cycle_mix (seed) % 100);
  result.disrupted = result.roll_percent < result.probability_percent;
  return result;
}

#endif
