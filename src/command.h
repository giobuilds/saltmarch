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
 *   CMD_SELL_RESOURCE   a=island b=resource c=qty d=limit price
 *   CMD_BUY_RESOURCE    a=island b=resource c=qty d=limit price
 *                        (c<0 => "max"; d==0 => no limit)
 *   CMD_UPGRADE_HOUSE   a=island b=building index c=TierBranch
 *                        (0 = the house's own line, 1 = Scholars via
 *                        an Academy — SUPPLY_CHAIN Phase 8 gave a
 *                        house two possible futures, so which one is
 *                        part of the command)
 *   CMD_BUILD_SHIP      a=island b=ShipClass b=shipyard index (unused today)
 *   CMD_SHIP_TRANSFER   a=ship   b=resource c=qty (sign=load/unload) d=island
 *   CMD_SHIP_DEPART     a=ship   b=destination island c=insure (0/1)
 *   CMD_COLONISE        a=ship   b=island index
 *   CMD_SET_ROUTE_RES   a=ship   b=leg (0=outbound A->B, 1=back B->A)
 *   CMD_TOGGLE_ROUTE    a=ship
 *   CMD_GRANT_START     a=island (settle it for player_id — the co-op
 *                        join bootstrap; validated: island unowned,
 *                        player owns nothing yet)
 *   CMD_ESCROW_PUT      a=island b=resource c=qty d=quay nonce
 *   CMD_ESCROW_TAKE     a=island b=resource c=qty d=quay nonce
 *                        (stockpile <-> escrow; d==0 => unstamped)
 *   CMD_SET_DOCKING     a=island b=allow (0/1 — foreign-ship permission)
 *   CMD_SET_INSURANCE   a=island b=on (0/1 — insure shipments from here)
 *   CMD_BUILD_RESEARCH_BOAT  a=island
 *   CMD_SURVEY          a=island (from) b=island (to)
 *   CMD_SET_ESCORT      a=ship b=ship it escorts (-1 to release)
 *   CMD_ATTACK_PIRATE   a=ship b=pirate fleet index
 *   CMD_PLACE_ORDER     a=island b=TRADE_PACK(kind,id) c=qty (sign is
 *                        the side: >0 buys, <0 sells) d=limit price
 *                        (MARITIME_PLAN Phase 2). The identity is
 *                        PACKED because what is traded is a kind and
 *                        an id, not a ResourceType — a chart names a
 *                        passage, and route charts are not
 *                        interchangeable. Sign-as-side is the trick
 *                        CMD_SHIP_TRANSFER already uses, and together
 *                        they are what let six fields fit four slots.
 *   CMD_CANCEL_ORDER    a=order id
 *   CMD_INTERCEPT       a=my ship b=target ship c=target departure tick
 *                        (the tick BINDS the reference: if the target
 *                        has since sailed again, the command names a
 *                        voyage that no longer exists and is refused
 *                        rather than applied to whatever is there now)
 *
 * THE LIMIT PRICE (UI_PLAN M3) is the price the screen was showing when
 * the player clicked. sim_apply recomputes the live quote and refuses
 * with REJ_PRICE_MOVED if it moved against them — because a command
 * applies a tick or more after the click, and under lockstep several,
 * during which another player's Sell-Max can move the market. Without
 * it the stale-screen race is a silent mis-fill; with it the race is a
 * logged, replayable, visible non-event. Zero means "whatever it is
 * now", which is what a replayed or scripted command carries.
 *
 * The PLACE_BUILDING pack (d = type*2 + pay_with_gold) is the one bit of
 * cleverness: five conceptual fields do not fit four slots, and both
 * type (a small enum) and pay_with_gold (a single bit) are bounded, so
 * they share d. Decoded in sim_apply.
 * ========================================================= */

#include <stddef.h>
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
    CMD_INTERCEPT,       /* attack another player's voyage at sea        */
    CMD_PLACE_ORDER,     /* post a buy or sell on the order book         */
    CMD_CANCEL_ORDER,    /* withdraw one, returning what it reserved     */
    /* Appended rather than slotted beside CMD_SET_DOCKING, which is
     * where it belongs by meaning: KIND_NAMES is positional and every
     * recorded log names kinds by number, so inserting in the middle
     * renumbers three existing commands and silently reinterprets any
     * log old enough to contain them. */
    CMD_SET_INSURANCE,   /* owner: standing marine policy for this port  */
    CMD_BUILD_RESEARCH_BOAT, /* owner: a hull for expeditions, at a yard */
    CMD_SURVEY,          /* owner: send a scholar to find a passage      */
    CMD_SET_ESCORT,      /* owner: assign a hull to guard another         */
    CMD_ATTACK_PIRATE,   /* owner: take a warship to a pirate lair         */
    CMD_COUNT
} CommandKind;

