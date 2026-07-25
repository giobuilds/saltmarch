#ifndef VITALS_H
#define VITALS_H

/* =========================================================
 * vitals.h  --  What is wrong right now (UI_PLAN Phase 4)
 *
 * A short, ranked list of the things a player would want to be told
 * without going looking: buildings that cannot work, houses going
 * hungry, stores that are full — and, folded in beside them, the health
 * of the simulation itself.
 *
 * Three properties make this worth having as its own module rather than
 * a few lines in the renderer:
 *
 * 1. RULE-DRIVEN, NOT HAND-PLACED. Each rule is a function of the
 *    snapshot producing at most one row. Adding a rule cannot disturb
 *    the others, and the strip has one ordering policy (severity first,
 *    then rule order) rather than a drawing order that happens to be
 *    the order someone wrote the ifs in.
 *
 * 2. CAPPED, WITH THE REMAINDER STATED. At most VITALS_MAX_ROWS are
 *    shown and the rest are counted into a "+k more" line. An alert
 *    strip that can grow without limit is the same capacity cliff as
 *    the trade screen, arriving on the worst possible day — the one
 *    where everything is broken at once.
 *
 * 3. THE SIM IS A PATIENT TOO. The last F9 result, how far behind the
 *    tick accumulator is, and how stale the shared feed has gone are
 *    rendered by the same machinery as a farm without a worker. The
 *    player is the monitoring system; a stalled sim should be visible
 *    seconds after it starts rather than at the next desync.
 *
 * SDL-free: rows are text and a severity, and the drawer decides what
 * that looks like.
 * ========================================================= */

#include <stdint.h>
#include "ui_snapshot.h"

#define VITALS_MAX_ROWS   8
#define VITALS_TEXT_LEN  48

typedef enum {
    VITAL_INFO = 0,     /* worth knowing                              */
    VITAL_WARN,         /* something is not working                   */
    VITAL_ALERT         /* something is wrong with the game itself    */
} VitalSeverity;

typedef struct {
    uint8_t severity;               /* VitalSeverity                  */
    int32_t count;                  /* how many things (0 = n/a)      */
    char    text[VITALS_TEXT_LEN];
} VitalRow;

typedef struct {
    VitalRow rows[VITALS_MAX_ROWS];
    int32_t  row_count;
    int32_t  hidden;                /* rules that fired but did not fit */
} VitalsView;

/* Run every rule against one island and the client's health readings.
 * Rows come back most severe first; within a severity, in rule order,
 * which is fixed — so a row does not jump around the strip as counts
 * change. */
void vitals_build(VitalsView *out, const UiSnapshot *snap, int island);

/* Severity as a short word ("Warning"), for drawers and tests. */
const char *vitals_severity_name(VitalSeverity s);

#endif /* VITALS_H */
