#include <assert.h>
#include "slave_unrest_cycle.h"

int
main (void)
{
  slave_unrest_cycle_t v;
  v = slave_unrest_cycle_evaluate (0, true, 1, 1);
  assert (v.level_after == 2 && v.probability_percent == 0 && !v.disrupted);
  v = slave_unrest_cycle_evaluate (9, true, 1, 2);
  assert (v.level_after == 11 && v.probability_percent == 1);
  v = slave_unrest_cycle_evaluate (50, false, 1, 3);
  assert (v.level_after == 49 && v.probability_percent == 4);
  v = slave_unrest_cycle_evaluate (100, true, 1, 4);
  assert (v.level_after == 100 && v.probability_percent == 10);
  v = slave_unrest_cycle_evaluate (0, false, 1, 5);
  assert (v.level_after == 0 && v.probability_percent == 0);
  v = slave_unrest_cycle_evaluate (100, true, 77, 999);
  slave_unrest_cycle_t retry = slave_unrest_cycle_evaluate (100, true, 77, 999);
  assert (v.roll_percent == retry.roll_percent && v.disrupted == retry.disrupted);
  for (int unrest = 0; unrest <= 100; unrest++)
    {
      v = slave_unrest_cycle_evaluate (unrest, true, 77, 1000 + unrest);
      assert (v.level_after >= 0 && v.level_after <= 100);
      assert (v.probability_percent == v.level_after / 10);
      assert (v.probability_percent <= 10);
    }
  return 0;
}
