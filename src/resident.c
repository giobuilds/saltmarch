/*  resident.c  --  a resident is a person  (LIFE_PLAN Phase 3)
 *
 *  See resident.h for why identity is split from motion and why names
 *  are derived rather than stored.
 */

#include "resident.h"
#include "simlog.h"
#include <stdio.h>
#include <string.h>

/* ---- names ------------------------------------------------
 * Marsh-flavoured and deliberately plain: these are fisherfolk and
 * farmhands, not heroes. 48 x 40 is 1920 combinations, which is more
 * than an island can hold, so a player rarely meets the same name twice
 * — and the collisions that do happen read as a family rather than as a
 * bug, which is the right failure. */
/* SPLIT BY SEX SINCE PHASE 6b. Residents have a sex now because a
 * household is founded by a couple and only one half of one can be
 * pregnant, and a table that answered "Bess" for a man would put that
 * fact on screen as a mistake the moment anything displayed either.
 * The two lists are deliberately the same length, so neither sex draws
 * from a smaller pool and reads as less varied. */
static const char *const FIRST_NAMES_F[] = {
    "Bess",  "Maud",  "Ivy",   "Neve",  "Sela",  "Nan",   "Tilda", "Marta",
    "Peg",   "Ada",   "Elsie", "Hettie","Winna", "Bryde", "Salla", "Grea",
    "Lisbet","Nell",  "Cass",  "Juna",  "Wren",  "Dove",  "Iris",  "Merrit"
};
static const char *const FIRST_NAMES_M[] = {
    "Cully", "Tam",   "Hob",   "Wick",  "Bram",  "Orrin", "Fen",   "Gil",
    "Colm",  "Rusk",  "Dorn",  "Sarn",  "Kell",  "Aldo",  "Torr",  "Odd",
    "Harl",  "Pike",  "Rowan", "Silas", "Ansel", "Mabb",  "Teague","Corwin"
};
static const char *const SURNAMES[] = {
    "Cobbleworth", "Marsh",     "Tidewell",   "Fenn",      "Saltley",
    "Reedy",       "Bracken",   "Netherby",   "Quill",     "Harrow",
    "Sedgewick",   "Loam",      "Alder",      "Waverly",   "Pike",
    "Thatcher",    "Wickham",   "Ebb",        "Furlong",   "Bogle",
    "Standish",    "Crane",     "Millward",   "Osier",     "Thorne",
    "Drift",       "Halloway",  "Peat",       "Ashby",     "Cordwain",
    "Rushton",     "Brine",     "Gullet",     "Weatherall","Stapley",
    "Ferrier",     "Winnow",    "Larkspur",   "Dunmore",   "Kittle"
};
#define FIRST_COUNT (int)(sizeof(FIRST_NAMES_F) / sizeof(FIRST_NAMES_F[0]))
#define SUR_COUNT   (int)(sizeof(SURNAMES)      / sizeof(SURNAMES[0]))

/* FNV-1a over (world_seed, id, salt) with a murmur3 finaliser, the same
 * shape survey.c uses and for the same reasons: integer-only so every
 * platform agrees, byte-wise so a small id does not leave the low bits
 * doing all the work, and avalanched at the end because callers read
 * the LOW bits with `%` and FNV diffuses badly into those.
 *
 * A different salt per question makes "what is your first name", "what
 * is your surname" and "how old are you" independent — without it every
 * Bess would be the same age, which is the sort of pattern a player
 * notices long before they can say why. */
static uint32_t resident_hash(uint32_t world_seed, uint32_t id, uint32_t salt)
{
    uint32_t h = 2166136261u;
    uint32_t parts[3];
    int      i;

    parts[0] = world_seed;
    parts[1] = id;
    parts[2] = salt;

    for (i = 0; i < 3; i++) {
        uint32_t v = parts[i];
        int      b;
        for (b = 0; b < 4; b++) {
            h ^= (v >> (b * 8)) & 0xFFu;
            h *= 16777619u;
        }
    }

    h ^= h >> 16;
    h *= 0x85EBCA6Bu;
    h ^= h >> 13;
    h *= 0xC2B2AE35u;
    h ^= h >> 16;
    return h;
}

