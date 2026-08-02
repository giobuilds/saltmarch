#ifndef COMMAND_H
#define COMMAND_H

/* command.h  --  The command funnel (MMO_PLAN Phase 1a) */

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
    CMD_SET_TAX_RATE,    /* owner: what the treasury takes (Phase 7)     */
    CMD_ESCROW_PUT,      /* owner: move goods stockpile -> harbor escrow */
    CMD_ESCROW_TAKE,     /* owner: move goods harbor escrow -> stockpile */
    CMD_SET_DOCKING,     /* owner: allow/forbid foreign ships docking    */
    CMD_INTERCEPT,       /* attack another player's voyage at sea        */
    CMD_PLACE_ORDER,     /* post a buy or sell on the order book         */
    CMD_CANCEL_ORDER,    /* withdraw one, returning what it reserved     */
    /* Appended rather than slotted beside CMD_SET_DOCKING, which. */
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
    /* Client-local sequence number, stamped by command_submit on. */
    uint32_t    seq;
    CommandKind kind;
    int32_t     a, b, c, d; /* payload, meaning per kind (see above)     */
} Command;

/* ---- Why the sim said no (UI_PLAN decision 3) -------------- */
typedef enum {
    REJ_OK = 0,               /* not a rejection                        */

    /* placement (Phase 0.5) */
    REJ_OUT_OF_BOUNDS,        /* footprint leaves the map               */
    REJ_NOT_BUILDABLE,        /* water, rock, or otherwise unbuildable   */
    REJ_NEEDS_FERTILE,        /* farm on infertile soil                 */
    /* Was REJ_NEEDS_HOP_FERTILE. Generalised with the crop bitmask */
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
    /* Appended after the generic one rather than filed beside */
    REJ_ORDER_GONE,           /* the order filled or was withdrawn      */

    /* An expedition's three costs, each refused in its own words */
    REJ_NO_CREW,              /* no scholar free to sail                */
    REJ_NO_BOAT,              /* no research boat free                  */
    REJ_NOTHING_TO_FIND,      /* every passage there is already known   */

    REJ_COUNT
} RejectReason;

/* Human-readable name for a CommandKind, for logging/debug. Never NULL;
 * returns "?" for an out-of-range kind. */
const char *command_kind_name(CommandKind kind);

/* Decode one command into readable text: kind plus its payload,
 * interpreted per the table above ("PLACE_BUILDING  island 0  (12,34) */
void command_describe(const Command *c, char *out, size_t n);

#endif /* COMMAND_H */
