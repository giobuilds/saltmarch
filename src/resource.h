#ifndef RESOURCE_H
#define RESOURCE_H

/* =========================================================
 * resource.h  --  Resource types and stockpile  (Phase 4)
 *
 * A Stockpile holds one integer count per ResourceType.
 * All buildings read and write the single global Stockpile
 * that lives in GameState.
 *
 * GOLD is special: it is a currency, not a physical good.
 * In Phase 4 it simply accumulates (no spending yet).
 * Spending mechanics arrive in Phase 5 with population needs.
 * ========================================================= */

/* ---- Resource types ------------------------------------ */
typedef enum {
    RES_WOOD  = 0,
    RES_FISH  = 1,
    RES_GRAIN = 2,
    RES_GOLD  = 3,
    RES_COUNT          /* always last */
} ResourceType;

/* Human-readable name for each resource (for debug / future UI). */
extern const char *RESOURCE_NAMES[RES_COUNT];

/* Per-resource storage cap before any Warehouse is built.
 * See building.h's WAREHOUSE_STORAGE_BONUS for how building one
 * raises this. Gold is exempt (see stockpile_add). */
#define BASE_STORAGE_CAP 100

/* ---- Stockpile ----------------------------------------- */
typedef struct {
    int amount[RES_COUNT];   /* current count per resource   */
    int capacity;            /* cap applied to amount[] (except GOLD) */
} Stockpile;

/* Initialise all amounts to zero and capacity to BASE_STORAGE_CAP. */
void stockpile_init(Stockpile *s);

/* Add `delta` units of `res` to the stockpile.
 * delta may be negative (consumption).
 * Clamps to zero on the low end — stock never goes negative.
 * For every resource except RES_GOLD (a currency, not a physical
 * good — see the design note above ResourceType), also clamps to
 * s->capacity on the high end. */
void stockpile_add(Stockpile *s, ResourceType res, int delta);

/* Set the storage cap applied to every non-gold resource.
 * Called by the game layer whenever the number of built
 * Warehouses changes. Existing amounts above the new cap are
 * clamped down immediately. */
void stockpile_set_capacity(Stockpile *s, int capacity);

#endif /* RESOURCE_H */
