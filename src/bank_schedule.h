#ifndef TWCLONE_BANK_SCHEDULE_H
#define TWCLONE_BANK_SCHEDULE_H

#include <stdint.h>
#include <time.h>

/* Return the UTC instant for the requested monthly day, clamped to the last
 * calendar day when the month is shorter (including Gregorian leap years). */
static inline int64_t
bank_schedule_monthly_at (int year, int month_1_to_12, int requested_day,
                          int hour, int minute)
{
  static const int month_days[12] =
    {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month_1_to_12 < 1 || month_1_to_12 > 12 || requested_day < 1
      || requested_day > 31 || hour < 0 || hour > 23 || minute < 0
      || minute > 59)
    return 0;
  int days = month_days[month_1_to_12 - 1];
  if (month_1_to_12 == 2
      && (year % 4 == 0) && (year % 100 != 0 || year % 400 == 0))
    days = 29;
  struct tm utc = {0};
  utc.tm_year = year - 1900;
  utc.tm_mon = month_1_to_12 - 1;
  utc.tm_mday = requested_day < days ? requested_day : days;
  utc.tm_hour = hour;
  utc.tm_min = minute;
  return (int64_t) timegm (&utc);
}

#endif
