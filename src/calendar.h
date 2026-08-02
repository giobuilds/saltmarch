#ifndef CALENDAR_H
#define CALENDAR_H

/* =========================================================
 * calendar.h  --  the tick, said in dates  (LIFE_PLAN Phase 4)
 *
 * ONE CLOCK, NOT TWO. The needs tick IS the month. It is already the
 * economic heartbeat — where eating, growth and gold happen — already
 * integer, already hashed, and 300 sim ticks long. A calendar built on
 * anything else would be a second clock beating against the first, and
 * a player would eventually notice that the harvest and the month did
 * not line up.
 *
 * So this module stores NOTHING. A date is a pure function of
 * GameState.sim_tick_no: no new field, no new hashed state, and the UI
 * can ask for one from the tick its snapshot already carries.
 *
 *     month  = 300 sim ticks = 30 seconds = one needs tick
 *     day    = 1 second, 30 to the month, DECORATION ONLY
 *     season = 3 months
 *     year   = 12 months = 6 minutes
 *
 * WHY SIX-MINUTE YEARS. Cities: Skylines resolves the calendar problem
 * by compression — a citizen lives about six in-game years, a tenth of
 * a human life — and pays for it with a population that dies in
 * cohorts and a modding scene devoted to undoing the default.
 * Stellaris does not resolve it; it REMOVES it, by giving lifespans
 * only to a handful of named leaders while the mass never ages at all.
 * Its numbers are 30-day months, a 360-day year and one real second per
 * day, which is a year every six minutes.
 *
 * The assumption both of those expose is that a life must fit inside a
 * session. It does not. At six-minute years a 70-year life is seven
 * hours of play: you inherit people mid-life and they die on you at a
 * moment you did not choose, which is more dramatic than watching a
 * full arc rather than less.
 *
 * THE DAY IS DECORATION and is deliberately never a unit anything is
 * decided on. Stellaris keeps 30-day months precisely so monthly income
 * is uniform; the same reasoning applies here with more force, because
 * the month is not an approximation of the needs tick — it IS the needs
 * tick, and a rule that fired on days would have to explain what a
 * tenth of a needs tick means.
 * ========================================================= */

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
