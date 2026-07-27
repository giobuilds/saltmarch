/*  command.c  --  The command funnel: log storage and submission
 *                 (MMO_PLAN Phase 1a)
 *
 * command_submit() stamps a command for the next tick and appends it to
 * the world's command log. It does NOT apply it: sim_run_one_tick()
 * (game.c) drains the pending tail of the log at each tick boundary, in
 * order. The dispatch itself (sim_apply) also lives in game.c beside the
 * mutators; this file owns only the log: its growth, its lifetime, and
 * the submit path.
 *
 * The tick-boundary deferral (Phase 1b) is what makes command latency a
 * fixed, frame-rate-independent quantity — the property multiplayer
 * lockstep later relies on. Submitting a command therefore no longer
 * reports whether it succeeded (that is not known until its tick runs);
 * command_submit returns 1 if the command was queued, 0 only if the log
 * could not grow.
 */

#include "game.h"
#include "orderbook.h"
#include "simlog.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const KIND_NAMES[CMD_COUNT] = {
    "PLACE_BUILDING", "PLACE_ROAD", "DEMOLISH", "SELL_RESOURCE",
    "BUY_RESOURCE", "UPGRADE_HOUSE", "BUILD_SHIP", "SHIP_TRANSFER",
    "SHIP_DEPART", "COLONISE", "SET_ROUTE_RES", "TOGGLE_ROUTE",
    "GRANT_START", "ESCROW_PUT", "ESCROW_TAKE", "SET_DOCKING",
    "INTERCEPT", "PLACE_ORDER", "CANCEL_ORDER", "SET_INSURANCE"
};

const char *command_kind_name(CommandKind kind)
{
    if (kind < 0 || kind >= CMD_COUNT) return "?";
    return KIND_NAMES[kind];
}

void command_describe(const Command *c, char *out, size_t n)
{
    if (!out || n == 0) return;

    /* One case per kind, decoding the same slots command.h documents.
     * A kind added without a case here still prints its name and raw
     * slots rather than silently lying about them. */
    switch (c->kind) {
    case CMD_PLACE_BUILDING:
        snprintf(out, n, "PLACE_BUILDING  island %d  (%d,%d)  type %d  pay %s",
                 c->a, c->b, c->c, c->d / 2, (c->d & 1) ? "Gold" : "goods");
        break;
    case CMD_PLACE_ROAD:
        snprintf(out, n, "PLACE_ROAD  island %d  (%d,%d)", c->a, c->b, c->c);
        break;
    case CMD_DEMOLISH:
        snprintf(out, n, "DEMOLISH  island %d  building %d", c->a, c->b);
        break;
    case CMD_SELL_RESOURCE:
        if (c->d > 0)
            snprintf(out, n, "SELL  island %d  res %d  qty %d  at %d+",
                     c->a, c->b, c->c, c->d);
        else
            snprintf(out, n, "SELL  island %d  res %d  qty %d",
                     c->a, c->b, c->c);
        break;
    case CMD_BUY_RESOURCE:
        if (c->d > 0)
            snprintf(out, n, "BUY  island %d  res %d  qty %d  at %d-",
                     c->a, c->b, c->c, c->d);
        else
            snprintf(out, n, "BUY  island %d  res %d  qty %d",
                     c->a, c->b, c->c);
        break;
    case CMD_UPGRADE_HOUSE:
        snprintf(out, n, "UPGRADE_HOUSE  island %d  building %d", c->a, c->b);
        break;
    case CMD_BUILD_SHIP:
        snprintf(out, n, "BUILD_SHIP  island %d", c->a);
        break;
    case CMD_SHIP_TRANSFER:
        snprintf(out, n, "SHIP_TRANSFER  ship %d  res %d  qty %d  island %d",
                 c->a, c->b, c->c, c->d);
        break;
    case CMD_SHIP_DEPART:
        snprintf(out, n, "SHIP_DEPART  ship %d  to island %d", c->a, c->b);
        break;
    case CMD_COLONISE:
        snprintf(out, n, "COLONISE  ship %d  island %d", c->a, c->b);
        break;
    case CMD_SET_ROUTE_RES:
        snprintf(out, n, "SET_ROUTE_RES  ship %d  leg %d", c->a, c->b);
        break;
    case CMD_TOGGLE_ROUTE:
        snprintf(out, n, "TOGGLE_ROUTE  ship %d", c->a);
        break;
    case CMD_GRANT_START:
        snprintf(out, n, "GRANT_START  island %d", c->a);
        break;
    case CMD_ESCROW_PUT:
        snprintf(out, n, "ESCROW_PUT  island %d  res %d  qty %d",
                 c->a, c->b, c->c);
        break;
    case CMD_ESCROW_TAKE:
        snprintf(out, n, "ESCROW_TAKE  island %d  res %d  qty %d",
                 c->a, c->b, c->c);
        break;
    case CMD_SET_DOCKING:
        snprintf(out, n, "SET_DOCKING  island %d  allow %d", c->a, c->b);
        break;
    case CMD_INTERCEPT:
        snprintf(out, n, "INTERCEPT  ship %d -> ship %d  (departed %d)",
                 c->a, c->b, c->c);
        break;
    case CMD_PLACE_ORDER:
        snprintf(out, n, "%s  island %d  kind %u id %u  qty %d  at %d",
                 c->c >= 0 ? "BUY ORDER" : "SELL ORDER", c->a,
                 (unsigned)TRADE_KIND_OF(c->b), (unsigned)TRADE_ID_OF(c->b),
                 c->c >= 0 ? c->c : -c->c, c->d);
        break;
    case CMD_CANCEL_ORDER:
        snprintf(out, n, "CANCEL_ORDER  %d", c->a);
        break;
    case CMD_SET_INSURANCE:
        snprintf(out, n, "SET_INSURANCE  island %d  %s", c->a,
                 c->b ? "on" : "off");
        break;
    default:
        snprintf(out, n, "%s  %d %d %d %d", command_kind_name(c->kind),
                 c->a, c->b, c->c, c->d);
        break;
    }
}