typedef struct {
    uint64_t    tick;       /* sim tick at which this command applies    */
    uint32_t    player_id;  /* 0 for now; becomes identity in Phase 5    */
    /* Client-local sequence number, stamped by command_submit on the
     * machine that authored this command (UI_PLAN M1). It exists so the
     * UI can recognise its OWN command coming back — applied a tick
     * later, or several ticks later through a co-op host — and say what
     * happened to it at the widget or tile that emitted it.
     *
     * Meaningless across clients: two players' sequences both start at
     * 1, so a match is only a match when player_id is yours too.
     * Ignored by the sim entirely; it is not part of what a command
     * DOES, which is why replay is unaffected by it. */
    uint32_t    seq;
    CommandKind kind;
    int32_t     a, b, c, d; /* payload, meaning per kind (see above)     */
} Command;

/* ---- Why the sim said no (UI_PLAN decision 3) --------------
 * MMO_PLAN requires a rejected command to change nothing and to be
 * rejected identically on every replay — which, with a bare 0/1 return,
 * makes every rejection a silently eaten click. This enum is the shared
 * vocabulary that fixes that: the sim produces a reason, the UI renders
 * it (ui_reject_text in ui_kit.c holds the strings, since wording is a
 * client concern), and the message a player sees is definitionally the
 * reason the sim refused rather than a client-side guess that can drift.
 *
 * REJ_OK is 0 so `if (reason)` reads as "was rejected". Values are
 * append-only: they will travel in logs and, later, over the wire.
 *
 * Adopted incrementally — UI_PLAN Phase 0.5 converts placement
 * (building_place_check); trade, ownership and escrow rejections join
 * as their phases land. */
typedef enum {
    REJ_OK = 0,               /* not a rejection                        */

    /* placement (Phase 0.5) */
    REJ_OUT_OF_BOUNDS,        /* footprint leaves the map               */
    REJ_NOT_BUILDABLE,        /* water, rock, or otherwise unbuildable   */
    REJ_NEEDS_FERTILE,        /* farm on infertile soil                 */
    /* Was REJ_NEEDS_HOP_FERTILE. Generalised with the crop bitmask
     * (SUPPLY_CHAIN Phase 1): one reason covers all fourteen crops,
     * because "this soil won't grow that" is the same sentence
     * whichever crop it is, and the player already knows which
     * building they are holding. */
    REJ_NEEDS_CROP,           /* wrong crop for this soil               */
    REJ_NEEDS_DEPOSIT,        /* no such mineral under or beside it     */
    /* Upgrading a house is gated on being able to supply the tier it
     * would become (SUPPLY_CHAIN Phase 2), not on gold alone. */
    REJ_NEEDS_GOODS,          /* the next tier's needs aren't in stock  */
    REJ_NEEDS_BUILDING,       /* the island lacks a required building   */
    REJ_NEEDS_COAST,          /* no adjacent water                      */
    REJ_NEEDS_FOREST,         /* no adjacent forest                     */
    REJ_OCCUPIED,             /* another building is already there      */

    /* economy and authority (adopted by later phases) */
    REJ_CANT_AFFORD,          /* the player cannot pay                  */
    REJ_NO_STOCK,             /* nothing there to sell/move             */
    REJ_NO_STORAGE,           /* no headroom to receive it              */
    REJ_COUNTERPARTY_NO_GOLD, /* the faction is out of money            */
    REJ_PRICE_MOVED,          /* quote moved past the limit sent        */
    REJ_NOT_OWNER,            /* someone else's island or ship          */
    REJ_ESCROW_REFUSED,       /* docking forbidden, or no harbour       */
    REJ_OFFER_CHANGED,        /* the quay moved under an open panel     */
    REJ_NO_TARGET,            /* that voyage is not there to intercept  */
    REJ_UNAVAILABLE,          /* generic: not possible right now        */
    /* Appended after the generic one rather than filed beside
     * REJ_NO_TARGET where it belongs by meaning: this enum is
     * positional and its values travel to the client as command
     * results, so inserting in the middle renames every reason after
     * it for a peer that has not been rebuilt. */
    REJ_ORDER_GONE,           /* the order filled or was withdrawn      */

    /* An expedition's three costs, each refused in its own words
     * (UI_PLAN N7). They were all REJ_UNAVAILABLE, which made "you
     * have nobody to send", "you have no boat" and "you have already
     * charted this crossing" the same sentence — and the last of those
     * is not a problem to be fixed but a thing to be told. */
    REJ_NO_CREW,              /* no scholar free to sail                */
    REJ_NO_BOAT,              /* no research boat free                  */
    REJ_NOTHING_TO_FIND,      /* every passage there is already known   */

    REJ_COUNT
} RejectReason;

/* Human-readable name for a CommandKind, for logging/debug. Never NULL;
 * returns "?" for an out-of-range kind. */
const char *command_kind_name(CommandKind kind);

/* Decode one command into readable text: kind plus its payload,
 * interpreted per the table above ("PLACE_BUILDING  island 0  (12,34)
 * type 3  pay Gold"). Writes at most `n` bytes including the
 * terminator; never fails.
 *
 * This exists so the confirm popup can show the LITERAL command it is
 * about to submit (UI_PLAN Phase 6). The point is not decoration: the
 * decoding lives beside the encoding it mirrors, so a popup cannot
 * describe one thing while sim_apply receives another, and a
 * screenshot of a confirmation is evidence about the wire format. */
void command_describe(const Command *c, char *out, size_t n);

#endif /* COMMAND_H */
