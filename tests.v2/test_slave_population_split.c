#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "../src/slave_population.h"

static int64_t
sum (const int64_t *values, unsigned count)
{
  int64_t total = 0;
  for (unsigned i = 0; i < count; i++) total += values[i];
  return total;
}

int
main (void)
{
  int64_t buckets[5] = {2, 3, 5, 0, 0};
  int64_t loss[5] = {0};
  assert (slave_population_split_loss (buckets, 5, 5, loss));
  assert (loss[0] == 1 && loss[1] == 2 && loss[2] == 2);
  assert (loss[3] == 0 && loss[4] == 0 && sum (loss, 5) == 5);

  /* Equal remainders use stable assignment order, not randomness. */
  int64_t tied[5] = {1, 1, 1, 1, 1};
  assert (slave_population_split_loss (tied, 5, 2, loss));
  assert (loss[0] == 1 && loss[1] == 1 && loss[2] == 0);
  assert (loss[3] == 0 && loss[4] == 0 && sum (loss, 5) == 2);

  /* Complete loss and no-op loss preserve exact population totals. */
  assert (slave_population_split_loss (buckets, 5, 10, loss));
  assert (sum (loss, 5) == 10);
  assert (slave_population_split_loss (buckets, 5, 0, loss));
  assert (sum (loss, 5) == 0);

  assert (!slave_population_split_loss (buckets, 5, 11, loss));
  int64_t invalid[5] = {1, -1, 0, 0, 0};
  assert (!slave_population_split_loss (invalid, 5, 1, loss));
  puts ("slave population loss allocation passed");
  return 0;
}
