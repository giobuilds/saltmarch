#ifndef ISLAND_BAR_H
#define ISLAND_BAR_H

/* island_bar.h  --  Which island am I looking at?
 * (UI_PLAN Phase 5) */

#include <stdint.h>
#include "ui_kit.h"
#include "ui_snapshot.h"

#define ISLAND_BAR_H_PX     30.0f
#define ISLAND_BAR_W        320.0f
#define ISLAND_BAR_TOP       8.0f
#define ISLAND_BAR_CHEVRON  30.0f

/* Build the header for `snap->current_island`. Chevron widgets carry */
void island_bar_build(UiList *out, const UiSnapshot *snap,
                      float screen_w);

typedef enum {
    ISLAND_BAR_HIT_NONE = 0,
    ISLAND_BAR_HIT_SWITCH        /* `island` is where to go */
} IslandBarHitKind;

typedef struct {
    IslandBarHitKind kind;
    int              island;
} IslandBarHit;

IslandBarHit island_bar_hit(const UiList *list, float x, float y);

/* The island's colour. Deterministic in the index, so it never changes
 * across sessions or between two clients looking at the same world —
 * an island whose colour depended on settlement order would be a
 * different colour to each player. */
void island_hue(int island, uint8_t *r, uint8_t *g, uint8_t *b);

#endif /* ISLAND_BAR_H */
