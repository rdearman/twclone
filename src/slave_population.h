#ifndef TWCLONE_SLAVE_POPULATION_H
#define TWCLONE_SLAVE_POPULATION_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* The tick and read-only production overview must apply exactly the same
 * server-authoritative slave modifier. */
static inline int64_t
slave_population_effective_output (int64_t normal_output,
                                   const char *commodity_code,
                                   bool is_slave_population,
                                   bool unrest_stopped)
{
  if (!is_slave_population)
    return normal_output;
  if (unrest_stopped)
    return 0;
  if (commodity_code
      && (strcmp (commodity_code, "ORE") == 0
          || strcmp (commodity_code, "ORG") == 0
          || strcmp (commodity_code, "EQU") == 0))
    return normal_output > 0 ? normal_output / 2 : 0;
  return normal_output;
}

/* Allocate a configured slave loss proportionally across fixed production
 * assignment buckets. Largest remainders are awarded in bucket order for
 * deterministic ties; the returned quantities sum exactly to loss. */
static inline bool
slave_population_split_loss (const int64_t *buckets, unsigned count,
                             int64_t loss, int64_t *out)
{
  if (!buckets || !out || count == 0 || loss < 0)
    return false;
  int64_t total = 0;
  for (unsigned i = 0; i < count; i++)
    {
      if (buckets[i] < 0 || total > INT64_MAX - buckets[i])
        return false;
      total += buckets[i];
      out[i] = 0;
    }
  if (loss > total)
    return false;
  if (loss == 0 || total == 0)
    return true;

  int64_t remainders[count];
  int64_t assigned = 0;
  for (unsigned i = 0; i < count; i++)
    {
      __int128 product = (__int128) buckets[i] * loss;
      out[i] = (int64_t) (product / total);
      remainders[i] = (int64_t) (product % total);
      assigned += out[i];
    }
  int64_t unallocated = loss - assigned;
  while (unallocated > 0)
    {
      unsigned best = count;
      for (unsigned i = 0; i < count; i++)
        if (out[i] < buckets[i]
            && (best == count || remainders[i] > remainders[best]))
          best = i;
      if (best == count)
        return false;
      out[best]++;
      remainders[best] = -1;
      unallocated--;
    }
  return true;
}

#endif
