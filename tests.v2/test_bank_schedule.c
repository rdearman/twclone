#define _GNU_SOURCE
#include "bank_schedule.h"

#include <assert.h>

static void
assert_month_date (int64_t epoch, int year, int month, int day,
                   int hour, int minute)
{
  time_t value = (time_t) epoch;
  struct tm utc;
  assert (gmtime_r (&value, &utc));
  assert (utc.tm_year + 1900 == year);
  assert (utc.tm_mon + 1 == month);
  assert (utc.tm_mday == day);
  assert (utc.tm_hour == hour);
  assert (utc.tm_min == minute);
}

int
main (void)
{
  assert_month_date (bank_schedule_monthly_at (2023, 2, 31, 4, 15),
                     2023, 2, 28, 4, 15);
  assert_month_date (bank_schedule_monthly_at (2024, 2, 31, 4, 15),
                     2024, 2, 29, 4, 15);
  assert_month_date (bank_schedule_monthly_at (2100, 2, 31, 4, 15),
                     2100, 2, 28, 4, 15);
  assert_month_date (bank_schedule_monthly_at (2000, 2, 31, 4, 15),
                     2000, 2, 29, 4, 15);
  assert_month_date (bank_schedule_monthly_at (2026, 4, 31, 23, 59),
                     2026, 4, 30, 23, 59);
  assert (bank_schedule_monthly_at (2026, 13, 1, 0, 0) == 0);
  assert (bank_schedule_monthly_at (2026, 1, 0, 0, 0) == 0);
  return 0;
}
