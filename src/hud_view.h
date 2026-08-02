#ifndef HUD_VIEW_H
#define HUD_VIEW_H

/* hud_view.h  --  The build bar, in categories (UI_PLAN Phase 3) */

#include <stdint.h>
#include "building.h"
#include "ui_kit.h"
#include "ui_snapshot.h"

/* Every placeable building, not every building that fits on screen: */
#define HUD_MAX_ENTRIES  128
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

/* ---- geometry --------------------------------------------- */
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