int resident_stage(const Resident *r)
{
    int years;

    if (!r) return LIFE_ADULT;
    years = r->age_months / MONTHS_PER_YEAR;

    if (years < AGE_TEEN_YEARS)    return LIFE_INFANT;
    if (years < AGE_ADULT_YEARS)   return LIFE_TEEN;
    if (years < AGE_RETIRED_YEARS) return LIFE_ADULT;
    return LIFE_RETIRED;
}

const char *life_stage_name(int stage)
{
    static const char *const NAMES[LIFE_STAGE_COUNT] = {
        [LIFE_INFANT]  = "child",
        [LIFE_TEEN]    = "youth",
        [LIFE_ADULT]   = "adult",
        [LIFE_RETIRED] = "elder"
    };
    if (stage < 0 || stage >= LIFE_STAGE_COUNT || !NAMES[stage])
        return "adult";
    return NAMES[stage];
}

void resident_name(const Resident *r, uint32_t world_seed,
                   char *out, size_t out_len)
{
    uint32_t f, s;

    if (!out || out_len == 0) return;
    if (!r) { out[0] = '\0'; return; }

    f = resident_hash(world_seed, r->id, 0x1111u) % (uint32_t)FIRST_COUNT;
    s = resident_hash(world_seed, r->id, 0x2222u) % (uint32_t)SUR_COUNT;

    snprintf(out, out_len, "%s %s",
             r->sex == SEX_MALE ? FIRST_NAMES_M[f] : FIRST_NAMES_F[f],
             SURNAMES[s]);
}

/* ---- sync -------------------------------------------------
 * Mirrors agents_sync (agent.c) deliberately: same shape, same
 * house-by-house reconciliation, so the two cannot drift apart in how
 * they decide a house has gained or lost somebody. */

static int count_live_for_home(const Resident r[], int count, int home_idx)
{
    int i, n = 0;
    for (i = 0; i < count; i++)
        if (r[i].active && r[i].home_idx == home_idx) n++;
    return n;
}

/* Severs `idx`'s marriage from the OTHER side, so no live resident is
 * ever left pointing at a slot that has been cleared or reused.
 *
 * Reciprocity is checked rather than assumed: only a partner who names
 * `idx` back is widowed. Without that a stale one-way link — from an
 * older snapshot, or from a bug — would let one death silently divorce
 * a couple it had nothing to do with. */
static void widow_partner(Resident r[], int count, int idx)
{
    int sp = r[idx].spouse;

    if (sp >= 0 && sp < count && r[sp].spouse == idx) r[sp].spouse = -1;
    r[idx].spouse = -1;
}

/* A FOUNDER arrives grown, aged 20-31 and spread. The spread is the
 * anti-cohort invariant from resident.h; the CEILING is new in Phase
 * 6b and is the other half of the same concern. Founders used to
 * arrive at 20-45, which was harmless when a house was five unrelated
 * strangers and growth came off a boat. Now the founding pair are the
 * only fertile couple a house will ever have, so a mother who lands at
 * 44 has one year of fertility and the household never fills. Twenty
 * to thirty-one leaves fifteen-odd years of it: several children, and
 * still a decade of spread between two neighbouring houses.
 *
 * A NEWBORN is age zero, with no jitter, because a baby's age is not a
 * sample of anything — it is the one age a person is genuinely known
 * to be.
 *
 * Returns the slot used, or -1 if the island is full. */
