/*  population.c  --  Residents and needs  (Phase 5)  */

#include "population.h"
#include <SDL3/SDL.h>   /* SDL_Log */

/* ---- pop_init ------------------------------------------ */
void pop_init(PopData *p)
{
    p->active    = 1;
    p->residents = 5;        /* start half-full so growth is visible */
    p->timer     = 0.0f;
    p->happy     = 0;
}

/* ---- pop_update ----------------------------------------
 * The needs loop.  Runs once per frame for every house.
 *
 * We use a single timer per house rather than a global
 * tick so houses placed at different times stagger their
 * consumption — avoiding a sudden stockpile spike every
 * NEEDS_INTERVAL seconds.
 * -------------------------------------------------------- */
void pop_update(PopData pop[], const Building buildings[], int count,
               Stockpile *s, float dt)
{
    int i;

    for (i = 0; i < count; i++) {
        PopData *p = &pop[i];
        if (!p->active) continue;

        p->timer += dt;
        if (p->timer < NEEDS_INTERVAL) continue;
        p->timer = 0.0f;

        /* --- Needs check: road-connected, plus FISH and GRAIN --
         * A disconnected house has no route for a Warehouse to
         * deliver food, so it's treated the same as needs unmet. */
        if (buildings[i].connected &&
            s->amount[RES_FISH]  > 0 &&
            s->amount[RES_GRAIN] > 0 &&
            p->residents > 0) {

            /* Consume one of each food type */
            stockpile_add(s, RES_FISH,  -1);
            stockpile_add(s, RES_GRAIN, -1);

            /* Generate gold proportional to residents */
            stockpile_add(s, RES_GOLD,
                          GOLD_PER_RESIDENT * p->residents);

            p->happy = 1;

            /* Population grows toward capacity when happy */
            if (p->residents < HOUSE_CAPACITY)
                p->residents++;

            SDL_Log("House %d: happy, %d residents, +%d gold",
                i, p->residents,
                GOLD_PER_RESIDENT * p->residents);

        } else {
            /* Needs not met — residents leave */
            p->happy = 0;
            if (p->residents > 0)
                p->residents--;

            SDL_Log("House %d: unhappy (%s%s%s), %d residents",
                i,
                !buildings[i].connected ? "no road to Warehouse " : "",
                s->amount[RES_FISH]  == 0 ? "no fish "  : "",
                s->amount[RES_GRAIN] == 0 ? "no grain"  : "",
                p->residents);
        }
    }
}

/* ---- pop_total ----------------------------------------- */
int pop_total(const PopData pop[], int count)
{
    int i, total = 0;
    for (i = 0; i < count; i++)
        if (pop[i].active)
            total += pop[i].residents;
    return total;
}
