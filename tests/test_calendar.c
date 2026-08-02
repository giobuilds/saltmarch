/*  test_calendar.c  --  the tick, said in dates  (LIFE_PLAN Phase 4)
 *
 * The calendar stores nothing: a date is a pure function of the sim
 * tick. So these assertions are about the ARITHMETIC and about the one
 * property the whole design rests on —
 *
 *     ONE MONTH IS ONE NEEDS TICK IS ONE WORK CYCLE.
 *
 * That alignment is the entire reason the shift durations were retuned
 * in this phase. If it ever stops holding, an island's harvest and its
 * calendar drift apart and a player is told two different things about
 * the same world, so it is asserted directly against the constants the
 * sim actually uses rather than against 300.
 *
 * Linked against the sim alone: no SDL, no UI.
 */

#include "calendar.h"
#include "population.h"
#include "agent.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond, msg) do {                                          \
        if (!(cond)) { printf("  FAIL: %s\n", (msg)); failures++; }     \
        else         { printf("  ok:   %s\n", (msg)); }                 \
    } while (0)

/* ---- 1. THE HEADLINE: one clock, not several -------------- */
static void test_one_clock(void)
{
    printf("\n=== one month, one needs tick, one shift ===\n");

    CHECK(CALENDAR_MONTH_TICKS == (uint64_t)NEEDS_INTERVAL_TICKS,
          "a month is exactly the needs tick — eating IS the month");

    /* The retune this phase exists to carry. 60 + 15 was a 75-second
     * cycle against a 30-second needs tick: two periods drifting past
     * each other, and the reason an island's output was about four
     * fifths of its headcount. */
    {
        float cycle = AGENT_SHIFT_DURATION + AGENT_REST_DURATION;
        char  msg[128];

        snprintf(msg, sizeof(msg),
                 "and one work cycle (%.0fs + %.0fs) is one month too",
                 (double)AGENT_SHIFT_DURATION, (double)AGENT_REST_DURATION);
        CHECK((uint64_t)(cycle * SIM_TICKS_PER_SEC) == CALENDAR_MONTH_TICKS,
              msg);
    }

    CHECK(AGENT_SHIFT_DURATION > AGENT_REST_DURATION,
          "people still work more of the month than they rest");
}

/* ---- 2. the arithmetic ------------------------------------ */
static void test_dates(void)
{
    Calendar c;

    printf("\n=== what the tick says ===\n");

    calendar_from_tick(0, &c);
    CHECK(c.year == 1 && c.month == 0 && c.day == 1,
          "a new world begins on the first day of Year 1");
    CHECK(c.season == SEASON_THAW, "which is in the Thaw");

    /* One month in. */
    calendar_from_tick(CALENDAR_MONTH_TICKS, &c);
    CHECK(c.months == 1 && c.month == 1 && c.day == 1,
          "one needs tick later it is the first of the second month");

    /* One year. */
    calendar_from_tick(CALENDAR_MONTH_TICKS * MONTHS_PER_YEAR, &c);
    CHECK(c.year == 2 && c.month == 0,
          "twelve months later it is Year 2");

    /* The day walks the whole month and never overflows it — the
     * multiply-before-divide, which is what stops a 300-tick month
     * being twenty-nine short days and one long one. */
    {
        uint64_t t;
        int      seen_first = 0, seen_last = 0, bad = 0;

        for (t = 0; t < CALENDAR_MONTH_TICKS; t++) {
            calendar_from_tick(t, &c);
            if (c.day < 1 || c.day > DAYS_PER_MONTH) bad++;
            if (c.day == 1) seen_first = 1;
            if (c.day == DAYS_PER_MONTH) seen_last = 1;
        }
        CHECK(bad == 0, "every tick of a month lands on a real day");
        CHECK(seen_first && seen_last,
              "and the month runs from the first to the thirtieth");
    }

    /* Seasons: three months each, four to the year, in order. */
    {
        int m, wrong = 0;
        for (m = 0; m < MONTHS_PER_YEAR; m++) {
            calendar_from_tick(CALENDAR_MONTH_TICKS * (uint64_t)m, &c);
            if (c.season != m / MONTHS_PER_SEASON) wrong++;
        }
        CHECK(wrong == 0, "three months to a season, four to the year");
    }
}

/* ---- 3. it says it in words ------------------------------- */
static void test_names(void)
{
    Calendar c;
    char     buf[64];
    int      m, empty = 0, dup = 0, i;

    printf("\n=== and says it in words ===\n");

    for (m = 0; m < MONTHS_PER_YEAR; m++) {
        const char *n = calendar_month_name(m);
        if (!n || !n[0]) empty++;
        for (i = 0; i < m; i++)
            if (strcmp(n, calendar_month_name(i)) == 0) dup++;
    }
    CHECK(empty == 0 && dup == 0, "twelve months, each with its own name");

    CHECK(calendar_month_name(-1) && calendar_month_name(99),
          "and an impossible month still answers with something");
    CHECK(calendar_season_name(-1) && calendar_season_name(99),
          "as does an impossible season");

    calendar_from_tick(CALENDAR_MONTH_TICKS * 8 + 40, &c);
    calendar_format(&c, buf, sizeof(buf));
    CHECK(strstr(buf, "Gleaning") != NULL && strstr(buf, "Year 1") != NULL,
          "a date reads like a date");
    printf("        (\"%s\")\n", buf);

    /* Truncation rather than overrun — this runs under ASan in CI. */
    {
        char tiny[6];
        calendar_format(&c, tiny, sizeof(tiny));
        CHECK(strlen(tiny) < sizeof(tiny), "a short buffer is respected");
        calendar_format(NULL, tiny, sizeof(tiny));
        CHECK(tiny[0] == '\0', "and no date is no text");
    }
}

/* ---- 4. a life is a session's worth ----------------------- */
static void test_the_scale(void)
{
    double month_secs = (double)CALENDAR_MONTH_TICKS / SIM_TICKS_PER_SEC;
    double year_mins  = month_secs * MONTHS_PER_YEAR / 60.0;
    char   msg[128];

    printf("\n=== and a life fits in an evening ===\n");

    snprintf(msg, sizeof(msg), "a year is %.1f minutes", year_mins);
    CHECK(year_mins > 5.0 && year_mins < 7.0, msg);

    /* The number the whole calendar decision turned on. Below about
     * four hours nobody plays long enough to inherit anybody; much
     * above ten and a life stops being a thing you can watch at all. */
    snprintf(msg, sizeof(msg),
             "so a seventy-year life is %.1f hours of play",
             70.0 * year_mins / 60.0);
    CHECK(70.0 * year_mins / 60.0 > 4.0 && 70.0 * year_mins / 60.0 < 10.0,
          msg);
}

int main(void)
{
    printf("== calendar (LIFE_PLAN Phase 4) ==\n");

    test_one_clock();
    test_dates();
    test_names();
    test_the_scale();

    printf("\n%s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
