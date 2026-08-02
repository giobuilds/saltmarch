/* resident.c  --  a resident is a person  (LIFE_PLAN Phase 3) */

#include "resident.h"
#include "simlog.h"
#include <stdio.h>
#include <string.h>

/* ---- names ------------------------------------------------ */
/* SPLIT BY SEX SINCE PHASE 6b. Residents have a sex now because. */
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

/* FNV-1a over (world_seed, id, salt) with a murmur3 finaliser, the same */
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

/* Severs `idx`'s marriage from the OTHER side, so no live resident is
 * ever left pointing at a slot that has been cleared or reused. */
static void widow_partner(Resident r[], int count, int idx)
{
    int sp = r[idx].spouse;

    if (sp >= 0 && sp < count && r[sp].spouse == idx) r[sp].spouse = -1;
    r[idx].spouse = -1;
}

/* A FOUNDER arrives grown, aged 20-31 and spread. The spread is. */
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
    r[slot].spouse         = -1;
    r[slot].pregnancy      = 0;
    r[slot].children       = 0;
    r[slot].birth_cooldown = 0;
    r[slot].reserve_since  = 0;
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

/* Twelve or older and not yet retired. */
static int of_working_age(const Resident *r)
{
    int stage = resident_stage(r);
    return stage == LIFE_TEEN || stage == LIFE_ADULT;
}

int residents_adults_at(const Resident r[], int count, int home_idx)
{
    int i, n = 0;
    for (i = 0; i < count; i++)
        if (r[i].active && r[i].home_idx == home_idx
            && r[i].pregnancy == 0                 /* carrying: not free */
            && of_working_age(&r[i])) n++;
    return n;
}

void residents_tally(const Resident r[], int count, int building_count,
                     const int happiness[], int live[], int workers[],
                     int prod[])
{
    int i;
    int sum[MAX_BUILDINGS], n[MAX_BUILDINGS];

    for (i = 0; i < building_count; i++) {
        if (live)    live[i]    = 0;
        if (workers) workers[i] = 0;
        if (prod)    { prod[i] = PRODUCTIVITY_BASE; sum[i] = 0; n[i] = 0; }
    }

    for (i = 0; i < count; i++) {
        int h = r[i].home_idx;

        if (!r[i].active || h < 0 || h >= building_count) continue;
        if (live) live[h]++;
        if (r[i].pregnancy > 0 || !of_working_age(&r[i])) continue;
        if (workers) workers[h]++;
        if (prod) {
            sum[h] += resident_productivity(&r[i],
                          happiness ? happiness[h] : HAPPINESS_NEUTRAL);
            n[h]++;
        }
    }

    if (prod)
        for (i = 0; i < building_count; i++)
            if (n[i] > 0) prod[i] = sum[i] / n[i];
}

