#ifndef YARD_VIEW_H
#define YARD_VIEW_H

/* =========================================================
 * yard_view.h  --  The yard, and the fleet it launches
 *                  (UI_PLAN N6)
 *
 * MARITIME_PLAN Phase 5 made a hull a choice: guns cost hold, so a
 * merchantman carries the cargo that makes trading worth doing and
 * cannot defend it, a warship can take anything at sea and has nowhere
 * to put it, and the answer to "how do I move cargo through dangerous
 * water" is a second ship rather than a compromise ship.
 *
 * None of that was reachable. `game_build_ship()` is
 * `game_build_ship_class(SHIP_MERCHANTMAN)` and the confirm popup called
 * it with no way to say otherwise, so every ship in every game since
 * that phase has been a merchantman — the trade-off existed in the sim
 * and nowhere a player could see it. Escorts were the same: the command
 * has been there since Phase 5a with nothing to issue it.
 *
 * So this screen is two lists, on `Y`:
 *
 *   the yard   — one row per hull, its guns, what it survives, what it
 *                carries and what the yard charges, side by side, so
 *                the trade is legible BEFORE the gold is spent;
 *   the fleet  — one row per ship of yours, its class, its condition,
 *                where it is, and who it is guarding.
 *
 * CONDITION IS THE OTHER HALF OF THE PHASE. A hull is the only thing
 * about a ship that moves: it loses hull in a fight and never gets it
 * back, and a ship out of hull is gone. Until now that number existed
 * only in the sim, which meant a player sent a half-wrecked warship at a
 * fleet with no way of knowing it was half-wrecked. `guns` here is
 * `ship_fighting_strength()` — what the hull is ACTUALLY worth in a
 * fight, scaled by how much of it is left — rather than the class
 * table's number, because that is the figure the bet is made against.
 *
 * There is no refit: the sim has no command that repairs a hull, so
 * this screen does not offer one. A damaged ship says what it is worth
 * and that is the whole truth available. Inventing a repair here would
 * mean inventing it in the sim, which is a design decision and not a
 * UI phase's to make.
 *
 * ESCORT IS CHOSEN BY CYCLING, for the reason N3's good-picker cycles:
 * there is no text input and no drag in this game. Each button carries
 * the ship it SELECTS as its identity rather than a direction, so a
 * recorded click still means "guard ship 3" if the fleet changed shape
 * between frames — the same rule the island bar's chevrons follow.
 * ========================================================= */

#include <stdint.h>
#include "ui_kit.h"
#include "ui_snapshot.h"
#include "ship.h"

#define YARD_MAX_ROWS    (SHIP_CLASS_COUNT + MAX_SHIPS)
#define YARD_NAME_LEN    24
#define YARD_TITLE_LEN   48

typedef struct {
    uint8_t  is_hull;        /* a hull the yard offers, not a ship     */
    int32_t  klass;          /* ShipClass, both kinds of row           */
    int32_t  ship;           /* fleet rows: the ship index             */

    int32_t  guns;           /* hulls: the class's. ships: what it is
                              * actually worth in a fight now          */
    int32_t  hull, hull_max;
    int32_t  hold;
    int32_t  cost;           /* hull rows only                         */

    int32_t  at_island;      /* -1 at sea                              */
    int32_t  to_island;
    int32_t  escorting;      /* -1 for none                            */
    int32_t  escort_next;    /* the ship the cycle button would pick   */
    int32_t  cargo_units;    /* how full the hold is, all goods        */
    uint8_t  affordable;

    char     name[YARD_NAME_LEN];
} YardRow;

typedef struct {
    char     title[YARD_TITLE_LEN];
    uint8_t  hue_r, hue_g, hue_b;
    int32_t  island;
    uint64_t tick;

    int32_t  your_gold;
    int32_t  fleet_size;     /* ships of yours afloat                  */
    int32_t  fleet_cap;      /* MAX_SHIPS                              */
    uint8_t  yours;

    YardRow  rows[YARD_MAX_ROWS];
    int32_t  row_count;
} YardView;

/* Rebuilt each frame from the snapshot alone: unlike the book's orders
 * or the charts' passages, nothing here can vanish under a cursor. A
 * hull is a fixed table, and a ship that sinks was one of yours — its
 * row leaving is the news, not an accident of ordering. */
void yard_view_build(YardView *v, const UiSnapshot *snap, int island);

/* ---- geometry ---------------------------------------------- */
#define YARD_W            820.0f
#define YARD_MARGIN        16.0f
#define YARD_TITLE_H       40.0f
#define YARD_HEAD_H        22.0f
#define YARD_ROW_H         32.0f
#define YARD_ROW_GAP        4.0f
#define YARD_FOOTER_H      40.0f
#define YARD_BTN_W         96.0f
#define YARD_BTN_GAP        6.0f
#define YARD_MAX_H        820.0f

#define YARD_COL_NAME     190.0f
#define YARD_COL_GUNS      80.0f
#define YARD_COL_HULL     110.0f
#define YARD_COL_HOLD      90.0f
#define YARD_COL_WHERE    150.0f

typedef enum {
    YD_COL_NAME = 0,
    YD_COL_GUNS,
    YD_COL_HULL,       /* hulls: what it survives. ships: condition   */
    YD_COL_HOLD,
    YD_COL_WHERE,      /* hulls: the price. ships: where it is        */
    YD_COL_COUNT
} YardCol;

UiRect yard_col_rect(UiRect row, YardCol col);

void yard_build(UiList *out, const YardView *view, const UiState *st,
                float screen_w, float screen_h);

int  yard_page_count(const YardView *view, float screen_h);

typedef enum {
    YARD_HIT_NONE = 0,
    YARD_HIT_OUTSIDE,
    YARD_HIT_CLOSE,
    YARD_HIT_PAGE,
    YARD_HIT_BUILD,        /* `klass` is the hull to lay down          */
    YARD_HIT_ESCORT        /* `ship` guards `target` (-1 releases it)   */
} YardHitKind;

typedef struct {
    YardHitKind kind;
    int32_t     klass;
    int32_t     ship;
    int32_t     target;
    int32_t     page;
    UiRect      rect;
} YardHit;

YardHit yard_hit(const UiList *list, const YardView *view,
                 const UiState *st, float x, float y);

#endif /* YARD_VIEW_H */
