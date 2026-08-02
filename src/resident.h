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

/* ---- when a life turns (LIFE_PLAN Phase 7b) ----------------
 * WORKING AND ADULTHOOD ARE TWO DIFFERENT AGES, and tying them to one
 * constant was the single biggest thing holding the working share down.
 * A household was two workers and eight dependants because nobody under
 * eighteen could do anything at all.
 *
 * Twelve is where a youth starts earning — old enough to haul, mend and
 * tend, which is what a marsh village actually asked of its children —
 * and eighteen stays what it was for everything that makes somebody an
 * adult: marriage, and bearing children. A twelve-year-old works. They
 * do not marry, and they do not become a parent.
 *
 * So LIFE_TEEN is the WORKING YOUTH stage now rather than a stage
 * nothing read, and the labour gate accepts it while marriage and
 * fertility go on asking for LIFE_ADULT. */
#define AGE_TEEN_YEARS     12   /* starts work                        */
#define AGE_ADULT_YEARS    18   /* may marry, may bear children        */
#define AGE_RETIRED_YEARS  65

/* Which half of a couple somebody is. Stored rather than derived from
 * the id, unlike the name and the stage — a house has to be founded
 * with one of each, and a derived sex cannot be asked to come out a
 * particular way without hunting for an id that happens to answer
 * correctly, which would make the id sequence depend on the answer. */
typedef enum {
    SEX_FEMALE = 0,
    SEX_MALE   = 1
} Sex;

/* How long a pregnancy runs, in months — which is to say in needs
 * ticks, which is four and a half minutes of play at a six-minute
 * year. Nine because it is nine; there is no tuning here. */
#define PREGNANCY_MONTHS 9

/* Per-mille chance that an eligible couple conceives in a given month.
 * Eligible means married, both under AGE_FERTILE_MAX_YEARS, she is
 * neither carrying nor recovering from her last. A FULL HOUSE IS NOT A
 * REASON TO REFUSE — children are never turned away for want of a bed
 * (see residents_breed).
 *
 * 200 puts the average wait at five months, so a child costs about
 * fourteen months of its parents' time from conception to birth — seven
 * minutes — and a house of two fills its four spare beds in something
 * under half an hour. Fast enough to watch, slow enough that laying a
 * second house is a better idea than waiting. */
#define CONCEIVE_PERMILLE_PER_MONTH 200

/* A person. 48 bytes since Phase 7 — every field added since 6b is a
 * full int32 rather than the byte or two it needs, because this struct
 * is hashed field by field and packing it would introduce padding, and
 * hashing padding is hashing uninitialised memory (the exact failure
 * ci/sanitize.sh's MSan pass exists to catch). At MAX_RESIDENTS that is
 * 24 KB an island, against the half-megabyte an Agent-sized record
 * would have cost. */
typedef struct {
    int      active;         /* slot in use                            */
    int      home_idx;       /* buildings[] slot of the house          */
    uint32_t id;             /* stable on this island; names derive    */
    int32_t  age_months;
    int32_t  spouse;         /* residents[] index, -1 for none         */
    uint32_t tenure_months;  /* at the current workplace               */
    int32_t  sex;            /* Sex                                    */
    int32_t  pregnancy;      /* months left to carry, 0 when not       */
    /* The house this person was BORN in, or -1 for a founder who
     * arrived grown. Its only job is to stop siblings marrying each
     * other: a house is a family now, so the within-house pairing
     * Phase 6 shipped would otherwise marry a brother to his sister the
     * month they both turned eighteen. */
    int32_t  birth_house;
    /* How many children this woman has borne. Nothing is gated on it —
     * fertility is bounded by biology, not by a quota — but it is what
     * lets the feed say "her fourth" and what a test counts. */
    int32_t  children;
    /* Months before she can conceive again. Set to BIRTH_COOLDOWN_MONTHS
     * on delivery and counted down monthly.
     *
     * THIS IS WHAT PACES A FAMILY, and it replaces the child cap an
     * earlier draft of this model used. A cap answers "how many" with a
     * number nobody can defend; a recovery period answers it with a
     * rate, and the number of children falls out of how long she is
     * fertile. */
    int32_t  birth_cooldown;
    /* The sim tick she entered the reserve, or 0 when housed. FIFO is
     * ordered on this and THE CLOCK NEVER RESETS — not when a house is
     * laid for somebody else, and not when a migration moves her to
     * another island. Someone who has waited twenty-three months is
     * twenty-three months in, wherever they are standing. */
    int32_t  reserve_since;
} Resident;

