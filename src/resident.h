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
#include "calendar.h" /* MONTHS_PER_YEAR: an age is in months */
#include <stddef.h>
#include <stdint.h>

/* One agent per resident today, so the two ceilings are the same one
 * and should stay that way: a resident who cannot have an Agent cannot
 * go to work, which is a silent failure rather than a loud one. When
 * ageing arrives and only adults commute this can rise — that is the
 * point of splitting them (LIFE_PLAN, "Residents are world state
 * now"). */
#define MAX_RESIDENTS MAX_AGENTS

/* Ages are in MONTHS, and a month is one needs tick — see calendar.h,
 * which owns that definition since Phase 4 made it official. Phase 3
 * stored months in advance precisely so this format would not move
 * again when the calendar arrived, and it did not. */

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

/* ---- how long a life is (LIFE_PLAN Phase 5) ---------------
 * Stellaris's shape, because it is the one that works: a GUARANTEED
 * span nobody dies before, then a rising monthly chance after it. The
 * alternative — everybody dies on their birthday at exactly N — is how
 * a population ends up dying in cohorts even when its ages are spread,
 * because a chain staffed by people hired the same year still loses
 * them the same year.
 *
 * At a six-minute year (calendar.h) seventy years is seven hours of
 * play, so most residents outlive the session they were born into.
 * That is the intent: you inherit people mid-life and they die on you
 * at a moment you did not choose. */
#define LIFE_GUARANTEED_YEARS  70

/* Per-mille chance of dying in a given month, once past the guarantee
 * and rising with every year beyond it. 10 + 5*years is about 1% a
 * month at seventy and 3.5% at seventy-five, which puts the typical
 * death in the mid-seventies and makes a hundred-year-old rare rather
 * than impossible. */
#define LIFE_DEATH_BASE_PERMILLE   10
#define LIFE_DEATH_RISE_PERMILLE    5
#define LIFE_DEATH_MAX_PERMILLE   500

/* ---- marriage and birth (LIFE_PLAN Phase 6) ---------------
 * A HOUSE IS THE HOUSEHOLD. Pairing happens between people who already
 * share an address, which is what lets marriage cost nothing in
 * bookkeeping: no resident ever changes `home_idx`, so no house's
 * pop_data.residents has to be moved from one slot to another in the
 * same tick that residents_sync is reconciling against it. Island-wide
 * pairing with a spouse moving in was the alternative, and it buys a
 * nicer story for a reconciliation bug that would be very hard to see.
 *
 * MARRIAGE IS A MONTHLY DRAW, NOT AN IMMEDIATE FACT, and for the same
 * reason ages are jittered at spawn: if every eligible pair married on
 * the first month they were eligible, every couple would start having
 * children in the same month, and the island would be back to raising
 * cohorts — the exact defect the age spread exists to prevent. */
#define MARRY_PERMILLE_PER_MONTH  120

/* Births stop well before work does. Without this a couple would go on
 * having children until the day they retire at 65, which is not a
 * population model so much as an arithmetic accident. */
#define AGE_FERTILE_MAX_YEARS  45

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

/* How many residents of `home_idx` are old enough to work.
 *
 * THIS IS WHAT GATES LABOUR, and it does so by the simplest available
 * route: agents_sync spawns one agent per ADULT rather than per
 * resident, so a child has no agent, therefore no job, therefore no
 * shift. Nothing in agent.c or island.c had to learn what an age is. */
int residents_adults_at(const Resident residents[], int count, int home_idx);

/* Effective mouths at `home_idx` for the needs tick: an adult eats a
 * whole ration and everybody else a half, rounded UP so a house of
 * children is never fed for free.
 *
 * Raw goods only — refined goods are charged per household and the
 * number of mouths never entered their cost, which is why this lever
 * is worth much less than LIFE_PLAN first assumed (see its §4). */
int residents_mouths_at(const Resident residents[], int count, int home_idx);

/* One month older, and some of them die of it.
 *
 * Deaths are RESIDENT-DRIVEN and decrement pop_data[home].residents as
 * they happen, which is the opposite direction from growth: growth is
 * decided by the needs tick and residents_sync follows it. Keeping the
 * two directions separate is what stops the reconciliation fighting
 * itself — a death that only removed a Resident would be undone by the
 * next sync, and one that only decremented the count would kill an
 * arbitrary person rather than the old one.
 *
 * Call once per needs tick (one month). `tick` salts the death draw so
 * the same resident is not asked the same question twice. */
void residents_age(Resident residents[], int count, PopData pop_data[],
                   uint32_t world_seed, uint64_t tick);

/* Pairs unmarried adults who share a house, in index order, each pair
 * on a monthly draw (LIFE_PLAN Phase 6). Call once per needs tick from
 * the same calendar trigger as residents_age, and AFTER it: somebody
 * who died this month should not marry this month.
 *
 * `spouse` is a residents[] INDEX and stays valid because both removal
 * paths — death and despawn — widow the partner before clearing the
 * slot. Every read of it is still bounds- and reciprocity-checked, so a
 * snapshot from a future that got this wrong degrades to "unmarried"
 * rather than to a stranger's marriage. */
void residents_marry(Resident residents[], int count,
                     uint32_t world_seed, uint64_t tick);

/* Is there a couple at `home_idx` young enough to have a child?
 *
 * THIS IS WHAT MAKES A BIRTH A BIRTH. Growth stays exactly where it
 * was — count-driven, decided by pop_update from happiness — and this
 * decides only WHO ARRIVES to fill the slot it opened: a newborn where
 * a fertile couple lives, an immigrant adult otherwise. So Phase 6 adds
 * no second source of population, needs no rebalance of the economy,
 * and leaves "deaths are resident-driven, growth is count-driven"
 * exactly as Phase 5 left it.
 *
 * The consequence is the one LIFE_PLAN predicted: houses with couples
 * fill with children who eat half a ration and hold no job, so the
 * adult fraction falls and the closure projection starts doing real
 * work. */
int residents_fertile_couple_at(const Resident residents[], int count,
                                int home_idx);

#endif /* RESIDENT_H */
