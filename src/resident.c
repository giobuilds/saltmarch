/*  resident.c  --  a resident is a person  (LIFE_PLAN Phase 3)
 *
 *  See resident.h for why identity is split from motion and why names
 *  are derived rather than stored.
 */

#include "resident.h"
#include <stdio.h>
#include <string.h>

/* ---- names ------------------------------------------------
 * Marsh-flavoured and deliberately plain: these are fisherfolk and
 * farmhands, not heroes. 48 x 40 is 1920 combinations, which is more
 * than an island can hold, so a player rarely meets the same name twice
 * — and the collisions that do happen read as a family rather than as a
 * bug, which is the right failure. */
static const char *const FIRST_NAMES[] = {
    "Bess",  "Cully", "Maud",  "Tam",   "Ivy",   "Hob",   "Neve",  "Wick",
    "Sela",  "Bram",  "Nan",   "Orrin", "Tilda", "Fen",   "Marta", "Gil",
    "Peg",   "Colm",  "Ada",   "Rusk",  "Elsie", "Dorn",  "Hettie","Sarn",
    "Winna", "Kell",  "Bryde", "Aldo",  "Merrit","Salla", "Torr",  "Grea",
    "Odd",   "Lisbet","Harl",  "Nell",  "Pike",  "Cass",  "Rowan", "Juna",
    "Silas", "Wren",  "Ansel", "Dove",  "Mabb",  "Teague","Iris",  "Corwin"
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
#define FIRST_COUNT (int)(sizeof(FIRST_NAMES) / sizeof(FIRST_NAMES[0]))
#define SUR_COUNT   (int)(sizeof(SURNAMES)    / sizeof(SURNAMES[0]))

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

    snprintf(out, out_len, "%s %s", FIRST_NAMES[f], SURNAMES[s]);
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

/* Arriving adults, aged 20-45 and spread — see the invariant in
 * resident.h. The spread is what stops an island dying in cohorts. */
static void spawn_resident(Resident r[], int *count, uint32_t *next_id,
                           int home_idx, uint32_t world_seed)
{
    int      slot = -1, i;
    uint32_t id, years, months;

    for (i = 0; i < *count; i++)
        if (!r[i].active) { slot = i; break; }
    if (slot < 0) {
        if (*count >= MAX_RESIDENTS) return;   /* soft cap, like agents */
        slot = (*count)++;
    }

    id     = (*next_id)++;
    years  = 20u + resident_hash(world_seed, id, 0x3333u) % 26u;
    months = resident_hash(world_seed, id, 0x4444u) % (uint32_t)MONTHS_PER_YEAR;

    memset(&r[slot], 0, sizeof(r[slot]));
    r[slot].active     = 1;
    r[slot].home_idx   = home_idx;
    r[slot].id         = id;
    r[slot].age_months = (int32_t)(years * MONTHS_PER_YEAR + months);
    r[slot].spouse     = -1;
}

/* Removes the LAST matching resident, which is deterministic and is all
 * that is required while nothing distinguishes one from another. Phase 5
 * replaces this: once people age, who leaves stops being arbitrary. */
static void despawn_one_for_home(Resident r[], int count, int home_idx)
{
    int i;
    for (i = count - 1; i >= 0; i--)
        if (r[i].active && r[i].home_idx == home_idx) {
            r[i].active = 0;
            return;
        }
}

int residents_adults_at(const Resident r[], int count, int home_idx)
{
    int i, n = 0;
    for (i = 0; i < count; i++)
        if (r[i].active && r[i].home_idx == home_idx
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

        r[i].active = 0;
        /* The house is one smaller, and pop_update will see that this
         * same tick. Clamped rather than trusted: a resident whose
         * house was demolished under them must not drive a count
         * negative. */
        if (pop_data && pop_data[r[i].home_idx].residents > 0)
            pop_data[r[i].home_idx].residents--;
    }
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

        for (k = live; k < target; k++)
            spawn_resident(residents, count, next_id, i, world_seed);
        for (k = live; k > target; k--)
            despawn_one_for_home(residents, *count, i);
    }
}