/* Months after a birth before she may conceive again. Twelve, which at
 * a six-minute year is six minutes of play, and which together with the
 * nine of gestation and the conception draw makes a child cost about
 * twenty-six months of its mother's fertile life. */
#define BIRTH_COOLDOWN_MONTHS 12

/* How long somebody waits in the reserve before leaving.
 *
 * THIS IS THE ONLY BOUND ON POPULATION. Fertility is limited by
 * menopause and the recovery period but not by a quota, so a woman
 * bears something like eighteen children over a full life and each
 * daughter does the same — roughly ninefold growth per generation. Left
 * alone that reaches MAX_RESIDENTS in two or three generations whatever
 * the player does. Emigration is what makes the island's population a
 * function of how many roofs have been built instead. */
#define RESERVE_TOLERANCE_MONTHS 24

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

/* How many residents of `home_idx` are old enough to work AND free to.
 *
 * THIS IS WHAT GATES LABOUR, and it does so by the simplest available
 * route: agents_sync spawns one agent per WORKER rather than per
 * resident, so a small child has no agent, therefore no job, therefore
 * no shift. Nothing in agent.c or island.c had to learn what an age is.
 *
 * A WORKER IS TWELVE OR OLDER AND NOT YET RETIRED (Phase 7b) — youths
 * included. The name is kept for the callers' sake; what it counts is
 * hands, not adulthood.
 *
 * A WOMAN CARRYING A CHILD IS NOT COUNTED (Phase 6b), by the same
 * route and for the same reason: she loses her agent, so she keeps no
 * workplace and walks no shift, and the island is short a pair of hands
 * for nine months. That is the cost of the next generation, and it is
 * meant to be felt — a two-person household that is expecting is a
 * one-person household until the child arrives. */
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

/* ---- the reserve (LIFE_PLAN Phase 6c, EXPERIMENT) ----------
 * A resident whose home_idx is this has no roof. They still age, still
 * marry, still die — and still eat, at half a ration of the two staples
 * — but they hold no job, because a job is reached from a house.
 *
 * They come from ONE place: a birth into a household that is already
 * full. That is the whole idea. Births never stop for want of a bed;
 * the bed simply becomes the player's problem, and an unhoused
 * population is a standing cost that converts into a workforce the
 * moment somebody roofs it. */
#define RESIDENT_HOMELESS (-1)

/* Spawns a married founding pair — one woman, one man, both grown —
 * into `home_idx`. This is IMMIGRATION, and Phase 6c makes it a finite
 * resource: the island has a founder allowance and this is the only
 * thing that spends it. Returns 2, or 0 if the island is full.
 *
 * The caller sets pop_data[home_idx].residents; this function does not
 * touch it, because the two callers (a house laid with allowance left,
 * and the test fixtures) disagree about what should happen next. */
int residents_found_pair(Resident residents[], int *count, uint32_t *next_id,
                         int home_idx, uint32_t world_seed);

/* How many people are waiting for a roof. */
int residents_reserve_count(const Resident residents[], int count);

/* Ration cost of the reserve for one month, in whole units of each of
 * the two staples: half a ration each, rounded UP so a reserve of one
 * is never fed for free. */
int residents_reserve_ration(const Resident residents[], int count);

/* Moves people out of the reserve and into `home_idx`. Returns how many
 * were housed: 0, 1 or 2.
 *
 * FIRST COME, FIRST HOUSED. The longest-waiting person in the reserve
 * takes the roof, ordered on `reserve_since`, which never resets. Then,
 * if somebody of the other sex is waiting who is not their sibling,
 * they come too and the pair are married on moving in.
 *
 * ONE IS A VALID ANSWER. If nobody of the other sex is waiting, the
 * first person moves in ALONE and keeps the house until a spouse
 * becomes available — which is why this may also be called on a house
 * that already holds a single unmarried adult, and why it returns 1
 * rather than refusing. A lone occupant is a worker and a claim on a
 * roof; they are simply not yet a household. */
int residents_settle_house(Resident residents[], int count, int home_idx);

/* ---- what a worker is worth (LIFE_PLAN Phase 8) ------------
 * Slept, ate, married, employed -> an integer percentage. A hundred is
 * an ordinary worker; the band runs from PRODUCTIVITY_MIN to
 * PRODUCTIVITY_MAX.
 *
 * INTEGER PERCENT, NOT A FLOAT. This multiplies production, production
 * is hashed, and a float accumulated into hashed state fails as two
 * machines disagreeing rather than as a wrong answer on one.
 *
 * FOUR INDEPENDENT INPUTS, which is the rule LIFE_PLAN section 6 asks
 * for: "status must be several independent inputs, so one bad harvest
 * cannot move every one of them at once". A famine moves `fed` and
 * nothing else; a young island moves `prime` and nothing else. Each is
 * worth little alone and they do not share a cause.
 *
 * WHAT SECTION 5 SAYS ABOUT THE FLOOR, and why it is 85 rather than the
 * 75 written there: modifiers multiply the closure ratio by 1/p, and at
 * the working share Phase 7b measures the base tier sits close enough
 * to the wall that a deep floor would put it over. That calculation is
 * a projection over the def table rather than a measurement, so it is
 * not treated as a specification — but it is a reason to keep the
 * downward half of the band shallower than the upward half. */
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

