#ifndef ISLAND_BAR_H
#define ISLAND_BAR_H

/* =========================================================
 * island_bar.h  --  Which island am I looking at?
 *                   (UI_PLAN Phase 5)
 *
 * A header across the top: ‹ Saltford ›, with chevrons that step
 * through the islands you have settled.
 *
 * The question it answers is one the game could not previously answer
 * without opening the world map. Every overlay is current-island
 * relative, every stockpile is per-island, and goods do not teleport —
 * so "which market am I trading in" and "whose warehouse is this" are
 * load-bearing questions, and the answer was a colour of terrain and a
 * memory of what you last clicked.
 *
 * The per-island HUE does the same job pre-attentively: each island
 * gets a fixed colour, used by this header and by the title bar of
 * every overlay that belongs to an island. Glancing at a screen edge
 * tells you where you are before you have read anything.
 *
 * Chevrons step only through SETTLED islands. An unclaimed island is
 * something you look at from the world map, not somewhere you are.
 * ========================================================= */

#include <stdint.h>
#include "ui_kit.h"
#include "ui_snapshot.h"

#define ISLAND_BAR_H_PX     30.0f
#define ISLAND_BAR_W        320.0f
#define ISLAND_BAR_TOP       8.0f
#define ISLAND_BAR_CHEVRON  30.0f

/* Build the header for `snap->current_island`. Chevron widgets carry
 * the island they would switch TO (UI_GROUP_ISLAND, value = index), so
 * a click needs no arithmetic to interpret and no knowledge of what was
 * on screen when it was made. With fewer than two settled islands both
 * chevrons are present but disabled — greyed, not hidden, so the header
 * does not change width when you found your second colony. */
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
