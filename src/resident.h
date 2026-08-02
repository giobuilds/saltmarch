#ifndef RESIDENT_H
#define RESIDENT_H

/* =========================================================
 * resident.h  --  a resident is a person  (LIFE_PLAN Phase 3)
 *
 * Until this existed, a resident was an integer. PopData.residents
 * counted them and the Agent that walked to work was derived state,
 * rebuilt from that count every session — so nobody had a name, an age
 * or a history, and nothing that happened to an island could happen to
 * a PERSON.
 *
 * IDENTITY IS SPLIT FROM MOTION, deliberately and from the first
 * commit. Agent is 1060 bytes and 1024 of that is path[]: derived,
 * never saved, rebuilt every session. A Resident is 24, hashed, saved
 * and snapshotted. Putting a name inside Agent would have meant paying
 * half a megabyte to store what a kilobyte of names weighs, and
 * serialising a pathfinding cursor to keep it.
 *
 * NAMES ARE DERIVED, NOT STORED. This simulation has no mutable RNG
 * stream — outcomes come from hashing an identity (see survey_hash in
 * survey.c), which is stronger than a stream because there is no shared
 * cursor to step out of order. A resident's name is therefore a pure
 * function of (world_seed, id): reproducible on every machine, absent
 * from the snapshot, and costing two table lookups to ask for.
 *
 * PHASE 3 IS DELIBERATELY INERT. Residents are created, named, aged at
 * birth and carried through save, snapshot and hash — and then nothing
 * reads them. Ageing, work, marriage and death are Phases 4-7. The
 * serialisation is proven before anything depends on it, because a
 * format is far cheaper to get right than to change.
 * ========================================================= */

#include "population.h"
#include "building.h"
#include "agent.h"   /* MAX_AGENTS: one agent per resident, see below */
#include <stddef.h>
#include <stdint.h>

/* One agent per resident today, so the two ceilings are the same one
 * and should stay that way: a resident who cannot have an Agent cannot
 * go to work, which is a silent failure rather than a loud one. When
 * ageing arrives and only adults commute this can rise — that is the
 * point of splitting them (LIFE_PLAN, "Residents are world state
 * now"). */
#define MAX_RESIDENTS MAX_AGENTS

/* Months, because a month is the needs tick and the needs tick is what
 * everything in this economy already runs on (LIFE_PLAN, "The
 * calendar"). Phase 4 makes that official; storing months now means the
 * save format does not move again when it does. */
#define MONTHS_PER_YEAR 12

/* The four stages. DERIVED from age rather than stored: one number
 * cannot disagree with itself, and a stage field would be a second
 * place for the truth to live. */
typedef enum {
    LIFE_INFANT = 0,
    LIFE_TEEN,
    LIFE_ADULT,
    LIFE_RETIRED,
    LIFE_STAGE_COUNT
} LifeStage;

#define AGE_TEEN_YEARS     13
#define AGE_ADULT_YEARS    18
#define AGE_RETIRED_YEARS  65

/* A person. 24 bytes, and the width is not an accident — this is
 * per-resident state that goes into every snapshot, so at MAX_RESIDENTS
 * it is 12 KB an island rather than the half-megabyte an Agent-sized
 * record would have cost. */
typedef struct {
    int      active;         /* slot in use                            */
    int      home_idx;       /* buildings[] slot of the house          */
    uint32_t id;             /* stable on this island; names derive    */
    int32_t  age_months;
    int32_t  spouse;         /* residents[] index, -1 for none         */
    uint32_t tenure_months;  /* at the current workplace               */
} Resident;

/* Which stage `r` is in, from its age alone. */
int resident_stage(const Resident *r);

/* Display name for a stage ("adult"). Never NULL. */
const char *life_stage_name(int stage);

/* Writes this resident's full name into `out`. Derived from
 * (world_seed, id), so it is the same on every machine that replays the
 * same log and costs nothing in the snapshot. */
void resident_name(const Resident *r, uint32_t world_seed,
                   char *out, size_t out_len);

/* Reconciles residents[] against every house's pop_data.residents,
 * exactly as agents_sync() reconciles agents — spawning people when a
 * house grows and removing them when it shrinks.
 *
 * *next_id is this island's monotonic id counter and IS world state:
 * it decides what everybody is called, so it is hashed and saved like
 * anything else the world remembers.
 *
 * NEW RESIDENTS ARRIVE AS ADULTS OF SPREAD AGES, which is an invariant
 * and not a flourish. Residents born or spawned together age together,
 * become adults together, retire together and DIE TOGETHER, taking
 * every worker in a chain with them at once — the defect Cities:
 * Skylines is best known for in its population model, and the one
 * Stellaris avoids by recruiting leaders at a randomised 28-50. This
 * game is primed for it: pop_init seeds a house at five and growth adds
 * one per needs tick, so six cottages laid in a minute would otherwise
 * be thirty people of identical age. Seeding the spread here, where the
 * array is being created, costs one hash; retrofitting it after ageing
 * ships would be an economy-breaking change. */
void residents_sync(Resident residents[], int *count, uint32_t *next_id,
                    const Building buildings[], const PopData pop_data[],
                    int building_count, uint32_t world_seed);

#endif /* RESIDENT_H */
