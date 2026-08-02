#ifndef CALENDAR_H
#define CALENDAR_H

/* calendar.h  --  the tick, said in dates  (LIFE_PLAN Phase 4) */

#include "simclock.h"
#include <stddef.h>
#include <stdint.h>

/* A month is one needs tick. Written in terms of the needs interval
 * rather than as 300, so the two cannot drift apart: if the economy's
 * heartbeat is ever retuned, the calendar follows it by construction. */
#define CALENDAR_MONTH_TICKS  \
    ((uint64_t)(30.0f * SIM_TICKS_PER_SEC))
#define DAYS_PER_MONTH        30
#define MONTHS_PER_SEASON     3
#define MONTHS_PER_YEAR       12
#define SEASONS_PER_YEAR      (MONTHS_PER_YEAR / MONTHS_PER_SEASON)

typedef enum {
    SEASON_THAW = 0,
    SEASON_GREEN,
    SEASON_HARVEST,
    SEASON_FROST,
    SEASON_COUNT
} Season;

typedef struct {
    uint64_t months;   /* months since the world began, 0-based        */
    int      year;     /* 1-based, so a new world begins in Year 1     */
    int      month;    /* 0..11                                        */
    int      day;      /* 1..30 — decoration                           */
    int      season;   /* Season                                       */
} Calendar;

/* The date at `tick`. Total function: every tick is some date. */
void calendar_from_tick(uint64_t tick, Calendar *out);

/* Names. Never NULL, even out of range. */
const char *calendar_month_name(int month);
const char *calendar_season_name(int season);

/* "Gleaning 14, Year 3" into `out`. Truncates rather than overruns. */
void calendar_format(const Calendar *c, char *out, size_t out_len);

#endif /* CALENDAR_H */