/* One resident's productivity, as a percentage.
 *
 * `happiness` is their household's, 0..HAPPINESS_MAX — the one input
 * that comes from outside the person. Everything else is read off the
 * Resident. Clamped to the band, so a caller cannot produce a rate the
 * economy was never balanced against. */
int resident_productivity(const Resident *r, int happiness);

/* The average productivity of everybody of working age at `home_idx`,
 * as a percentage, or PRODUCTIVITY_BASE when nobody lives there.
 *
 * PER HOUSEHOLD RATHER THAN PER PERSON, because an Agent carries no
 * link back to the Resident who is walking it — one agent is spawned
 * per working-age resident of a house, and which one it is was never
 * recorded. Averaging over the household is the granularity that link
 * actually supports, and adding the link would mean widening a struct
 * that is snapshotted for a number that changes every month. */
int residents_house_productivity(const Resident residents[], int count,
                                 int home_idx, int happiness);

/* Who inherits `home`, or -1 (LIFE_PLAN Phase 7b).
 *
 * A HOUSE IS A LINE, NOT A TENANCY. The elders of a house are whoever
 * was NOT born in it — the founding pair, and any spouse who married
 * in. While one of them lives the house is theirs and the children
 * marry out as normal. When the last is gone, the eldest adult child
 * born there inherits: they keep the house and bring a spouse INTO it,
 * and their younger siblings marry out.
 *
 * That is what makes a hundred founder places buy a hundred LINES
 * rather than a hundred marriages, and it is why an unmarried sibling
 * still under the roof is part of a household rather than something the
 * model has to dispose of. */
int residents_heir_of(const Resident residents[], int count, int home);

/* Everybody who has waited longer than RESERVE_TOLERANCE_MONTHS leaves.
 *
 * `relocate(ctx, resident_index)` is asked first, and is the caller's
 * chance to move somebody to another island with room rather than lose
 * them — the player's own islands before anybody else's. It returns 1
 * if it took them. Only when it declines is the person removed from the
 * world.
 *
 * Returns how many left the island (relocated or removed), which is
 * what the vitals strip reports as "people are leaving". */
int residents_emigrate(Resident residents[], int count, uint64_t tick,
                       int (*relocate)(void *ctx, int idx), void *ctx);

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
/* THE COUNTS ARE NOT OPTIONAL. A marriage is a change of address —
 * somebody moves in with somebody else, or the couple join the reserve
 * — so two pop_data counts move with every match. An earlier draft kept
 * a short form for tests with no PopData to offer, and it quietly sent
 * every cross-household couple to the reserve because with no counts
 * there was never any room anywhere. Better to have one function that
 * cannot be called wrongly. */
void residents_marry(Resident residents[], int count, PopData pop_data[],
                     int building_count, uint32_t world_seed, uint64_t tick);

/* Is there a couple at `home_idx` young enough to have a child? */
int residents_fertile_couple_at(const Resident residents[], int count,
                                int home_idx);

/* Conception, gestation and birth — one month of it (LIFE_PLAN Phase
 * 6b). Call once per needs tick, from the same calendar trigger as
 * residents_age and after it.
 *
 * BIRTH IS NOW THE ONLY WAY A HOUSE GROWS. Phase 6 made a birth a
 * question of WHO filled a slot that happiness had already opened;
 * Phase 6b removes that slot entirely — pop_update no longer grows a
 * house at all — so this function drives pop_data[].residents UP the
 * same way residents_age drives it DOWN. Both directions are
 * resident-driven now, which is simpler than the split Phase 5 needed
 * and is only possible because nobody immigrates into an existing
 * house any more.
 *
 * A house still empties when it starves: decline stays in pop_update,
 * where it always was. Growth and decline are no longer symmetric, and
 * that is the point — leaving is a decision about this month, being
 * born takes nine of them. */
void residents_breed(Resident residents[], int *count, uint32_t *next_id,
                     PopData pop_data[], int building_count,
                     uint32_t world_seed, uint64_t tick);

#endif /* RESIDENT_H */
