/* calendar.c  --  the tick, said in dates  (LIFE_PLAN Phase 4) */

#include "calendar.h"
#include <stdio.h>

/* Twelve months of a marsh year, three to a season. Named for what the
 * water and the light are doing rather than for anyone's gods, which
 * keeps them readable to a player who has been told nothing. */
static const char *const MONTH_NAMES[MONTHS_PER_YEAR] = {
    "Rimemelt", "Freshet",  "Greening",    /* Thaw    */
    "Longlight","Highsun",  "Swelter",     /* Green   */
    "Firstcut", "Goldfall", "Gleaning",    /* Harvest */
    "Rimefall", "Deepdark", "Hollow"       /* Frost   */
};

static const char *const SEASON_NAMES[SEASON_COUNT] = {
    "Thaw", "Green", "Harvest", "Frost"
};

void calendar_from_tick(uint64_t tick, Calendar *out)
{
    uint64_t into_month;

    if (!out) return;

    out->months = tick / CALENDAR_MONTH_TICKS;
    into_month  = tick % CALENDAR_MONTH_TICKS;

    /* Integer throughout. Nothing here is hashed — a date is derived,
     * never stored — but the same discipline applies anyway, because
     * two clients showing different dates for the same tick would be
     * a bug reported as a desync. */
    out->year   = (int)(out->months / MONTHS_PER_YEAR) + 1;
    out->month  = (int)(out->months % MONTHS_PER_YEAR);
    out->season = out->month / MONTHS_PER_SEASON;

    /* 1-based, and the multiply comes before the divide so a 300-tick
     * month splits into thirty even days rather than twenty-nine and a
     * long one. */
    out->day = (int)((into_month * DAYS_PER_MONTH) / CALENDAR_MONTH_TICKS) + 1;
}

const char *calendar_month_name(int month)
{
    if (month < 0 || month >= MONTHS_PER_YEAR) return "Rimemelt";
    return MONTH_NAMES[month];
}

const char *calendar_season_name(int season)
{
    if (season < 0 || season >= SEASON_COUNT) return "Thaw";
    return SEASON_NAMES[season];
}

void calendar_format(const Calendar *c, char *out, size_t out_len)
{
    if (!out || out_len == 0) return;
    if (!c) { out[0] = '\0'; return; }

    snprintf(out, out_len, "%s %d, Year %d",
             calendar_month_name(c->month), c->day, c->year);
}
