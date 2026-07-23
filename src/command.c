/*  command.c  --  The command funnel: log storage and submission
 *                 (MMO_PLAN Phase 1a)
 *
 * command_submit() appends a command to the world's command log and
 * applies it. The dispatch itself (sim_apply) lives in game.c beside
 * the mutators; this file owns only the log: its growth, its lifetime,
 * and the submit path that keeps "logged" and "applied" in lockstep.
 *
 * Phase 1a applies each command immediately on submit, which preserves
 * today's same-frame behaviour exactly. Phase 1b will change this so
 * submit only appends (stamped for the next tick) and sim_run_one_tick()
 * drains and applies at tick boundaries, in log order.
 */

#include "game.h"
#include <SDL3/SDL.h>
#include <stdlib.h>

static const char *const KIND_NAMES[CMD_COUNT] = {
    "PLACE_BUILDING", "PLACE_ROAD", "DEMOLISH", "SELL_RESOURCE",
    "BUY_RESOURCE", "UPGRADE_HOUSE", "BUILD_SHIP", "SHIP_TRANSFER",
    "SHIP_DEPART", "COLONISE", "SET_ROUTE_RES", "TOGGLE_ROUTE"
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

    /* Stamp with the world clock and (for now) the single local player.
     * Log first, apply second, and only apply if the log actually
     * recorded it — see cmd_log_push. */
    stamped.tick      = gs->sim_tick_no;
    stamped.player_id = 0;

    if (!cmd_log_push(gs, &stamped))
        return 0;

    return sim_apply(gs, &stamped);
}

void command_log_free(GameState *gs)
{
    free(gs->cmd_log);
    gs->cmd_log   = NULL;
    gs->cmd_count = 0;
    gs->cmd_cap   = 0;
}
