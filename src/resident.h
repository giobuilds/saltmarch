#ifndef RESIDENT_H
#define RESIDENT_H

/* resident.h -- named residents: identity, ageing, marriage, birth,
 * work and death. Identity only; Agent (agent.h) owns motion.
 * Design and history: docs/LIFE_PLAN.md. */

#include "population.h"
#include "building.h"
#include "agent.h"   /* MAX_AGENTS: one agent per resident, see below */
#include "calendar.h" /* MONTHS_PER_YEAR: an age is in months */
#include <stddef.h>
#include <stdint.h>

/* One agent per resident today, so the two ceilings are the same one */
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

/* ---- when a life turns (LIFE_PLAN Phase 7b) ----------------
 * WORKING AND ADULTHOOD ARE TWO DIFFERENT AGES, and tying them to one
 * constant was the single biggest thing holding the working share down. */
#define AGE_TEEN_YEARS     12   /* starts work                        */
#define AGE_ADULT_YEARS    18   /* may marry, may bear children        */
#define AGE_RETIRED_YEARS  65

/* Which half of a couple somebody is. Stored rather than derived. */
typedef enum {
    SEX_FEMALE = 0,
    SEX_MALE   = 1
} Sex;

/* How long a pregnancy runs, in months — which is to say in needs
 * ticks, which is four and a half minutes of play at a six-minute
 * year. Nine because it is nine; there is no tuning here. */
#define PREGNANCY_MONTHS 9

/* Per-mille chance that an eligible couple conceives in a given month. */
#define CONCEIVE_PERMILLE_PER_MONTH 200

/* A person. 48 bytes since Phase 7 — every field added since 6b is. */
typedef struct {
    int      active;         /* slot in use                            */
    int      home_idx;       /* buildings[] slot of the house          */
    uint32_t id;             /* stable on this island; names derive    */
    int32_t  age_months;
    int32_t  spouse;         /* residents[] index, -1 for none         */
    uint32_t tenure_months;  /* at the current workplace               */
    int32_t  sex;            /* Sex                                    */
    int32_t  pregnancy;      /* months left to carry, 0 when not       */
    /* The house this person was BORN in, or -1 for a founder who */
    int32_t  birth_house;
    /* How many children this woman has borne. Nothing is gated on it —
     * fertility is bounded by biology, not by a quota — but it is what
     * lets the feed say "her fourth" and what a test counts. */
    int32_t  children;
    /* Months before she can conceive again. Set to BIRTH_COOLDOWN_MONTHS
     * on delivery and counted down monthly. */
    int32_t  birth_cooldown;
    /* The sim tick she entered the reserve, or 0 when housed. FIFO. */
    int32_t  reserve_since;
} Resident;

/* Months after a birth before she may conceive again. Twelve, which at
 * a six-minute year is six minutes of play, and which together with the
 * nine of gestation and the conception draw makes a child cost about
 * twenty-six months of its mother's fertile life. */
#define BIRTH_COOLDOWN_MONTHS 12

/* How long somebody waits in the reserve before leaving. */
#define RESERVE_TOLERANCE_MONTHS 24

/* ---- how long a life is (LIFE_PLAN Phase 5) --------------- */
#define LIFE_GUARANTEED_YEARS  70

/* Per-mille chance of dying in a given month, once past the guarantee */
#define LIFE_DEATH_BASE_PERMILLE   10
#define LIFE_DEATH_RISE_PERMILLE    5
#define LIFE_DEATH_MAX_PERMILLE   500

/* ---- marriage and birth (LIFE_PLAN Phase 6) --------------- */
#define MARRY_PERMILLE_PER_MONTH  120

/* Births stop well before work does. Without this a couple would go on
 * having children until the day they retire at 65, which is not a
 * population model so much as an arithmetic accident. */
/* Menopause. Sixty rather than the forty-five an earlier draft used:
 * fertility should end because a body ends it, not because the model
 * needed a brake. The brake is BIRTH_COOLDOWN_MONTHS and, above all,
 * RESERVE_TOLERANCE_MONTHS. */
#define AGE_FERTILE_MAX_YEARS  60

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
 * house grows and removing them when it shrinks. */
