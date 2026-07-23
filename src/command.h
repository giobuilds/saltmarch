#ifndef COMMAND_H
#define COMMAND_H

/* =========================================================
 * command.h  --  The command funnel (MMO_PLAN Phase 1a)
 *
 * The architectural spine of the whole MMO plan: EVERY mutation of
 * world state is expressed as a Command and applied through the single
 * sim_apply() funnel (declared in game.h, next to the mutators it
 * dispatches to). Nothing mutates islands, stockpiles, buildings,
 * population or ships directly any more -- the UI-facing game_* helpers
 * build a Command and call command_submit().
 *
 * Why this matters: because the world is a pure function of
 * (world seed, ordered command log), a client can reproduce the entire
 * world by replaying the log. Determinism, save-as-log, the F9 desync
 * detector and eventual multiplayer all stand on this one invariant.
 * See MMO_PLAN.md.
 *
 * This header is deliberately SDL-free and depends only on <stdint.h>:
 * Command is destined for the future headless sim library and, later,
 * the wire. Keep it that way.
 *
 * PAYLOAD ENCODING. Command carries four generic int32 slots (a,b,c,d);
 * their meaning is per-kind and documented here so submit and apply
 * cannot drift:
 *
 *   CMD_PLACE_BUILDING  a=island b=row c=col d=type*2 + pay_with_gold
 *   CMD_PLACE_ROAD      a=island b=row c=col
 *   CMD_DEMOLISH        a=island b=building index
 *   CMD_SELL_RESOURCE   a=island b=resource c=qty
 *   CMD_BUY_RESOURCE    a=island b=resource c=qty  (c<0 => "max")
 *   CMD_UPGRADE_HOUSE   a=island b=building index
 *   CMD_BUILD_SHIP      a=island b=shipyard index (unused today)
 *   CMD_SHIP_TRANSFER   a=ship   b=resource c=qty (sign=load/unload) d=island
 *   CMD_SHIP_DEPART     a=ship   b=destination island
 *   CMD_COLONISE        a=ship   b=island index
 *   CMD_SET_ROUTE_RES   a=ship   b=leg (0=outbound A->B, 1=back B->A)
 *   CMD_TOGGLE_ROUTE    a=ship
 *   CMD_GRANT_START     a=island (settle it for player_id — the co-op
 *                        join bootstrap; validated: island unowned,
 *                        player owns nothing yet)
 *   CMD_ESCROW_PUT      a=island b=resource c=qty (stockpile -> escrow)
 *   CMD_ESCROW_TAKE     a=island b=resource c=qty (escrow -> stockpile)
 *   CMD_SET_DOCKING     a=island b=allow (0/1 — foreign-ship permission)
 *
 * The PLACE_BUILDING pack (d = type*2 + pay_with_gold) is the one bit of
 * cleverness: five conceptual fields do not fit four slots, and both
 * type (a small enum) and pay_with_gold (a single bit) are bounded, so
 * they share d. Decoded in sim_apply.
 * ========================================================= */

#include <stdint.h>

typedef enum {
    CMD_PLACE_BUILDING,
    CMD_PLACE_ROAD,
    CMD_DEMOLISH,
    CMD_SELL_RESOURCE,
    CMD_BUY_RESOURCE,
    CMD_UPGRADE_HOUSE,
    CMD_BUILD_SHIP,
    CMD_SHIP_TRANSFER,
    CMD_SHIP_DEPART,
    CMD_COLONISE,
    CMD_SET_ROUTE_RES,   /* cycle a route leg's carried resource         */
    CMD_TOGGLE_ROUTE,    /* activate/deactivate a ship's trade route     */
    CMD_GRANT_START,     /* settle a starting island for a new player    */
    CMD_ESCROW_PUT,      /* owner: move goods stockpile -> harbor escrow */
    CMD_ESCROW_TAKE,     /* owner: move goods harbor escrow -> stockpile */
    CMD_SET_DOCKING,     /* owner: allow/forbid foreign ships docking    */
    CMD_COUNT
} CommandKind;

typedef struct {
    uint64_t    tick;       /* sim tick at which this command applies    */
    uint32_t    player_id;  /* 0 for now; becomes identity in Phase 5    */
    CommandKind kind;
    int32_t     a, b, c, d; /* payload, meaning per kind (see above)     */
} Command;

/* Human-readable name for a CommandKind, for logging/debug. Never NULL;
 * returns "?" for an out-of-range kind. */
const char *command_kind_name(CommandKind kind);

#endif /* COMMAND_H */