static int spawn_resident(Resident r[], int *count, uint32_t *next_id,
                          int home_idx, uint32_t world_seed,
                          int newborn, int sex)
{
    int      slot = -1, i;
    uint32_t id, years, months;

    for (i = 0; i < *count; i++)
        if (!r[i].active) { slot = i; break; }
    if (slot < 0) {
        if (*count >= MAX_RESIDENTS) return -1;  /* soft cap, like agents */
        slot = (*count)++;
    }

    id     = (*next_id)++;
    years  = 20u + resident_hash(world_seed, id, 0x3333u) % 12u;
    months = resident_hash(world_seed, id, 0x4444u) % (uint32_t)MONTHS_PER_YEAR;

    memset(&r[slot], 0, sizeof(r[slot]));
    r[slot].active      = 1;
    r[slot].home_idx    = home_idx;
    r[slot].id          = id;
    r[slot].age_months  = newborn
                        ? 0
                        : (int32_t)(years * MONTHS_PER_YEAR + months);
    r[slot].spouse      = -1;
    r[slot].pregnancy   = 0;
    r[slot].birth_house = newborn ? home_idx : -1;
    /* A founder is told which half of the couple to be; a newborn is
     * asked, and the coin is the id it was just given. */
    r[slot].sex = sex >= 0
                ? sex
                : (int32_t)(resident_hash(world_seed, id, 0x7777u) & 1u);
    return slot;
}

/* Removes the LAST matching resident, which is deterministic and is all
 * that is required while nothing distinguishes one from another. Phase 5
 * replaces this: once people age, who leaves stops being arbitrary. */
static void despawn_one_for_home(Resident r[], int count, int home_idx)
{
    int i;
    for (i = count - 1; i >= 0; i--)
        if (r[i].active && r[i].home_idx == home_idx) {
            /* Before the slot goes, so a surviving spouse is not left
             * pointing into it — the slot is reused by the next
             * arrival, and a stale index would marry a widow to a
             * stranger who moved in (LIFE_PLAN Phase 6). */
            widow_partner(r, count, i);
            r[i].active = 0;
            return;
        }
}

int residents_adults_at(const Resident r[], int count, int home_idx)
{
    int i, n = 0;
    for (i = 0; i < count; i++)
        if (r[i].active && r[i].home_idx == home_idx
            && r[i].pregnancy == 0                 /* carrying: not free */
            && resident_stage(&r[i]) == LIFE_ADULT) n++;
    return n;
}

int residents_mouths_at(const Resident r[], int count, int home_idx)
{
    int i, adults = 0, others = 0;

    for (i = 0; i < count; i++) {
        if (!r[i].active || r[i].home_idx != home_idx) continue;
        if (resident_stage(&r[i]) == LIFE_ADULT) adults++;
        else                                     others++;
    }
    /* Rounded up: half of three is two here, not one. A shortage should
     * cost the island, not be quietly forgiven by integer division. */
    return adults + (others + 1) / 2;
}

/* Past the guarantee, a rising monthly chance. Salted with the tick so
 * a resident is asked a fresh question each month rather than the same
 * one forever — without that, whoever survived their first roll would
 * survive every roll and live indefinitely. */
static int resident_dies(const Resident *r, uint32_t world_seed, uint64_t tick)
{
    int years = r->age_months / MONTHS_PER_YEAR;
    int past, permille;

    if (years < LIFE_GUARANTEED_YEARS) return 0;

    past     = years - LIFE_GUARANTEED_YEARS;
    permille = LIFE_DEATH_BASE_PERMILLE + LIFE_DEATH_RISE_PERMILLE * past;
    if (permille > LIFE_DEATH_MAX_PERMILLE) permille = LIFE_DEATH_MAX_PERMILLE;

    return (int)(resident_hash(world_seed,
                               r->id ^ (uint32_t)(tick & 0xFFFFFFFFu),
                               0x5555u) % 1000u) < permille;
}

void residents_age(Resident r[], int count, PopData pop_data[],
                   uint32_t world_seed, uint64_t tick)
{
    int i;

    for (i = 0; i < count; i++) {
        if (!r[i].active) continue;

        r[i].age_months++;
        if (r[i].tenure_months < 0xFFFFFFFFu) r[i].tenure_months++;

        if (!resident_dies(&r[i], world_seed, tick)) continue;

        /* Widowed before the slot is cleared, for the reason given at
         * widow_partner: the slot is about to become reusable. */
        widow_partner(r, count, i);
        r[i].active = 0;
        /* The house is one smaller, and pop_update will see that this
         * same tick. Clamped rather than trusted: a resident whose
         * house was demolished under them must not drive a count
         * negative. */
        if (pop_data && pop_data[r[i].home_idx].residents > 0)
            pop_data[r[i].home_idx].residents--;
    }
}

