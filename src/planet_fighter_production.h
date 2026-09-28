#ifndef PLANET_FIGHTER_PRODUCTION_H
#define PLANET_FIGHTER_PRODUCTION_H

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
  int64_t fighters_added;
  int64_t remainder;
  int64_t equipment_consumed;
  int64_t last_tick;
  bool duplicate_tick;
} planet_fighter_plan_t;

static inline int64_t
planet_fighter_nonnegative (int64_t value)
{
  return value > 0 ? value : 0;
}

/* Military/weapons colonists contribute one worker-tick apiece. The class
 * fighterProduction value is worker-ticks required per fighter. A fighter
 * consumes one stored EQU unit. Remainders carry fractional worker-ticks;
 * complete unarmed work is discarded rather than banked as an unlimited queue.
 * tick_id is the stable 10-minute production interval identifier. */
static inline planet_fighter_plan_t
planet_fighter_plan (int64_t citadel_level, int64_t workers,
                     int64_t worker_ticks_per_fighter,
                     int64_t equipment_available, int64_t current_fighters,
                     int64_t fighter_capacity, int64_t old_remainder,
                     int64_t tick_id, int64_t last_tick)
{
  planet_fighter_plan_t plan = {.last_tick = last_tick};
  if (citadel_level < 1 || worker_ticks_per_fighter <= 0)
    return plan;

  int64_t remainder = old_remainder;
  if (remainder < 0)
    remainder = 0;
  remainder %= worker_ticks_per_fighter;
  plan.remainder = remainder;
  if (tick_id <= last_tick)
    {
      plan.duplicate_tick = true;
      return plan;
    }
  plan.last_tick = tick_id;

  current_fighters = planet_fighter_nonnegative (current_fighters);
  fighter_capacity = planet_fighter_nonnegative (fighter_capacity);
  if (current_fighters >= fighter_capacity)
    return plan;

  workers = planet_fighter_nonnegative (workers);
  equipment_available = planet_fighter_nonnegative (equipment_available);
  int64_t labor = INT64_MAX - workers < remainder
    ? INT64_MAX : workers + remainder;
  int64_t possible = labor / worker_ticks_per_fighter;
  int64_t room = fighter_capacity - current_fighters;
  int64_t affordable = equipment_available;
  plan.fighters_added = possible;
  if (plan.fighters_added > affordable) plan.fighters_added = affordable;
  if (plan.fighters_added > room) plan.fighters_added = room;
  plan.equipment_consumed = plan.fighters_added;
  plan.remainder = labor % worker_ticks_per_fighter;
  return plan;
}

#endif
