#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "../src/planet_fighter_production.h"

int
main (void)
{
  planet_fighter_plan_t plan;

  /* No citadel gates weapons labor and resource consumption. */
  plan = planet_fighter_plan (0, 100, 10, 100, 0, 1000, 0, 10, -1);
  assert (plan.fighters_added == 0);
  assert (plan.equipment_consumed == 0);
  assert (plan.last_tick == -1);

  /* Citadel level 1 uses one fighterProduction bundle per fighter. */
  plan = planet_fighter_plan (1, 25, 10, 100, 0, 1000, 0, 10, -1);
  assert (plan.fighters_added == 2);
  assert (plan.equipment_consumed == 2);
  assert (plan.remainder == 5);
  assert (plan.last_tick == 10);

  /* Partial worker labor carries to the next 10-minute tick. */
  plan = planet_fighter_plan (1, 5, 10, 100, 0, 1000, 5, 11, 10);
  assert (plan.fighters_added == 1);
  assert (plan.equipment_consumed == 1);
  assert (plan.remainder == 0);

  /* Insufficient EQU prevents fighters and consumes no resources. */
  plan = planet_fighter_plan (1, 30, 10, 1, 0, 1000, 0, 12, 11);
  assert (plan.fighters_added == 1);
  assert (plan.equipment_consumed == 1);
  plan = planet_fighter_plan (1, 30, 10, 0, 0, 1000, 0, 13, 12);
  assert (plan.fighters_added == 0);
  assert (plan.equipment_consumed == 0);
  assert (plan.remainder == 0);

  /* Production respects capacity and spends EQU only for fighters added. */
  plan = planet_fighter_plan (1, 35, 10, 20, 9, 10, 0, 14, 13);
  assert (plan.fighters_added == 1);
  assert (plan.equipment_consumed == 1);
  assert (plan.remainder == 5);

  /* Replaying the same interval cannot award or consume twice. */
  plan = planet_fighter_plan (1, 35, 10, 20, 10, 10, 5, 14, 14);
  assert (plan.duplicate_tick);
  assert (plan.fighters_added == 0);
  assert (plan.equipment_consumed == 0);
  assert (plan.last_tick == 14);

  /* Zero class efficiency disables fighter manufacture. */
  plan = planet_fighter_plan (1, 35, 0, 20, 0, 1000, 0, 15, 14);
  assert (plan.fighters_added == 0);
  assert (plan.equipment_consumed == 0);

  puts ("planet fighter production arithmetic passed");
  return 0;
}