/* Append one command to the log, growing by doubling. Returns 1 on
 * success, 0 if the log could not be grown (out of memory) — in which
 * case the caller must NOT apply the command, or the applied world
 * would diverge from the recorded log and the whole replay invariant
 * breaks. */
static int cmd_log_push(GameState *gs, const Command *c)
{
    if (gs->cmd_count == gs->cmd_cap) {
        int      ncap = gs->cmd_cap ? gs->cmd_cap * 2 : 64;
        Command *n    = (Command *)realloc(gs->cmd_log,
                                           (size_t)ncap * sizeof(Command));
        if (!n) {
            sim_log("command_submit: out of memory growing log to %d", ncap);
            return 0;
        }
        gs->cmd_log = n;
        gs->cmd_cap = ncap;
    }
    gs->cmd_log[gs->cmd_count++] = *c;
    return 1;
}

int command_submit(GameState *gs, const Command *c)
{
    Command stamped = *c;

    /* Nothing may be submitted while viewing the past (MMO_PLAN's
     * scrubber). A command stamped for a tick the log has already
     * passed would be inserted behind its own head, and "the world is
     * the ordered log" would stop being true — which is the one
     * invariant everything else in this architecture stands on. */
    if (gs->scrub_active) return 0;

    /* Stamp the sequence before routing, so a command handed to a co-op
     * host carries it there and back and the UI recognises its own
     * (UI_PLAN M1). Sequences start at 1: zero means "not ours". */
    if (stamped.seq == 0) {
        if (gs->cmd_seq_next == 0) gs->cmd_seq_next = 1;
        stamped.seq = gs->cmd_seq_next++;
    }
    gs->cmd_seq_last = stamped.seq;

    /* In a co-op session the submission is routed through the host's
     * ordering authority instead of the local log (host: stamp + log +
     * broadcast; guest: send upstream and wait for it to come back
     * stamped). Offline, or if the session declines, fall through to
     * local stamping. */
    if (gs->net && gs->net_submit && gs->net_submit(gs->net, gs, &stamped))
        return 1;

    /* Stamp for the next tick to run (sim_tick_no) and with the local
     * player's identity (Phase 5: ownership validation reads this).
     * sim_run_one_tick applies it when the world clock reaches that
     * tick. */
    stamped.tick      = gs->sim_tick_no;
    stamped.player_id = gs->local_player_id;

    return cmd_log_push(gs, &stamped);
}

int command_log_append(GameState *gs, const Command *c)
{
    return cmd_log_push(gs, c);
}

int command_log_set(GameState *gs, const Command *cmds, int n)
{
    if (n < 0) return 0;

    if (n > gs->cmd_cap) {
        int      ncap = gs->cmd_cap ? gs->cmd_cap : 64;
        Command *p;
        while (ncap < n) ncap *= 2;
        p = (Command *)realloc(gs->cmd_log, (size_t)ncap * sizeof(Command));
        if (!p) return 0;
        gs->cmd_log = p;
        gs->cmd_cap = ncap;
    }

    if (n > 0) memcpy(gs->cmd_log, cmds, (size_t)n * sizeof(Command));
    gs->cmd_count   = n;
    gs->cmd_applied = 0;
    return 1;
}

void command_log_free(GameState *gs)
{
    free(gs->cmd_log);
    gs->cmd_log     = NULL;
    gs->cmd_count   = 0;
    gs->cmd_cap     = 0;
    gs->cmd_applied = 0;
}

/* ---- the recorded input stream (UI_PLAN M1) ---------------- */

int intent_record(GameState *gs, const Intent *in)
{
    if (gs->intent_count == gs->intent_cap) {
        int     ncap = gs->intent_cap ? gs->intent_cap * 2 : 64;
        Intent *n    = (Intent *)realloc(gs->intent_log,
                                         (size_t)ncap * sizeof(Intent));
        if (!n) {
            /* Unlike the command log, losing one of these breaks
             * nothing: the world is still a pure function of the
             * commands. It costs a test case, so say so and move on. */
            sim_log("intent_record: out of memory at %d intents",
                    gs->intent_count);
            return 0;
        }
        gs->intent_log = n;
        gs->intent_cap = ncap;
    }
    gs->intent_log[gs->intent_count++] = *in;
    return 1;
}

int intent_log_set(GameState *gs, const Intent *ins, int n)
{
    if (n > gs->intent_cap) {
        Intent *g = (Intent *)realloc(gs->intent_log,
                                      (size_t)n * sizeof(Intent));
        if (!g) return 0;
        gs->intent_log = g;
        gs->intent_cap = n;
    }
    if (n > 0) memcpy(gs->intent_log, ins, (size_t)n * sizeof(Intent));
    gs->intent_count = n;
    return 1;
}

void intent_log_free(GameState *gs)
{
    free(gs->intent_log);
    gs->intent_log   = NULL;
    gs->intent_count = 0;
    gs->intent_cap   = 0;
}
