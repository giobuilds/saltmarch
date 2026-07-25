#ifndef HUD_VIEW_H
#define HUD_VIEW_H

/* =========================================================
 * hud_view.h  --  The build bar, in categories (UI_PLAN Phase 3)
 *
 * The HUD used to be every placeable building in one row. That works at
 * twelve and stops working somewhere around twenty-two, which is the
 * count at which slots run into the right-hand buttons — the second of
 * the capacity cliffs UI_PLAN measured. Categories are the fix: one
 * tab per BuildingCategory (Phase 2's data), each holding its own row.
 *
 * Two rules the plan is firm about, both about not moving things under
 * the player's cursor:
 *
 *   - THE TAB IS STICKY. It changes when the player clicks a tab, and
 *     at no other time. Auto-switching to "the tab containing what you
 *     just built" would be helpful exactly once and disorienting after
 *     that.
 *   - UNAVAILABLE IS GREYED, NEVER HIDDEN. A building you cannot yet
 *     afford keeps its slot, with the reason attached. Hiding it would
 *     reshuffle every slot after it each time Gold crossed a threshold,
 *     and would answer "where is the Brewery?" with silence.
 *
 * Like the exchange screen, this is a value struct plus a pure builder,
 * so the layout can be driven headlessly at goods counts the game does
 * not have — the test builds a 40-entry def table and asserts the bar
 * still fits.
 * ========================================================= */

#include <stdint.h>
#include "building.h"
#include "ui_kit.h"
#include "ui_snapshot.h"

#define HUD_MAX_ENTRIES  64
#define HUD_ENTRY_NAME   24

/* One placeable building, as the bar sees it. */
typedef struct {
    uint16_t type;          /* BuildingType — the identity            */
    uint8_t  category;      /* BuildingCategory                       */
    uint8_t  affordable;
    uint8_t  refuse;        /* RejectReason when it is not            */
    char     name[HUD_ENTRY_NAME];
    int32_t  cost_gold;     /* the Gold line of its cost, for hover   */
} HudEntry;

typedef struct {
    HudEntry entries[HUD_MAX_ENTRIES];
    int32_t  entry_count;

    /* Client state the bar renders but does not own. Set these before
     * calling hud_build(); they live in GameState today and move into
     * UiState when Phase 4's overlay arbiter lands. */
    int32_t  selected;        /* BuildingType, or BUILDING_NONE       */
    int32_t  demolish_mode;
    int32_t  world_open;
    int32_t  menu_open;
} HudView;

/* Fill `out` from the static def table and one island's stock: which
 * buildings exist, which category each is in, and whether this island
 * can currently afford it. Buildings with hud_placeable == 0 are left
 * out entirely — they are not reachable from the bar by design. */
void hud_view_build(HudView *out, const UiSnapshot *snap, int island);

/* ---- geometry ---------------------------------------------
 * The bar's metrics live here rather than in ui.h because this file is
 * part of the SDL-free UI library and ui.h is not — and because the
 * layout that uses them is here. ui.h includes this header, so every
 * existing user of HUD_HEIGHT (client.c's hover cutoff, ui.c) keeps
 * working unchanged.
 *
 * The bar grew from 80px to 112 to make room for the tab strip:
 * 6 gap + 28 tabs + 8 gap + 64 slot + 6 bottom. */
#define HUD_HEIGHT      112   /* pixels tall                  */
#define HUD_SLOT_SIZE    64   /* width and height of one slot */
#define HUD_SLOT_PAD     12   /* gap between slots            */
#define HUD_MARGIN_LEFT  20   /* left edge inset              */

#define HUD_TAB_H        28.0f
#define HUD_TAB_W       150.0f
#define HUD_TAB_GAP       4.0f
#define HUD_TAB_TOP       6.0f    /* gap above the tab strip          */
#define HUD_SLOT_TOP      8.0f    /* gap between tabs and slots       */

/* Build the bar's widget list: one widget per tab, one per slot in the
 * selected tab, plus the three right-hand buttons. `st->hud_category`
 * chooses the tab. */
void hud_build(UiList *out, const HudView *view, const UiState *st,
               float screen_w, float screen_h);

/* How many slots fit in one tab row at this width. Public because the
 * overflow indicator and the test both need it. */
int hud_slots_that_fit(float screen_w);

typedef enum {
    HUD_HIT_NONE = 0,      /* the bar itself: absorb the click        */
    HUD_HIT_OUTSIDE,       /* not on the bar at all                   */
    HUD_HIT_TAB,           /* `category` is the tab clicked           */
    HUD_HIT_BUILDING,      /* `type` is the building clicked          */
    HUD_HIT_MENU,
    HUD_HIT_DEMOLISH,
    HUD_HIT_WORLD
} HudHitKind;

typedef struct {
    HudHitKind kind;
    int        type;       /* BuildingType for HUD_HIT_BUILDING       */
    int        category;   /* BuildingCategory for HUD_HIT_TAB        */
    uint8_t    refuse;     /* why a greyed slot is greyed             */
} HudHit;

/* Decode a click against the list that was drawn. A greyed slot still
 * reports itself (with its reason) rather than reading as empty space:
 * the click should teach, not vanish. */
HudHit hud_hit(const UiList *list, float x, float y);

#endif /* HUD_VIEW_H */
