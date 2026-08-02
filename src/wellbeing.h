#ifndef WELLBEING_H
#define WELLBEING_H

/* wellbeing.h -- the six-factor wellbeing projection.
 *
 * A READ-ONLY PROJECTION OVER THE UI SNAPSHOT. Nothing here is hashed,
 * saved or sent, so floats are safe: no two machines ever have to agree
 * on a score. The one place wellbeing crosses into the sim is
 * resident_productivity(), which is integer and lives in resident.c.
 *
 * Design: docs/new-happiness-design.md, docs/LIFE_PLAN.md. */

#include "ui_snapshot.h"
#include "population.h"
#include "resident.h"

typedef enum {
    WB_SOCIAL = 0,
    WB_INCOME,
    WB_FREEDOM,
    WB_HEALTH,
    WB_CORRUPTION,
    WB_GENEROSITY,
    WB_FACTOR_COUNT
} WellbeingFactor;

/* One factor's weight and whether anything in the game backs it.
 *
 * `modelled` is 0 for the two the sim has no system for: there is no
 * corruption and no charitable giving, so scoring them would be scoring
 * a constant. Their weight is excluded from the total rather than
 * contributed as a fixed amount, and building either one later is a
 * change to this table plus a case in wellbeing_island(). */
typedef struct {
    const char *name;
    float       weight;
    int         modelled;
} WellbeingFactorDef;

/* Every weight, range and curve constant in the projection. */
extern const WellbeingFactorDef WELLBEING_FACTORS[WB_FACTOR_COUNT];

#define WB_SCALE_MAX      10.0f  /* the ladder the score is scaled to  */
#define WB_BASELINE       1.0f   /* floor, so nothing scores zero      */
#define WB_DENSITY_PEAK   6.0f   /* household size the curve peaks at  */
#define WB_TENURE_FULL    60.0f  /* months to a full tenure term       */
/* The best wage anyone can earn: the flat rate times the top of the
 * productivity band. Derived rather than written down, because a
 * constant here that did not track WAGE_PER_WORKER would flatten the
 * income factor to a fixed value without anything saying so. */
#define WB_WAGE_MAX ((float)WAGE_PER_WORKER * (float)PRODUCTIVITY_MAX / 100.0f)

typedef struct {
    float factor[WB_FACTOR_COUNT];   /* 0..1; 0 for anything unmodelled */
    float score;                     /* 0..WB_SCALE_MAX                 */
} Wellbeing;

/* Scores one island as a whole. */
void wellbeing_island(Wellbeing *out, const UiSnapshot *snap, int island);

/* Scores one member of the cast, using their island for the terms that
 * are a property of the place rather than the person. */
void wellbeing_resident(Wellbeing *out, const UiSnapshot *snap, int island,
                        const UiResident *r);

/* Display name for a factor ("Social support"). Never NULL. */
const char *wellbeing_factor_name(int factor);

/* A one-line description of `r`, for the cast list:
 * "Bess Cobbleworth, 61, has worked the Fisher's Hut for 19 years".
 * Writes at most `out_len` bytes and always terminates. */
void wellbeing_describe(const UiResident *r, char *out, size_t out_len);

#endif /* WELLBEING_H */