int residents_mouths_at(const Resident r[], int count, int home_idx)
{
    int i, adults = 0, others = 0;

    for (i = 0; i < count; i++) {
        if (!r[i].active || r[i].home_idx != home_idx) continue;
        /* A WHOLE RATION FOR ANYONE WHO WORKS (Phase 7b), which now */
        if (of_working_age(&r[i])) adults++;
        else                       others++;
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
        if (r[i].birth_cooldown > 0) r[i].birth_cooldown--;

        if (!resident_dies(&r[i], world_seed, tick)) continue;

        /* Widowed before the slot is cleared, for the reason given at
         * widow_partner: the slot is about to become reusable. */
        widow_partner(r, count, i);
        r[i].active = 0;
        /* The house is one smaller, and pop_update will see that this
         * same tick. Clamped rather than trusted: a resident whose
         * house was demolished under them must not drive a count
         * negative. */
        /* Guarded for the reserve: a homeless resident has no house
         * whose count could fall, and home_idx of -1 would index off
         * the front of the array (Phase 6c). */
        if (pop_data && r[i].home_idx >= 0
            && pop_data[r[i].home_idx].residents > 0)
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

/* Two people who were born in the same house are brother and sister. */
static int siblings(const Resident *a, const Resident *b)
{
    return a->birth_house >= 0 && a->birth_house == b->birth_house;
}

/* Could these two live together after marrying? */
/* ---- inheritance (LIFE_PLAN Phase 7b) ---------------------- */
/* ---- what a worker is worth (LIFE_PLAN Phase 8) ------------
 * See resident.h for the band, the four inputs and why the floor sits
 * where it does.
 */
int resident_productivity(const Resident *r, int happiness)
{
    int p = PRODUCTIVITY_BASE, stage;

    if (!r || !r->active) return PRODUCTIVITY_BASE;
    stage = resident_stage(r);

    /* 1. PRIME. A youth of twelve works, and works at the baseline. */
    if (stage == LIFE_ADULT) p += PROD_PRIME_BONUS;
    if (stage == LIFE_RETIRED) p -= PROD_TIRED_PENALTY;

    /* 2. MARRIED. Small, because it is the input a player has the least
     * control over. */
    if (r->spouse >= 0) p += PROD_MARRIED_BONUS;

    /* 3. FED, which is the household's business rather than the
     * person's, and the only one of the four a bad harvest can move.
     * Above neutral means the luxuries arrived too. */
    if (happiness > HAPPINESS_NEUTRAL)      p += PROD_FED_BONUS;
    else if (happiness < HAPPINESS_NEUTRAL) p -= PROD_HUNGRY_PENALTY;

    /* 4. TENURE, read here for the first time since Phase 3 tracked it.
     * Ramps linearly to PROD_TENURE_MAX over PROD_TENURE_FULL months,
     * which makes a settled crew worth keeping and gives demolishing a
     * workplace a cost that is not just the gold. */
    {
        uint32_t t = r->tenure_months;
        if (t > (uint32_t)PROD_TENURE_FULL) t = (uint32_t)PROD_TENURE_FULL;
        p += (int)t * PROD_TENURE_MAX / PROD_TENURE_FULL;
    }

    /* A pregnancy is not counted here: she holds no job at all while
     * carrying (residents_adults_at), so there is no output of hers to
     * scale. */

    if (p < PRODUCTIVITY_MIN) p = PRODUCTIVITY_MIN;
    if (p > PRODUCTIVITY_MAX) p = PRODUCTIVITY_MAX;
    return p;
}

int residents_house_productivity(const Resident r[], int count,
                                 int home_idx, int happiness)
{
    int i, n = 0, sum = 0;

    for (i = 0; i < count; i++) {
        if (!r[i].active || r[i].home_idx != home_idx) continue;
        if (r[i].pregnancy > 0) continue;          /* holds no job */
        if (!of_working_age(&r[i])) continue;
        sum += resident_productivity(&r[i], happiness);
        n++;
    }
    return n ? sum / n : PRODUCTIVITY_BASE;
}

int residents_heir_of(const Resident r[], int count, int home)
{
    int i, heir = -1;

    if (home < 0) return -1;

    for (i = 0; i < count; i++) {
        if (!r[i].active || r[i].home_idx != home) continue;
        if (r[i].birth_house != home) return -1;      /* an elder lives */
    }
    for (i = 0; i < count; i++) {
        if (!r[i].active || r[i].home_idx != home) continue;
        if (resident_stage(&r[i]) != LIFE_ADULT) continue;
        if (heir < 0 || r[i].age_months > r[heir].age_months) heir = i;
    }
    return heir;
}

static int where_they_would_live(const Resident r[], int count, int a, int b,
                                 const PopData pop_data[], int building_count)
{
    int ha = r[a].home_idx, hb = r[b].home_idx;

    if (ha == RESIDENT_HOMELESS && hb == RESIDENT_HOMELESS)
        return RESIDENT_HOMELESS;
    if (ha == hb) return ha;                      /* already housemates */

    /* THE HEIR KEEPS THE HOUSE, AND CAPACITY DOES NOT APPLY TO THEIR */
    {
        int ah = residents_heir_of(r, count, ha);
        int bh = residents_heir_of(r, count, hb);

        if (ah == a && bh == b) return ha < hb ? ha : hb;
        if (ah == a) return ha;
        if (bh == b) return hb;
    }

    /* Otherwise: one of them has a roof with room in it. Prefer the
     * house that has room; if both do, the lower index, so the choice
     * does not depend on which of the pair the scan reached first. */
    if (hb >= 0 && hb < building_count && pop_data[hb].active
        && pop_data[hb].residents < HOUSE_CAPACITY
        && (ha == RESIDENT_HOMELESS || hb < ha))
        return hb;
    if (ha >= 0 && ha < building_count && pop_data[ha].active
        && pop_data[ha].residents < HOUSE_CAPACITY)
        return ha;
    if (hb >= 0 && hb < building_count && pop_data[hb].active
        && pop_data[hb].residents < HOUSE_CAPACITY)
        return hb;
    return RESIDENT_HOMELESS;
}

/* Moves `who` into `home`, keeping both houses' counts straight. */
static void move_house(Resident r[], int who, int home, PopData pop_data[],
                       int building_count)
{
    int from = r[who].home_idx;

    if (from == home) return;
    if (from >= 0 && from < building_count && pop_data[from].residents > 0)
        pop_data[from].residents--;
    r[who].home_idx = home;
    if (home >= 0 && home < building_count)
        pop_data[home].residents++;
}

void residents_marry(Resident r[], int count, PopData pop_data[],
                     int building_count, uint32_t world_seed, uint64_t tick)
{
    int i, j, pass;

    /* TWO PASSES, AND THE ORDER IS THE "PRIORITISE SOMEONE FROM ANOTHER */
    for (pass = 0; pass < 2; pass++)
    for (i = 0; i < count; i++) {
        if (!marriageable(&r[i])) continue;

        for (j = i + 1; j < count; j++) {
            uint32_t draw;
            int      home;

            if (!marriageable(&r[j])) continue;
            if (r[j].sex == r[i].sex) continue;      /* it takes two */
            if (siblings(&r[i], &r[j])) continue;
            if (pass == 0 && r[j].home_idx == r[i].home_idx) continue;

            home = where_they_would_live(r, count, i, j, pop_data,
                                         building_count);

            /* Salted with BOTH ids and the tick. Both, so the question */
            draw = resident_hash(world_seed,
                                 r[i].id ^ (r[j].id * 2654435761u),
                                 0x6666u ^ (uint32_t)(tick & 0xFFFFFFFFu));
            if (draw % 1000u >= MARRY_PERMILLE_PER_MONTH) continue;

            /* Whoever is not already living there moves. A marriage. */
            move_house(r, i, home, pop_data, building_count);
            move_house(r, j, home, pop_data, building_count);
            if (home == RESIDENT_HOMELESS) {
                r[i].reserve_since = (int32_t)tick;
                r[j].reserve_since = (int32_t)tick;
            }

            r[i].spouse = j;
            r[j].spouse = i;
            break;      /* i is spoken for; move to the next person */
        }
    }
}

int residents_found_pair(Resident r[], int *count, uint32_t *next_id,
                         int home_idx, uint32_t world_seed)
{
    int her, him;

    her = spawn_resident(r, count, next_id, home_idx, world_seed, 0,
                         SEX_FEMALE);
    if (her < 0) return 0;
    him = spawn_resident(r, count, next_id, home_idx, world_seed, 0,
                         SEX_MALE);
    if (him < 0) { r[her].active = 0; return 0; }

    r[her].spouse = him;
    r[him].spouse = her;
    return 2;
}

/* ---- the reserve (Phase 6c) -------------------------------- */

int residents_reserve_count(const Resident r[], int count)
{
    int i, n = 0;
    for (i = 0; i < count; i++)
        if (r[i].active && r[i].home_idx == RESIDENT_HOMELESS) n++;
    return n;
}

int residents_reserve_ration(const Resident r[], int count)
{
    /* Half a ration each, rounded up: a reserve of one still eats. */
    return (residents_reserve_count(r, count) + 1) / 2;
}

/* The longest-waiting person in the reserve, or -1. Ordered. */
static int longest_waiting(const Resident r[], int count, int sex, int not_kin_of)
{
    int i, best = -1;

    for (i = 0; i < count; i++) {
        if (!r[i].active || r[i].home_idx != RESIDENT_HOMELESS) continue;
        if (sex >= 0 && r[i].sex != sex) continue;
        if (not_kin_of >= 0 && siblings(&r[i], &r[not_kin_of])) continue;
        if (best < 0 || r[i].reserve_since < r[best].reserve_since) best = i;
    }
    return best;
}

/* Is there already a lone unmarried adult under this roof? If so this
 * house does not need a first occupant, it needs a spouse for the one
 * it has. */
static int lone_occupant(const Resident r[], int count, int home_idx)
{
    int i, who = -1, n = 0;

    for (i = 0; i < count; i++) {
        if (!r[i].active || r[i].home_idx != home_idx) continue;
        n++;
        if (r[i].spouse < 0 && resident_stage(&r[i]) == LIFE_ADULT) who = i;
    }
    return n == 1 ? who : -1;
}

int residents_settle_house(Resident r[], int count, int home_idx)
{
    int first, mate, housed = 0;

    /* A house holding one unmarried adult wants a spouse, not a
     * stranger. Everything else about this is the same question. */
    first = lone_occupant(r, count, home_idx);
    if (first >= 0) {
        mate = longest_waiting(r, count,
                               r[first].sex == SEX_FEMALE ? SEX_MALE
                                                          : SEX_FEMALE,
                               first);
        if (mate < 0) return 0;
        r[mate].home_idx      = home_idx;
        r[mate].reserve_since = 0;
        r[first].spouse = mate;
        r[mate].spouse  = first;
        return 1;
    }

    /* An empty house. First come, first housed. */
    first = longest_waiting(r, count, -1, -1);
    if (first < 0) return 0;

    r[first].home_idx      = home_idx;
    r[first].reserve_since = 0;
    housed = 1;

    /* And a spouse if one is waiting. If not, they keep the roof alone
     * and this function is called again for them next month — a lone
     * occupant is a worker and a claim on a house, just not yet a
     * household. */
    mate = longest_waiting(r, count,
                           r[first].sex == SEX_FEMALE ? SEX_MALE : SEX_FEMALE,
                           first);
    if (mate >= 0) {
        r[mate].home_idx      = home_idx;
        r[mate].reserve_since = 0;
        /* Married on moving in unless they already are to each other,
         * which is the common case: they entered the reserve as a
         * couple who could not be housed. */
        if (r[first].spouse != mate) {
            r[first].spouse = mate;
            r[mate].spouse  = first;
        }
        housed = 2;
    }
    return housed;
}

int residents_emigrate(Resident r[], int count, uint64_t tick,
                       int (*relocate)(void *ctx, int idx), void *ctx)
{
    int i, left = 0;

    for (i = 0; i < count; i++) {
        int64_t waited;

        if (!r[i].active || r[i].home_idx != RESIDENT_HOMELESS) continue;

        waited = (int64_t)tick - (int64_t)r[i].reserve_since;
        if (waited < (int64_t)RESERVE_TOLERANCE_MONTHS
                   * (int64_t)CALENDAR_MONTH_TICKS)
            continue;

        /* Either way they leave THIS island, so the slot is vacated */
        if (relocate) relocate(ctx, i);

        widow_partner(r, count, i);
        r[i].active = 0;
        left++;
    }
    return left;
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
        if (r[i].birth_cooldown > 0) continue;   /* still recovering */
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

        /* A CHILD IS NEVER HOMELESS (Phase 7). It is born where. */
        {
            int home = r[i].home_idx;
            int at_home = home >= 0 && home < building_count
                       && pop_data[home].active;

            slot = spawn_resident(r, count, next_id,
                                  at_home ? home : RESIDENT_HOMELESS,
                                  world_seed, 1, -1);
            if (slot < 0) continue;             /* island full */

            /* spawn_resident derives birth_house from the home it was
             * given, which is -1 for a child born to a homeless mother.
             * Set explicitly so such a child is still its siblings'
             * sibling. */
            r[slot].birth_house   = at_home ? home : r[i].birth_house;
            r[slot].reserve_since = at_home ? 0 : (int32_t)tick;
            r[i].children++;
            r[i].birth_cooldown = BIRTH_COOLDOWN_MONTHS;

            if (at_home) {
                pop_data[home].residents++;
                sim_log("House %d: a child is born, %d residents",
                        home, pop_data[home].residents);
            } else {
                sim_log("A child is born to somebody with no roof");
            }
        }
    }

    /* And who begins. One conception per house per month at most —
     * there is only ever one couple in a house that can, since siblings
     * do not marry and their parents are the founders. */
    for (i = 0; i < building_count; i++) {
        int her;

        if (!pop_data[i].active) continue;
        /* A FULL HOUSE STILL CONCEIVES (Phase 6c). This is the gate. */
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
                    int building_count, uint32_t world_seed,
                    const int live[])
{
    int i;

    const int *live_counts = live;

    for (i = 0; i < building_count; i++) {
        int have, target, k;

        /* Any residential tier, not just BUILDING_HOUSE. agents_sync
         * once tested the concrete type here and silently stopped
         * managing anything upgraded; the predicate exists so that
         * mistake has one place to be fixed. */
        if (!buildings[i].active || !pop_is_house_type(buildings[i].type))
            continue;
        if (!pop_data[i].active) continue;

        have   = live_counts[i];
        target = pop_data[i].residents;

        /* THE ONLY PEOPLE THIS FUNCTION STILL CREATES ARE FOUNDERS */
        /* NOTHING IS SPAWNED HERE ANY MORE (Phase 6c). A household. */
        (void)next_id; (void)world_seed;
        for (k = have; k > target; k--)
            despawn_one_for_home(residents, *count, i);
    }
}
