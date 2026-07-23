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
#include "net.h"     /* Phase 5: route submissions through a session */
#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>

static const char *const KIND_NAMES[CMD_COUNT] = {
    "PLACE_BUILDING", "PLACE_ROAD", "DEMOLISH", "SELL_RESOURCE",
    "BUY_RESOURCE", "UPGRADE_HOUSE", "BUILD_SHIP", "SHIP_TRANSFER",
    "SHIP_DEPART", "COLONISE", "SET_ROUTE_RES", "TOGGLE_ROUTE",
    "GRANT_START", "ESCROW_PUT", "ESCROW_TAKE", "SET_DOCKING"
};

const char *command_kind_name(CommandKind kind)
{
    if (kind < 0 || kind >= CMD_COUNT) return "?";
    return KIND_NAMES[kind];
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
            SDL_Log("command_submit: out of memory growing log to %d", ncap);
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

    /* In a co-op session the submission is routed through the host's
     * ordering authority instead of the local log (host: stamp + log +
     * broadcast; guest: send upstream and wait for it to come back
     * stamped). Offline, or if the session declines, fall through to
     * local stamping. */
    if (gs->net && net_submit_local(gs->net, gs, c))
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
