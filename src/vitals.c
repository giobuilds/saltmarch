/*  vitals.c  --  The alert strip's rules (UI_PLAN Phase 4)
 *
 *  Each rule reads the snapshot and either produces one row or says
 *  nothing. See vitals.h for why this is a module rather than a few
 *  lines in the renderer.
 */

#include "vitals.h"
#include "building.h"
#include "resource.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* The tick backlog at which the sim is visibly behind rather than
 * merely between ticks. The accumulator is normally under one tick;
 * eight is the clamp the client applies, so anything near it means the
 * frame loop is not keeping up. */
#define VITALS_BACKLOG_TICKS  4

/* A feed older than this has stopped being a live ocean and started
 * being a museum (UI_PLAN M4's staleness rule, useful already). */
#define VITALS_FEED_STALE_S   120

/* Rows are collected unranked and sorted at the end, so a rule never
 * has to know where it sits in the strip. */
typedef struct {
    VitalRow rows[32];
    int      count;
} RuleOutput;

static void emit(RuleOutput *o, VitalSeverity sev, int32_t count,
                 const char *fmt, ...)
{
    va_list ap;
    VitalRow *r;

    if (o->count >= (int)(sizeof(o->rows) / sizeof(o->rows[0]))) return;

    r = &o->rows[o->count++];
    r->severity = (uint8_t)sev;
    r->count    = count;

    va_start(ap, fmt);
    vsnprintf(r->text, sizeof(r->text), fmt, ap);
    va_end(ap);
}

/* ---- the rules -------------------------------------------- */

/* Buildings that cannot reach a Warehouse. Roads and Warehouses are
 * exempt: neither needs a route to itself. */
static void rule_disconnected(RuleOutput *o, const UiIsland *isl)
{
    int i, n = 0;

    for (i = 0; i < isl->building_count; i++) {
        const UiBuilding *b = &isl->buildings[i];
        if (!b->active || b->connected) continue;
        if (b->type == BUILDING_ROAD || b->type == BUILDING_WAREHOUSE) continue;
        n++;
    }
    if (n > 0)
        emit(o, VITAL_WARN, n, "%d not connected by road", n);
}

/* Producers standing idle for want of a worker. Only buildings that
 * actually produce are counted — a Marketplace has no worker and is
 * not idle. */
static void rule_no_workers(RuleOutput *o, const UiIsland *isl)
{
    int i, n = 0;

    for (i = 0; i < isl->building_count; i++) {
        const UiBuilding *b = &isl->buildings[i];
        if (!b->active || b->worker_count > 0) continue;
        if (b->type < 0 || b->type >= BUILDING_TYPE_COUNT) continue;
        if (BUILDING_DEFS[b->type].tick_seconds <= 0.0f) continue;
        n++;
    }
    if (n > 0)
        emit(o, VITAL_WARN, n, "%d without workers", n);
}

static void rule_hungry_houses(RuleOutput *o, const UiIsland *isl)
{
    int i, n = 0;

    for (i = 0; i < isl->building_count; i++) {
        const UiBuilding *b = &isl->buildings[i];
        if (!b->active || b->residents == 0) continue;
        if (!b->happy) n++;
    }
    if (n > 0)
        emit(o, VITAL_WARN, n, "%d houses going hungry", n);
}

/* A full store is production thrown away, and it is invisible until
 * someone notices the number has stopped moving. */
static void rule_storage_full(RuleOutput *o, const UiIsland *isl)
{
    int r, n = 0, first = -1;

    for (r = 0; r < (int)RES_GOLD; r++) {
        if (isl->capacity > 0 && isl->stock[r] >= isl->capacity) {
            if (first < 0) first = r;
            n++;
        }
    }
    if (n == 1)
        emit(o, VITAL_WARN, 1, "%s store is full", RESOURCE_NAMES[first]);
    else if (n > 1)
        emit(o, VITAL_WARN, n, "%d stores are full", n);
}

static void rule_no_houses(RuleOutput *o, const UiIsland *isl)
{
    int i, houses = 0;

    for (i = 0; i < isl->building_count; i++) {
        const UiBuilding *b = &isl->buildings[i];
        if (b->active && (b->type == BUILDING_HOUSE ||
                          b->type == BUILDING_HOUSE_WORKER)) houses++;
    }
    if (houses == 0)
        emit(o, VITAL_INFO, 0, "No houses — nobody works here");
}

/* ---- the sim's own health --------------------------------- */

static void rule_health(RuleOutput *o, const UiHealth *h)
{
    /* A desync is the most serious thing this game can tell you: the
     * world stopped being a function of its log. */
    if (h->replay_state == 2)
        emit(o, VITAL_ALERT, 0, "DESYNC — world diverged from its log");

    if (h->backlog_ticks >= VITALS_BACKLOG_TICKS)
        emit(o, VITAL_ALERT, (int32_t)h->backlog_ticks,
             "Sim behind by %u ticks", (unsigned)h->backlog_ticks);

    if (h->feed_age_s >= VITALS_FEED_STALE_S)
        emit(o, VITAL_WARN, h->feed_age_s / 60,
             "Ocean feed stale (%ds)", h->feed_age_s);

    if (h->net_connected == 0)
        emit(o, VITAL_INFO, 0, "Hosting — nobody has joined");

    /* The feed is a file other people append to, so malformed records
     * are expected rather than exceptional — but dropping them in
     * silence means a sync script writing garbage looks exactly like a
     * quiet ocean (UI_PLAN M4). */
    if (h->feed_malformed > 0)
        emit(o, VITAL_INFO, h->feed_malformed,
             "%d unreadable feed records", h->feed_malformed);
}

/* ---- assembly ---------------------------------------------- */

void vitals_build(VitalsView *out, const UiSnapshot *snap, int island)
{
    RuleOutput o;
    int        sev, i;

    memset(out, 0, sizeof(*out));
    o.count = 0;

    if (island < 0 || island >= MAX_ISLANDS) return;

    /* Health first in emission order; the sort below is what actually
     * ranks them, but keeping the sim's rows first makes ties resolve
     * towards "something is wrong with the game" over "something is
     * wrong in the game". */
    rule_health(&o, &snap->health);

    if (snap->islands[island].settled) {
        const UiIsland *isl = &snap->islands[island];
        rule_disconnected(&o, isl);
        rule_no_workers(&o, isl);
        rule_hungry_houses(&o, isl);
        rule_storage_full(&o, isl);
        rule_no_houses(&o, isl);
    }

    /* Stable selection sort by severity: equal severities keep their
     * emission order, so a row never swaps places with a peer because
     * a count changed. Insertion order IS the tiebreak, deliberately. */
    for (sev = VITAL_ALERT; sev >= VITAL_INFO; sev--) {
        for (i = 0; i < o.count; i++) {
            if (o.rows[i].severity != (uint8_t)sev) continue;
            if (out->row_count >= VITALS_MAX_ROWS) { out->hidden++; continue; }
            out->rows[out->row_count++] = o.rows[i];
        }
    }
}

const char *vitals_severity_name(VitalSeverity s)
{
    switch (s) {
    case VITAL_ALERT: return "Alert";
    case VITAL_WARN:  return "Warning";
    case VITAL_INFO:  return "Note";
    default:          return "Note";
    }
}