/* ---- marriage (LIFE_PLAN Phase 6) -------------------------
 * See resident.h for why a house is the household and why this is a
 * monthly draw rather than an immediate pairing.
 */

static int marriageable(const Resident *r)
{
    return r->active && r->spouse < 0 && resident_stage(r) == LIFE_ADULT;
}

/* Two people who were born in the same house are brother and sister.
 *
 * THIS GUARD IS WHY birth_house EXISTS. Phase 6 paired within a house
 * on the reasoning that a house was six unrelated lodgers; Phase 6b
 * makes a house a family, and the very same rule would marry siblings
 * the month they both turned eighteen. Founders carry -1 and are
 * therefore never siblings of anyone, which is correct: they arrived
 * from somewhere else and from nobody here. */
static int siblings(const Resident *a, const Resident *b)
{
    return a->birth_house >= 0 && a->birth_house == b->birth_house;
}

void residents_marry(Resident r[], int count, uint32_t world_seed,
                     uint64_t tick)
{
    int i, j;

    for (i = 0; i < count; i++) {
        if (!marriageable(&r[i])) continue;

        for (j = i + 1; j < count; j++) {
            uint32_t draw;

            if (!marriageable(&r[j])) continue;
            if (r[j].home_idx != r[i].home_idx) continue;
            if (r[j].sex == r[i].sex) continue;      /* it takes two */
            if (siblings(&r[i], &r[j])) continue;

            /* Salted with BOTH ids and the tick. Both, so the question
             * is about this pair rather than about whoever happens to
             * be scanning; the tick, so a pair that is refused this
             * month is asked afresh next month instead of being
             * refused forever by one unlucky draw — the same reason
             * resident_dies is salted. */
            draw = resident_hash(world_seed,
                                 r[i].id ^ (r[j].id * 2654435761u),
                                 0x6666u ^ (uint32_t)(tick & 0xFFFFFFFFu));
            if (draw % 1000u >= MARRY_PERMILLE_PER_MONTH) continue;

            r[i].spouse = j;
            r[j].spouse = i;
            break;      /* i is spoken for; move to the next person */
        }
    }
}

/* ---- conception, gestation, birth (Phase 6b) --------------- */

static int fertile_age(const Resident *r)
{
    return r->age_months / MONTHS_PER_YEAR < AGE_FERTILE_MAX_YEARS
        && resident_stage(r) == LIFE_ADULT;
}

/* A woman who could start carrying this month: married, of an age, and
 * not already. Returns her index or -1. */
static int who_could_conceive(const Resident r[], int count, int home_idx)
{
    int i;

    for (i = 0; i < count; i++) {
        int sp = r[i].spouse;

        if (!r[i].active || r[i].home_idx != home_idx) continue;
        if (r[i].sex != SEX_FEMALE || r[i].pregnancy > 0) continue;
        if (!fertile_age(&r[i])) continue;

        if (sp < 0 || sp >= count || !r[sp].active) continue;
        if (r[sp].spouse != i) continue;          /* one-way: not a couple */
        if (!fertile_age(&r[sp])) continue;

        return i;
    }
    return -1;
}

void residents_breed(Resident r[], int *count, uint32_t *next_id,
                     PopData pop_data[], int building_count,
                     uint32_t world_seed, uint64_t tick)
{
    int i;

    /* Carry on carrying, and deliver those who are due. Done before
     * conception so a birth this month does not also conceive this
     * month off the back of the bed it just filled. */
    for (i = 0; i < *count; i++) {
        int slot;

        if (!r[i].active || r[i].pregnancy <= 0) continue;
        if (--r[i].pregnancy > 0) continue;

        /* The bed is checked again at delivery, not only at conception:
         * nine months is long enough for the house to have taken in a
         * sibling or for the mother to have been moved. A house with no
         * room keeps the child rather than losing it — the pregnancy
         * simply ends and she may conceive again. */
        if (r[i].home_idx < 0 || r[i].home_idx >= building_count) continue;
        if (pop_data[r[i].home_idx].residents >= HOUSE_CAPACITY) continue;

        slot = spawn_resident(r, count, next_id, r[i].home_idx,
                              world_seed, 1, -1);
        if (slot < 0) continue;                 /* island full */

        pop_data[r[i].home_idx].residents++;
        sim_log("House %d: a child is born, %d residents",
                r[i].home_idx, pop_data[r[i].home_idx].residents);
    }

    /* And who begins. One conception per house per month at most —
     * there is only ever one couple in a house that can, since siblings
     * do not marry and their parents are the founders. */
    for (i = 0; i < building_count; i++) {
        int her;

        if (!pop_data[i].active) continue;
        if (pop_data[i].residents >= HOUSE_CAPACITY) continue;

        her = who_could_conceive(r, *count, i);
        if (her < 0) continue;

        /* Salted with the tick, so a month that says no is one month
         * saying no rather than the answer forever. */
        if (resident_hash(world_seed,
                          r[her].id ^ (uint32_t)(tick & 0xFFFFFFFFu),
                          0x8888u) % 1000u >= CONCEIVE_PERMILLE_PER_MONTH)
            continue;

        r[her].pregnancy = PREGNANCY_MONTHS;
    }
}