void residents_sync(Resident residents[], int *count, uint32_t *next_id,
                    const Building buildings[], const PopData pop_data[],
                    int building_count, uint32_t world_seed,
                    const int live[]);

/* How many residents of `home_idx` are old enough to work AND free to. */
int residents_adults_at(const Resident residents[], int count, int home_idx);

/* Tallies every house in one pass: `live[h]` how many residents name h */
void residents_tally(const Resident residents[], int count,
                     int building_count, const int happiness[],
                     int live[], int workers[], int prod[]);

/* Effective mouths at `home_idx` for the needs tick: an adult eats a
 * whole ration and everybody else a half, rounded UP so a house of
 * children is never fed for free. */
int residents_mouths_at(const Resident residents[], int count, int home_idx);

/* One month older, and some of them die of it. */
void residents_age(Resident residents[], int count, PopData pop_data[],
                   uint32_t world_seed, uint64_t tick);

/* ---- the reserve (LIFE_PLAN Phase 6c, EXPERIMENT) ---------- */
#define RESIDENT_HOMELESS (-1)

/* Spawns a married founding pair — one woman, one man, both grown — */
int residents_found_pair(Resident residents[], int *count, uint32_t *next_id,
                         int home_idx, uint32_t world_seed);

/* How many people are waiting for a roof. */
int residents_reserve_count(const Resident residents[], int count);

/* Ration cost of the reserve for one month, in whole units of each of
 * the two staples: half a ration each, rounded UP so a reserve of one
 * is never fed for free. */
int residents_reserve_ration(const Resident residents[], int count);

/* Moves people out of the reserve and into `home_idx`. Returns how many
 * were housed: 0, 1 or 2. */
int residents_settle_house(Resident residents[], int count, int home_idx);

/* ---- what a worker is worth (LIFE_PLAN Phase 8) ------------ */
#define PRODUCTIVITY_BASE   100
#define PRODUCTIVITY_MIN     85
#define PRODUCTIVITY_MAX    140

/* What each input is worth. Deliberately small and deliberately
 * uneven: no single one of them decides anything. */
#define PROD_PRIME_BONUS     10   /* grown, and not yet retired        */
#define PROD_MARRIED_BONUS    5   /* somebody to come home to          */
#define PROD_FED_BONUS       10   /* more than the basics              */
#define PROD_HUNGRY_PENALTY  15   /* the house is below neutral        */
#define PROD_TIRED_PENALTY    5   /* carrying a child, or very old     */
#define PROD_TENURE_MAX      15   /* knowing the work                  */
/* Months at one workplace to earn the whole tenure bonus. Five years:
 * long enough that a settled crew is worth protecting, short enough
 * that a player sees it inside a session. */
#define PROD_TENURE_FULL     60

/* One resident's productivity, as a percentage. */
int resident_productivity(const Resident *r, int happiness);

/* The average productivity of everybody of working age at `home_idx`,
 * as a percentage, or PRODUCTIVITY_BASE when nobody lives there. */
int residents_house_productivity(const Resident residents[], int count,
                                 int home_idx, int happiness);

/* Who inherits `home`, or -1 (LIFE_PLAN Phase 7b). */
int residents_heir_of(const Resident residents[], int count, int home);

/* Everybody who has waited longer than RESERVE_TOLERANCE_MONTHS leaves. */
int residents_emigrate(Resident residents[], int count, uint64_t tick,
                       int (*relocate)(void *ctx, int idx), void *ctx);

/* Pairs unmarried adults who share a house, in index order, each pair */
/* THE COUNTS ARE NOT OPTIONAL. A marriage is a change of address — */
void residents_marry(Resident residents[], int count, PopData pop_data[],
                     int building_count, uint32_t world_seed, uint64_t tick);

/* Is there a couple at `home_idx` young enough to have a child? */
int residents_fertile_couple_at(const Resident residents[], int count,
                                int home_idx);

/* Conception, gestation and birth — one month of it (LIFE_PLAN Phase
 * 6b). Call once per needs tick, from the same calendar trigger as
 * residents_age and after it. */
void residents_breed(Resident residents[], int *count, uint32_t *next_id,
                     PopData pop_data[], int building_count,
                     uint32_t world_seed, uint64_t tick);

#endif /* RESIDENT_H */