int residents_fertile_couple_at(const Resident r[], int count, int home_idx)
{
    int i;

    for (i = 0; i < count; i++) {
        int sp = r[i].spouse;

        if (!r[i].active || r[i].home_idx != home_idx) continue;
        if (sp < 0 || sp >= count || !r[sp].active) continue;
        if (r[sp].spouse != i) continue;        /* one-way: not a couple */

        /* Both halves young enough. Checked on both because a couple
         * ages at two different rates in this model — they married as
         * adults, not as twins. */
        if (r[i].age_months  / MONTHS_PER_YEAR >= AGE_FERTILE_MAX_YEARS) continue;
        if (r[sp].age_months / MONTHS_PER_YEAR >= AGE_FERTILE_MAX_YEARS) continue;

        return 1;
    }
    return 0;
}

void residents_sync(Resident residents[], int *count, uint32_t *next_id,
                    const Building buildings[], const PopData pop_data[],
                    int building_count, uint32_t world_seed)
{
    int i;

    for (i = 0; i < building_count; i++) {
        int live, target, k;

        /* Any residential tier, not just BUILDING_HOUSE. agents_sync
         * once tested the concrete type here and silently stopped
         * managing anything upgraded; the predicate exists so that
         * mistake has one place to be fixed. */
        if (!buildings[i].active || !pop_is_house_type(buildings[i].type))
            continue;
        if (!pop_data[i].active) continue;

        live   = count_live_for_home(residents, *count, i);
        target = pop_data[i].residents;

        /* THE ONLY PEOPLE THIS FUNCTION STILL CREATES ARE FOUNDERS
         * (LIFE_PLAN Phase 6b). Growth inside an existing house is a
         * birth now and belongs to residents_breed; what is left here
         * is the moment a house first appears with pop_init's two
         * residents in it, and the reconciliation that removes people
         * when a starving house empties.
         *
         * The pair are spawned as one woman and one man and married to
         * each other on the spot. Leaving them to the ordinary monthly
         * draw would have worked, but it would have meant a new
         * household waiting a random handful of months before it could
         * begin — a delay with nothing to teach the player, since there
         * is nobody else in the house either of them could have
         * married instead. */
        for (k = live; k < target; k++) {
            int sex  = (k - live) % 2 == 0 ? SEX_FEMALE : SEX_MALE;
            int slot = spawn_resident(residents, count, next_id, i,
                                      world_seed, 0, sex);
            if (slot < 0) break;

            /* Marry this founder to the one spawned just before, if
             * that one is still single and is the other sex. */
            if (k > live) {
                int j, mate = -1;
                for (j = 0; j < *count; j++)
                    if (residents[j].active && j != slot
                        && residents[j].home_idx == i
                        && residents[j].spouse < 0
                        && residents[j].sex != residents[slot].sex
                        && residents[j].birth_house < 0) { mate = j; break; }
                if (mate >= 0) {
                    residents[slot].spouse = mate;
                    residents[mate].spouse = slot;
                }
            }
        }
        for (k = live; k > target; k--)
            despawn_one_for_home(residents, *count, i);
    }
}
