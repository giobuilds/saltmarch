/*  building.c  --  Building definitions and placement logic
 *
 *  PLACEMENT VALIDATION STRATEGY
 *  ==============================
 *  building_can_place() works in three passes:
 *
 *  Pass 1 – Bounds check
 *    The entire footprint (tile_w × tile_h) must lie within
 *    the map.  A 2×2 building at (row=63, col=63) would hang
 *    off the edge — reject it.
 *
 *  Pass 2 – Per-tile checks
 *    Every tile in the footprint must be:
 *      a) buildable  (not water, not forest)
 *      b) not already occupied by another building
 *    If PLACE_NEEDS_FERTILE is set, every tile must also
 *    carry the right fertility flag.
 *
 *  Pass 3 – Adjacency checks (only if flags require it)
 *    PLACE_NEEDS_COAST  → at least one of the 4-connected
 *                         neighbours of ANY footprint tile
 *                         must be TILE_WATER.
 *    PLACE_NEEDS_FOREST → same but for TILE_FOREST.
 *    We scan all tiles in the footprint and check their
 *    four cardinal neighbours (N, S, E, W).
 * 
 * Building definition table  (Phase 4: production fields)
 *
 * Production design:
 *   Fisher's Hut  – produces FISH from nothing (the sea is free)
 *   Warehouse     – no production; it is a storage building
 *   Farm          – produces GRAIN from nothing (sun and soil)
 *   Lumberjack    – produces WOOD from nothing (the forest is free)
 *
 * In Phase 5 we will add consumption chains:
 *   e.g. Fisher's Hut will consume WOOD for boat fuel,
 *   Farm will consume tools, etc.
 * RES_COUNT is used as a sentinel meaning "no resource".
 *
 * tick_seconds controls how fast each building works:
 *   slower tick = rarer, more valuable output
 */

#include "building.h"
#include <stdio.h>    /* snprintf */
#include <stddef.h>   /* NULL     */

/* =========================================================
 * Building definition table
 * ========================================================= */
/* Every row is DESIGNATED by its enum value. This table was positional
 * until the Shipyard / Worker's House rows were found swapped relative
 * to the enum (BUILDING_DEFS[10] held the Shipyard def while type 10 is
 * BUILDING_HOUSE_WORKER) — the same silent-misalignment failure the
 * RES_COL table had. Designated rows make the compiler place each def
 * at its enum index no matter the order rows appear in, and
 * tests/test_defs.c asserts name<->enum agreement so a future row can't
 * regress this. */
/* Fields inside each row are designated too (UI_PLAN Phase 2), not just
 * the row indices. The rows were positional until this phase needed to
 * add `category`: appending a field to BuildingDef would have been safe,
 * but inserting one anywhere else would have silently shifted every
 * value after it in all thirteen rows — the same class of failure as the
 * swapped Shipyard row, one edit away. Naming the fields makes that
 * impossible and costs nothing at runtime. */
const BuildingDef BUILDING_DEFS[BUILDING_TYPE_COUNT] = {
    [BUILDING_FISHERS_HUT] = {
        .name = "Fisher's Hut",
        .category = BCAT_FARMING,
        .tile_w = 1, .tile_h = 1,
        .placement_flags = PLACE_NEEDS_COAST,
        .col_r = 210, .col_g = 180, .col_b = 100,
        .produces = RES_FISH, .produce_amt = 1,
        .consumes = { RES_COUNT, RES_COUNT, RES_COUNT }, .consume_amt = { 0, 0, 0 },
        .tick_seconds = 6.0f,
        .cost = { [RES_GOLD] = 60 },
        .hud_placeable = 1
    },
    [BUILDING_WAREHOUSE] = {
        .name = "Warehouse",
        .category = BCAT_INFRASTRUCTURE,
        .tile_w = 2, .tile_h = 2,
        .placement_flags = PLACE_ANY_LAND,
        .col_r = 160, .col_g = 100, .col_b = 60,
        .produces = RES_COUNT, .produce_amt = 0,
        .consumes = { RES_COUNT, RES_COUNT, RES_COUNT }, .consume_amt = { 0, 0, 0 },
        .tick_seconds = 0.0f,
        .cost = { [RES_WOOD] = 20, [RES_GOLD] = 150 },
        .hud_placeable = 1
    },
    [BUILDING_FARM] = {
        .name = "Farm",
        .category = BCAT_FARMING,
        .tile_w = 2, .tile_h = 2,
        .placement_flags = PLACE_NEEDS_FERTILE,
        .col_r = 80, .col_g = 160, .col_b = 50,
        .produces = RES_GRAIN, .produce_amt = 1,
        .consumes = { RES_COUNT, RES_COUNT, RES_COUNT }, .consume_amt = { 0, 0, 0 },
        .tick_seconds = 8.0f,
        .cost = { [RES_GOLD] = 80 },
        .hud_placeable = 1
    },
    [BUILDING_LUMBERJACK] = {
        .name = "Lumberjack",
        .category = BCAT_EXTRACTION,
        .tile_w = 1, .tile_h = 1,
        .placement_flags = PLACE_NEEDS_FOREST,
        .col_r = 120, .col_g = 80, .col_b = 40,
        .produces = RES_WOOD, .produce_amt = 1,
        .consumes = { RES_COUNT, RES_COUNT, RES_COUNT }, .consume_amt = { 0, 0, 0 },
        .tick_seconds = 5.0f,
        .cost = { [RES_GOLD] = 60 },
        .hud_placeable = 1
    },
    /* Phase 5: residents live here and generate gold when fed.
     * Renamed to Marsh Cottage in SUPPLY_CHAIN Phase 3: it is now the
     * base of ONE of three house lines rather than the bottom of a
     * single ladder, and "House" no longer says which. */
    [BUILDING_HOUSE] = {
        .name = "Marsh Cottage",
        .category = BCAT_HOUSING,
        .tile_w = 1, .tile_h = 1,
        .placement_flags = PLACE_ANY_LAND,
        .col_r = 210, .col_g = 190, .col_b = 160,
        .produces = RES_COUNT, .produce_amt = 0,
        .consumes = { RES_COUNT, RES_COUNT, RES_COUNT }, .consume_amt = { 0, 0, 0 },
        .tick_seconds = 0.0f,
        .cost = { [RES_WOOD] = 15, [RES_GOLD] = 80 },
        .hud_placeable = 1
    },
    /* Phase 2: Road — no production; PLACE_ANY_LAND is sufficient
     * to keep it off water/forest, since building_can_place already
     * requires tile->buildable, which those tile types never have.
     * Free: a real road network needs many tiles, and charging per
     * tile made drag-placing one needlessly punishing. */
    [BUILDING_ROAD] = {
        .name = "Road",
        .category = BCAT_INFRASTRUCTURE,
        .tile_w = 1, .tile_h = 1,
        .placement_flags = PLACE_ANY_LAND,
        .col_r = 110, .col_g = 105, .col_b = 100,
        .produces = RES_COUNT, .produce_amt = 0,
        .consumes = { RES_COUNT, RES_COUNT, RES_COUNT }, .consume_amt = { 0, 0, 0 },
        .tick_seconds = 0.0f,
        .cost = { 0 },
        .hud_placeable = 1
    },
    /* Phase 4: Marketplace — no passive production; it's a pure
     * gateway building. Clicking a placed, road-connected one opens
     * the manual trade screen (see trade_ui.c, game_sell_resource). */
    [BUILDING_MARKETPLACE] = {
        .name = "Marketplace",
        .category = BCAT_INFRASTRUCTURE,
        .tile_w = 2, .tile_h = 2,
        .placement_flags = PLACE_ANY_LAND,
        .col_r = 200, .col_g = 140, .col_b = 60,
        .produces = RES_COUNT, .produce_amt = 0,
        .consumes = { RES_COUNT, RES_COUNT, RES_COUNT }, .consume_amt = { 0, 0, 0 },
        .tick_seconds = 0.0f,
        .cost = { [RES_WOOD] = 30, [RES_GOLD] = 200 },
        .hud_placeable = 1
    },
    /* Production chains, Phase 1 (Beer). Hop Farm names FERTILE_HOP in
     * needs_fertility (map.h) — it was the first building to want a
     * specific crop and, until SUPPLY_CHAIN Phase 1, the reason there
     * was a whole placement flag for that one crop. Malthouse is the
     * multi-input building: both Grain and Hops must be in stock for it
     * to tick at all (all-or-nothing, see game_tick_buildings, game.c). */
    [BUILDING_HOP_FARM] = {
        .name = "Hop Farm",
        .category = BCAT_FARMING,
        .tile_w = 1, .tile_h = 1,
        .placement_flags = PLACE_ANY_LAND,
        .needs_fertility = FERTILE_HOP,
        .col_r = 90, .col_g = 150, .col_b = 60,
        .produces = RES_HOPS, .produce_amt = 1,
        .consumes = { RES_COUNT, RES_COUNT, RES_COUNT }, .consume_amt = { 0, 0, 0 },
        .tick_seconds = 8.0f,
        .cost = { [RES_GOLD] = 80 },
        .hud_placeable = 1
    },
    [BUILDING_MALTHOUSE] = {
        .name = "Malthouse",
        .category = BCAT_WORKSHOP,
        .tile_w = 2, .tile_h = 2,
        .placement_flags = PLACE_ANY_LAND,
        .col_r = 170, .col_g = 140, .col_b = 90,
        .produces = RES_MALT, .produce_amt = 1,
        .consumes = { RES_GRAIN, RES_HOPS, RES_COUNT }, .consume_amt = { 1, 1, 0 },
        .tick_seconds = 10.0f,
        .cost = { [RES_WOOD] = 20, [RES_GOLD] = 150 },
        .hud_placeable = 1
    },
    [BUILDING_BREWERY] = {
        .name = "Brewery",
        .category = BCAT_WORKSHOP,
        .tile_w = 2, .tile_h = 2,
        .placement_flags = PLACE_ANY_LAND,
        .col_r = 190, .col_g = 150, .col_b = 70,
        .produces = RES_BEER, .produce_amt = 1,
        .consumes = { RES_MALT, RES_COUNT, RES_COUNT },
        .consume_amt = { 1, 0, 0 },
        .tick_seconds = 8.0f,
        .cost = { [RES_WOOD] = 20, [RES_GOLD] = 150 },
        .hud_placeable = 1
    },
    /* Colonisation: a Shipyard has no production of its own — like the
     * Marketplace it is a gateway you click, here to lay down a ship.
     * PLACE_NEEDS_COAST for the obvious reason. */
    [BUILDING_SHIPYARD] = {
        .name = "Shipyard",
        /* Sails since SUPPLY_CHAIN Phase 7, which is deliberately a
         * soft gate rather than a hard one. Sails need Cloth, Cloth
         * needs Cotton, and cotton grows only in the south -- which
         * you need a ship to reach. The faction stocks every good, so
         * a first Shipyard buys its canvas and every one after it can
         * be fitted from a Sail Loft of your own. That is the
         * gold-only escape hatch BUY_PRICE's comment describes, doing
         * the most load-bearing job it has ever had: keep it stocked,
         * or this becomes a deadlock rather than an expense. */
        .category = BCAT_MARITIME,
        .tile_w = 2, .tile_h = 2,
        .placement_flags = PLACE_NEEDS_COAST,
        .col_r = 130, .col_g = 120, .col_b = 160,
        .produces = RES_COUNT, .produce_amt = 0,
        .consumes = { RES_COUNT, RES_COUNT, RES_COUNT }, .consume_amt = { 0, 0, 0 },
        .tick_seconds = 0.0f,
        .cost = { [RES_PLANKS] = 15, [RES_SAILS] = 4, [RES_GOLD] = 250 },
        .hud_placeable = 1
    },
    /* Was Worker's House, reachable only by upgrading a Marsh Cottage.
     * SUPPLY_CHAIN Phase 3 makes it the base of the SECOND house line
     * and therefore something you build: hud_placeable is 1 and cost[]
     * is now load-bearing where it used to be ignored.
     *
     * The visible cost of the three-line model is that the old
     * Cottage -> Worker's House ladder is gone. Both are base tiers
     * now; their upgrade targets are Artisans (Phase 4) and Engineers
     * (Phase 6). Dearer than a cottage because Wrights want four goods
     * a cottage does not. */
    [BUILDING_HOUSE_WORKER] = {
        .name = "Wright's House",
        .category = BCAT_HOUSING,
        .tile_w = 1, .tile_h = 1,
        .placement_flags = PLACE_ANY_LAND,
        .col_r = 230, .col_g = 200, .col_b = 140,
        .produces = RES_COUNT, .produce_amt = 0,
        .consumes = { RES_COUNT, RES_COUNT, RES_COUNT }, .consume_amt = { 0, 0, 0 },
        .tick_seconds = 0.0f,
        .cost = { [RES_PLANKS] = 10, [RES_BRICKS] = 5, [RES_GOLD] = 180 },
        .hud_placeable = 1
    },
    /* MMO Phase 5: the inter-player airlock (see building.h). No
     * production — like Marketplace/Shipyard it is a gateway you click,
     * here to open the escrow panel. */
    [BUILDING_HARBOR] = {
        .name = "Harbor",
        .category = BCAT_MARITIME,
        .tile_w = 2, .tile_h = 2,
        .placement_flags = PLACE_NEEDS_COAST,
        .col_r = 90, .col_g = 130, .col_b = 170,
        .produces = RES_COUNT, .produce_amt = 0,
        .consumes = { RES_COUNT, RES_COUNT, RES_COUNT }, .consume_amt = { 0, 0, 0 },
        .tick_seconds = 0.0f,
        .cost = { [RES_WOOD] = 30, [RES_GOLD] = 200 },
        .hud_placeable = 1
    },

    /* ================================================================
     * SUPPLY_CHAIN Phase 3 — the northern base economy
     *
     * Seven chains. Three start at terrain Phase 1 added and could not
     * have been written before it: the Potato Field names a crop, the
     * Clay Pit names a deposit, and the pastures name grazing.
     *
     * Costs scale with depth: a field is cheap, the workshop that
     * consumes it is not. Tick rates run slower the further along a
     * chain a building sits, so a single upstream producer feeds
     * roughly one downstream consumer without the player having to
     * count ratios on paper.
     * ================================================================ */

    [BUILDING_SAWMILL] = {
        .name = "Sawmill",
        .category = BCAT_REFINERY,
        .tile_w = 2, .tile_h = 2,
        .placement_flags = PLACE_ANY_LAND,
        .col_r = 150, .col_g = 115, .col_b = 70,
        .produces = RES_PLANKS, .produce_amt = 1,
        .consumes = { RES_WOOD, RES_COUNT, RES_COUNT },
        .consume_amt = { 1, 0, 0 },
        .tick_seconds = 7.0f,
        .cost = { [RES_WOOD] = 20, [RES_GOLD] = 120 },
        .hud_placeable = 1
    },
    /* Grazing is the one crop bit that is not exclusive with the
     * others (map.h), so a pasture and a farm compete for the same
     * ground — which is the decision it exists to create. */
    [BUILDING_SHEEP_PASTURE] = {
        .name = "Sheep Pasture",
        .category = BCAT_FARMING,
        .tile_w = 2, .tile_h = 2,
        .placement_flags = PLACE_ANY_LAND,
        .needs_fertility = FERTILE_PASTURE,
        .col_r = 190, .col_g = 195, .col_b = 175,
        .produces = RES_WOOL, .produce_amt = 1,
        .consumes = { RES_COUNT, RES_COUNT, RES_COUNT },
        .consume_amt = { 0, 0, 0 },
        .tick_seconds = 9.0f,
        .cost = { [RES_GOLD] = 90 },
        .hud_placeable = 1
    },
    [BUILDING_KNITTING_HOUSE] = {
        .name = "Knitting House",
        .category = BCAT_WORKSHOP,
        .tile_w = 2, .tile_h = 2,
        .placement_flags = PLACE_ANY_LAND,
        .col_r = 165, .col_g = 150, .col_b = 125,
        .produces = RES_OILSKINS, .produce_amt = 1,
        .consumes = { RES_WOOL, RES_COUNT, RES_COUNT },
        .consume_amt = { 1, 0, 0 },
        .tick_seconds = 11.0f,
        .cost = { [RES_WOOD] = 15, [RES_GOLD] = 140 },
        .hud_placeable = 1
    },
    [BUILDING_POTATO_FIELD] = {
        .name = "Potato Field",
        .category = BCAT_FARMING,
        .tile_w = 2, .tile_h = 2,
        .placement_flags = PLACE_ANY_LAND,
        .needs_fertility = FERTILE_POTATO,
        .col_r = 130, .col_g = 145, .col_b = 75,
        .produces = RES_POTATOES, .produce_amt = 1,
        .consumes = { RES_COUNT, RES_COUNT, RES_COUNT },
        .consume_amt = { 0, 0, 0 },
        .tick_seconds = 8.0f,
        .cost = { [RES_GOLD] = 80 },
        .hud_placeable = 1
    },
    [BUILDING_STILL] = {
        .name = "Still",
        .category = BCAT_WORKSHOP,
        .tile_w = 1, .tile_h = 1,
        .placement_flags = PLACE_ANY_LAND,
        .col_r = 170, .col_g = 160, .col_b = 190,
        .produces = RES_MARSH_GIN, .produce_amt = 1,
        .consumes = { RES_POTATOES, RES_COUNT, RES_COUNT },
        .consume_amt = { 1, 0, 0 },
        .tick_seconds = 10.0f,
        .cost = { [RES_WOOD] = 10, [RES_GOLD] = 130 },
        .hud_placeable = 1
    },
    /* The first building that names a deposit. Its footprint must sit
     * entirely on clay, which is what makes a seam a place rather than
     * a number. */
    [BUILDING_CLAY_PIT] = {
        .name = "Clay Pit",
        .category = BCAT_EXTRACTION,
        .tile_w = 1, .tile_h = 1,
        .placement_flags = PLACE_ANY_LAND,
        .needs_deposit = DEPOSIT_CLAY,
        .col_r = 190, .col_g = 110, .col_b = 70,
        .produces = RES_CLAY, .produce_amt = 1,
        .consumes = { RES_COUNT, RES_COUNT, RES_COUNT },
        .consume_amt = { 0, 0, 0 },
        .tick_seconds = 7.0f,
        .cost = { [RES_GOLD] = 90 },
        .hud_placeable = 1
    },
    [BUILDING_BRICKWORKS] = {
        .name = "Brickworks",
        /* A furnace, not a bench: BCAT_FACTORY's own definition is
         * "heavy industry: furnaces, machine shops", which this fits
         * better than "one artisan's worth of processing". Moved in
         * SUPPLY_CHAIN Phase 6, when Workshops outgrew the build bar
         * by exactly one slot -- but moved because it is more nearly
         * true, not only because it fit. */
        .category = BCAT_FACTORY,
        .tile_w = 2, .tile_h = 2,
        .placement_flags = PLACE_ANY_LAND,
        .col_r = 175, .col_g = 90, .col_b = 60,
        .produces = RES_BRICKS, .produce_amt = 1,
        .consumes = { RES_CLAY, RES_COUNT, RES_COUNT },
        .consume_amt = { 1, 0, 0 },
        .tick_seconds = 9.0f,
        .cost = { [RES_WOOD] = 15, [RES_GOLD] = 130 },
        .hud_placeable = 1
    },
    /* One Pig Pen feeds two different workshops — the first place the
     * player has to choose what a raw good becomes. */
    [BUILDING_PIG_PEN] = {
        .name = "Pig Pen",
        .category = BCAT_FARMING,
        .tile_w = 2, .tile_h = 2,
        .placement_flags = PLACE_ANY_LAND,
        .needs_fertility = FERTILE_PASTURE,
        .col_r = 200, .col_g = 160, .col_b = 155,
        .produces = RES_PIGS, .produce_amt = 1,
        .consumes = { RES_COUNT, RES_COUNT, RES_COUNT },
        .consume_amt = { 0, 0, 0 },
        .tick_seconds = 10.0f,
        .cost = { [RES_GOLD] = 100 },
        .hud_placeable = 1
    },
    [BUILDING_BUTCHERY] = {
        .name = "Butchery",
        .category = BCAT_WORKSHOP,
        .tile_w = 2, .tile_h = 2,
        .placement_flags = PLACE_ANY_LAND,
        .col_r = 175, .col_g = 95, .col_b = 95,
        .produces = RES_SAUSAGES, .produce_amt = 1,
        .consumes = { RES_PIGS, RES_COUNT, RES_COUNT },
        .consume_amt = { 1, 0, 0 },
        .tick_seconds = 11.0f,
        .cost = { [RES_WOOD] = 15, [RES_GOLD] = 150 },
        .hud_placeable = 1
    },
    [BUILDING_TALLOW_WORKS] = {
        .name = "Tallow Works",
        .category = BCAT_REFINERY,
        .tile_w = 1, .tile_h = 1,
        .placement_flags = PLACE_ANY_LAND,
        .col_r = 200, .col_g = 190, .col_b = 150,
        .produces = RES_TALLOW, .produce_amt = 1,
        .consumes = { RES_PIGS, RES_COUNT, RES_COUNT },
        .consume_amt = { 1, 0, 0 },
        .tick_seconds = 11.0f,
        .cost = { [RES_WOOD] = 10, [RES_GOLD] = 110 },
        .hud_placeable = 1
    },
    [BUILDING_SOAP_BOILERY] = {
        .name = "Soap Boilery",
        .category = BCAT_WORKSHOP,
        .tile_w = 2, .tile_h = 2,
        .placement_flags = PLACE_ANY_LAND,
        .col_r = 210, .col_g = 205, .col_b = 190,
        .produces = RES_SOAP, .produce_amt = 1,
        .consumes = { RES_TALLOW, RES_COUNT, RES_COUNT },
        .consume_amt = { 1, 0, 0 },
        .tick_seconds = 13.0f,
        .cost = { [RES_WOOD] = 15, [RES_GOLD] = 160 },
        .hud_placeable = 1
    },
    [BUILDING_WINDMILL] = {
        .name = "Windmill",
        .category = BCAT_REFINERY,
        .tile_w = 2, .tile_h = 2,
        .placement_flags = PLACE_ANY_LAND,
        .col_r = 205, .col_g = 195, .col_b = 165,
        .produces = RES_FLOUR, .produce_amt = 1,
        .consumes = { RES_GRAIN, RES_COUNT, RES_COUNT },
        .consume_amt = { 1, 0, 0 },
        .tick_seconds = 8.0f,
        .cost = { [RES_WOOD] = 20, [RES_GOLD] = 120 },
        .hud_placeable = 1
    },
    [BUILDING_BAKEHOUSE] = {
        .name = "Bakehouse",
        .category = BCAT_WORKSHOP,
        .tile_w = 2, .tile_h = 2,
        .placement_flags = PLACE_ANY_LAND,
        .col_r = 195, .col_g = 150, .col_b = 95,
        .produces = RES_BREAD, .produce_amt = 1,
        .consumes = { RES_FLOUR, RES_COUNT, RES_COUNT },
        .consume_amt = { 1, 0, 0 },
        .tick_seconds = 10.0f,
        .cost = { [RES_WOOD] = 15, [RES_GOLD] = 140 },
        .hud_placeable = 1
    },

    /* ---- SUPPLY_CHAIN Phase 4: iron, glass and the Artisans ----
     *
     * Deeper than anything before it. Phase 3's chains were two steps
     * from a field; these run three and four, and the Bloomery is the
     * first building whose inputs are BOTH themselves manufactured —
     * ore from a mine and charcoal from a kiln — so an Artisans
     * district is a district, not a building.
     *
     * Steel Beams are the phase's build material, the way Bricks were
     * Phase 3's: nothing consumes them in a chain, they are what the
     * heavy industry below is made of. That is also what keeps them off
     * test_chains' orphan list, and it is deliberate rather than
     * incidental — a good with no consumer is a chain someone forgot to
     * finish. */
    [BUILDING_CHARCOAL_KILN] = {
        .name = "Charcoal Kiln",
        /* A furnace, not a bench: BCAT_FACTORY's own definition is
         * "heavy industry: furnaces, machine shops", which this fits
         * better than "one artisan's worth of processing". Moved in
         * SUPPLY_CHAIN Phase 6, when Workshops outgrew the build bar
         * by exactly one slot -- but moved because it is more nearly
         * true, not only because it fit. */
        .category = BCAT_FACTORY,
        .tile_w = 1, .tile_h = 1,
        .placement_flags = PLACE_ANY_LAND,
        .col_r = 70, .col_g = 62, .col_b = 58,
        .produces = RES_CHARCOAL, .produce_amt = 1,
        .consumes = { RES_WOOD, RES_COUNT, RES_COUNT },
        .consume_amt = { 1, 0, 0 },
        .tick_seconds = 7.0f,
        .cost = { [RES_WOOD] = 15, [RES_GOLD] = 90 },
        .hud_placeable = 1
    },
    [BUILDING_IRON_MINE] = {
        .name = "Iron Mine",
        .category = BCAT_EXTRACTION,
        /* 1x1 like the Clay Pit, and for the same reason: needs_deposit
         * demands the seam under EVERY tile of the footprint, and the
         * scatter pass lays deposits down one tile at a time. A 2x2
         * mine would need four adjacent iron tiles and would therefore
         * never place anywhere — which is exactly how test_chains
         * caught it, as "nothing anywhere can produce Iron Ore". */
        .tile_w = 1, .tile_h = 1,
        .placement_flags = PLACE_ANY_LAND,
        .needs_deposit = DEPOSIT_IRON,
        .col_r = 130, .col_g = 95, .col_b = 80,
        .produces = RES_IRON_ORE, .produce_amt = 1,
        .consumes = { RES_COUNT, RES_COUNT, RES_COUNT },
        .consume_amt = { 0, 0, 0 },
        .tick_seconds = 8.0f,
        .cost = { [RES_PLANKS] = 10, [RES_GOLD] = 160 },
        .hud_placeable = 1
    },
    [BUILDING_COAL_MINE] = {
        .name = "Coal Mine",
        .category = BCAT_EXTRACTION,
        /* 1x1 like the Clay Pit, and for the same reason: needs_deposit
         * demands the seam under EVERY tile of the footprint, and the
         * scatter pass lays deposits down one tile at a time. A 2x2
         * mine would need four adjacent iron tiles and would therefore
         * never place anywhere — which is exactly how test_chains
         * caught it, as "nothing anywhere can produce Iron Ore". */
        .tile_w = 1, .tile_h = 1,
        .placement_flags = PLACE_ANY_LAND,
        .needs_deposit = DEPOSIT_COAL,
        .col_r = 60, .col_g = 58, .col_b = 62,
        .produces = RES_COAL, .produce_amt = 1,
        .consumes = { RES_COUNT, RES_COUNT, RES_COUNT },
        .consume_amt = { 0, 0, 0 },
        .tick_seconds = 8.0f,
        .cost = { [RES_PLANKS] = 10, [RES_GOLD] = 160 },
        .hud_placeable = 1
    },
    /* The first building whose every input is itself manufactured. */
    [BUILDING_BLOOMERY] = {
        .name = "Bloomery",
        .category = BCAT_FACTORY,
        .tile_w = 2, .tile_h = 2,
        .placement_flags = PLACE_ANY_LAND,
        .col_r = 150, .col_g = 80, .col_b = 50,
        .produces = RES_IRON, .produce_amt = 1,
        .consumes = { RES_IRON_ORE, RES_CHARCOAL, RES_COUNT },
        .consume_amt = { 1, 1, 0 },
        .tick_seconds = 10.0f,
        .cost = { [RES_BRICKS] = 12, [RES_GOLD] = 200 },
        .hud_placeable = 1
    },
    [BUILDING_IRONWORKS] = {
        .name = "Ironworks",
        .category = BCAT_FACTORY,
        .tile_w = 2, .tile_h = 2,
        .placement_flags = PLACE_ANY_LAND,
        .col_r = 120, .col_g = 70, .col_b = 60,
        .produces = RES_STEEL_BEAMS, .produce_amt = 1,
        .consumes = { RES_IRON, RES_COUNT, RES_COUNT },
        .consume_amt = { 1, 0, 0 },
        .tick_seconds = 12.0f,
        .cost = { [RES_BRICKS] = 15, [RES_GOLD] = 240 },
        .hud_placeable = 1
    },
    [BUILDING_SAND_PIT] = {
        .name = "Sand Pit",
        .category = BCAT_EXTRACTION,
        .tile_w = 1, .tile_h = 1,
        .placement_flags = PLACE_ANY_LAND,
        .needs_deposit = DEPOSIT_SAND,
        .col_r = 215, .col_g = 195, .col_b = 140,
        .produces = RES_SAND, .produce_amt = 1,
        .consumes = { RES_COUNT, RES_COUNT, RES_COUNT },
        .consume_amt = { 0, 0, 0 },
        .tick_seconds = 6.0f,
        .cost = { [RES_GOLD] = 90 },
        .hud_placeable = 1
    },
    [BUILDING_GLASSWORKS] = {
        .name = "Glassworks",
        /* A furnace, not a bench: BCAT_FACTORY's own definition is
         * "heavy industry: furnaces, machine shops", which this fits
         * better than "one artisan's worth of processing". Moved in
         * SUPPLY_CHAIN Phase 6, when Workshops outgrew the build bar
         * by exactly one slot -- but moved because it is more nearly
         * true, not only because it fit. */
        .category = BCAT_FACTORY,
        .tile_w = 2, .tile_h = 2,
        .placement_flags = PLACE_ANY_LAND,
        .col_r = 150, .col_g = 200, .col_b = 205,
        .produces = RES_GLASS, .produce_amt = 1,
        .consumes = { RES_SAND, RES_COUNT, RES_COUNT },
        .consume_amt = { 1, 0, 0 },
        .tick_seconds = 8.0f,
        .cost = { [RES_BRICKS] = 10, [RES_GOLD] = 170 },
        .hud_placeable = 1
    },
    [BUILDING_BRASS_FOUNDRY] = {
        .name = "Brass Foundry",
        .category = BCAT_FACTORY,
        .tile_w = 2, .tile_h = 2,
        .placement_flags = PLACE_ANY_LAND,
        .col_r = 190, .col_g = 155, .col_b = 60,
        .produces = RES_BRASS, .produce_amt = 1,
        .consumes = { RES_IRON, RES_CHARCOAL, RES_COUNT },
        .consume_amt = { 1, 1, 0 },
        .tick_seconds = 11.0f,
        .cost = { [RES_BRICKS] = 12, [RES_GOLD] = 210 },
        .hud_placeable = 1
    },
    [BUILDING_WINDOW_SHOP] = {
        .name = "Window Shop",
        .category = BCAT_WORKSHOP,
        .tile_w = 2, .tile_h = 2,
        .placement_flags = PLACE_ANY_LAND,
        .col_r = 175, .col_g = 210, .col_b = 220,
        .produces = RES_WINDOWS, .produce_amt = 1,
        .consumes = { RES_GLASS, RES_PLANKS, RES_COUNT },
        .consume_amt = { 1, 1, 0 },
        .tick_seconds = 12.0f,
        .cost = { [RES_STEEL_BEAMS] = 8, [RES_GOLD] = 260 },
        .hud_placeable = 1
    },
    [BUILDING_SPECTACLE_SHOP] = {
        .name = "Spectacle Shop",
        .category = BCAT_LUXURY,
        .tile_w = 2, .tile_h = 2,
        .placement_flags = PLACE_ANY_LAND,
        .col_r = 205, .col_g = 190, .col_b = 120,
        .produces = RES_SPECTACLES, .produce_amt = 1,
        .consumes = { RES_GLASS, RES_BRASS, RES_COUNT },
        .consume_amt = { 1, 1, 0 },
        .tick_seconds = 14.0f,
        .cost = { [RES_STEEL_BEAMS] = 8, [RES_GOLD] = 280 },
        .hud_placeable = 1
    },
    /* Grazing again, competing with sheep and pigs for the same grass
     * — the same decision Phase 3's Sheep Pasture created, now with a
     * third claimant. */
    [BUILDING_CATTLE_PEN] = {
        .name = "Cattle Pen",
        .category = BCAT_FARMING,
        .tile_w = 2, .tile_h = 2,
        .placement_flags = PLACE_ANY_LAND,
        .needs_fertility = FERTILE_PASTURE,
        .col_r = 165, .col_g = 130, .col_b = 105,
        .produces = RES_CATTLE, .produce_amt = 1,
        .consumes = { RES_COUNT, RES_COUNT, RES_COUNT },
        .consume_amt = { 0, 0, 0 },
        .tick_seconds = 9.0f,
        .cost = { [RES_WOOD] = 15, [RES_GOLD] = 120 },
        .hud_placeable = 1
    },
    [BUILDING_PEPPER_FIELD] = {
        .name = "Pepper Field",
        .category = BCAT_FARMING,
        .tile_w = 2, .tile_h = 2,
        .placement_flags = PLACE_ANY_LAND,
        .needs_fertility = FERTILE_PEPPER,
        .col_r = 120, .col_g = 160, .col_b = 70,
        .produces = RES_PEPPER, .produce_amt = 1,
        .consumes = { RES_COUNT, RES_COUNT, RES_COUNT },
        .consume_amt = { 0, 0, 0 },
        .tick_seconds = 7.0f,
        .cost = { [RES_WOOD] = 10, [RES_GOLD] = 100 },
        .hud_placeable = 1
    },
    [BUILDING_KITCHEN] = {
        .name = "Kitchen",
        .category = BCAT_WORKSHOP,
        .tile_w = 2, .tile_h = 2,
        .placement_flags = PLACE_ANY_LAND,
        .col_r = 200, .col_g = 120, .col_b = 95,
        .produces = RES_POTTED_MEAT, .produce_amt = 1,
        .consumes = { RES_CATTLE, RES_PEPPER, RES_COUNT },
        .consume_amt = { 1, 1, 0 },
        .tick_seconds = 10.0f,
        .cost = { [RES_BRICKS] = 8, [RES_GOLD] = 150 },
        .hud_placeable = 1
    },
    [BUILDING_CANNERY] = {
        .name = "Cannery",
        .category = BCAT_FACTORY,
        .tile_w = 2, .tile_h = 2,
        .placement_flags = PLACE_ANY_LAND,
        .col_r = 175, .col_g = 165, .col_b = 150,
        .produces = RES_PRESERVES, .produce_amt = 1,
        .consumes = { RES_POTTED_MEAT, RES_COUNT, RES_COUNT },
        .consume_amt = { 1, 0, 0 },
        .tick_seconds = 12.0f,
        .cost = { [RES_STEEL_BEAMS] = 6, [RES_GOLD] = 230 },
        .hud_placeable = 1
    },
    [BUILDING_FOUNDRY] = {
        .name = "Foundry",
        .category = BCAT_FACTORY,
        .tile_w = 2, .tile_h = 2,
        .placement_flags = PLACE_ANY_LAND,
        .col_r = 100, .col_g = 90, .col_b = 95,
        .produces = RES_STEEL, .produce_amt = 1,
        .consumes = { RES_IRON, RES_COAL, RES_COUNT },
        .consume_amt = { 1, 1, 0 },
        .tick_seconds = 12.0f,
        .cost = { [RES_BRICKS] = 15, [RES_GOLD] = 250 },
        .hud_placeable = 1
    },
    [BUILDING_MACHINE_SHOP] = {
        .name = "Machine Shop",
        .category = BCAT_FACTORY,
        .tile_w = 2, .tile_h = 2,
        .placement_flags = PLACE_ANY_LAND,
        .col_r = 140, .col_g = 145, .col_b = 160,
        .produces = RES_SEWING_MACHINES, .produce_amt = 1,
        .consumes = { RES_STEEL, RES_PLANKS, RES_COUNT },
        .consume_amt = { 1, 1, 0 },
        .tick_seconds = 15.0f,
        .cost = { [RES_STEEL_BEAMS] = 10, [RES_GOLD] = 320 },
        .hud_placeable = 1
    },
    /* No HUD slot: the only way to one is upgrading a Marsh Cottage,
     * which is what makes this the first tier the upgrade rule
     * actually runs on end to end. cost[] is what the upgrade charges
     * beyond the tier's Gold — see TIER_DEFS. */
    [BUILDING_HOUSE_ARTISAN] = {
        .name = "Artisan's House",
        .category = BCAT_HOUSING,
        .tile_w = 1, .tile_h = 1,
        .placement_flags = PLACE_ANY_LAND,
        .col_r = 205, .col_g = 165, .col_b = 190,
        .produces = RES_COUNT, .produce_amt = 0,
        .consumes = { RES_COUNT, RES_COUNT, RES_COUNT },
        .consume_amt = { 0, 0, 0 },
        .tick_seconds = 0.0f,
        .cost = { [RES_BRICKS] = 10, [RES_GOLD] = 250 },
        .hud_placeable = 0
    },

    /* ---- SUPPLY_CHAIN Phase 5: the southern islands ----
     * The chain Phase 4 could not finish. Cotton grows on no northern
     * profile, so every Fur Coat in the archipelago starts as a voyage.
     */
    [BUILDING_COTTON_FIELD] = {
        .name = "Cotton Field",
        .category = BCAT_FARMING,
        .tile_w = 2, .tile_h = 2,
        .placement_flags = PLACE_ANY_LAND,
        .needs_fertility = FERTILE_COTTON,
        .col_r = 225, .col_g = 225, .col_b = 210,
        .produces = RES_COTTON, .produce_amt = 1,
        .consumes = { RES_COUNT, RES_COUNT, RES_COUNT },
        .consume_amt = { 0, 0, 0 },
        .tick_seconds = 7.0f,
        .cost = { [RES_WOOD] = 10, [RES_GOLD] = 100 },
        .hud_placeable = 1
    },
    [BUILDING_SPINNING_MILL] = {
        .name = "Spinning Mill",
        .category = BCAT_REFINERY,
        .tile_w = 2, .tile_h = 2,
        .placement_flags = PLACE_ANY_LAND,
        .col_r = 200, .col_g = 195, .col_b = 175,
        .produces = RES_CLOTH, .produce_amt = 1,
        .consumes = { RES_COTTON, RES_COUNT, RES_COUNT },
        .consume_amt = { 1, 0, 0 },
        .tick_seconds = 8.0f,
        .cost = { [RES_PLANKS] = 12, [RES_GOLD] = 150 },
        .hud_placeable = 1
    },
    /* Beside the forest, not on it — the same standing-next-to-what-it
     * works rule as the Fisher's Hut and the pearl station. A lodge on
     * a forest tile would be a lodge on a tile nothing can build on. */
    [BUILDING_TRAPPERS_LODGE] = {
        .name = "Trapper's Lodge",
        .category = BCAT_EXTRACTION,
        .tile_w = 1, .tile_h = 1,
        .placement_flags = PLACE_NEEDS_FOREST,
        .col_r = 125, .col_g = 95, .col_b = 65,
        .produces = RES_PELTS, .produce_amt = 1,
        .consumes = { RES_COUNT, RES_COUNT, RES_COUNT },
        .consume_amt = { 0, 0, 0 },
        .tick_seconds = 8.0f,
        .cost = { [RES_WOOD] = 10, [RES_GOLD] = 90 },
        .hud_placeable = 1
    },
    [BUILDING_FURRIER] = {
        .name = "Furrier",
        .category = BCAT_LUXURY,
        .tile_w = 2, .tile_h = 2,
        .placement_flags = PLACE_ANY_LAND,
        .col_r = 150, .col_g = 110, .col_b = 120,
        .produces = RES_FUR_COATS, .produce_amt = 1,
        .consumes = { RES_PELTS, RES_CLOTH, RES_COUNT },
        .consume_amt = { 1, 1, 0 },
        .tick_seconds = 13.0f,
        .cost = { [RES_STEEL_BEAMS] = 6, [RES_GOLD] = 260 },
        .hud_placeable = 1
    },

    /* ---- SUPPLY_CHAIN Phase 6: Engineers ----
     *
     * The tier that needs the whole archipelago. Gold ore is highland
     * and nowhere else, lac is jungle and nowhere else, lobster wants
     * any coast, and the glass and brass behind the rest are Phase 4's
     * northern industry -- so an Engineers neighbourhood is an argument
     * for holding one of everything.
     *
     * The Watchmaker's and the Gramophone Works are the first
     * three-input buildings in real content, which is the limit Phase 2
     * reserved and nothing until now has exercised. */
    [BUILDING_GOLD_MINE] = {
        .name = "Gold Mine",
        .category = BCAT_EXTRACTION,
        /* 1x1, like every other mine -- see the Iron Mine's note. */
        .tile_w = 1, .tile_h = 1,
        .placement_flags = PLACE_ANY_LAND,
        .needs_deposit = DEPOSIT_GOLD_ORE,
        .col_r = 205, .col_g = 175, .col_b = 60,
        .produces = RES_GOLD_ORE, .produce_amt = 1,
        .consumes = { RES_COUNT, RES_COUNT, RES_COUNT },
        .consume_amt = { 0, 0, 0 },
        .tick_seconds = 11.0f,
        .cost = { [RES_PLANKS] = 12, [RES_GOLD] = 220 },
        .hud_placeable = 1
    },
    [BUILDING_WIRE_MILL] = {
        .name = "Wire Mill",
        .category = BCAT_REFINERY,
        .tile_w = 2, .tile_h = 2,
        .placement_flags = PLACE_ANY_LAND,
        .col_r = 165, .col_g = 165, .col_b = 175,
        .produces = RES_WIRE, .produce_amt = 1,
        .consumes = { RES_IRON, RES_COUNT, RES_COUNT },
        .consume_amt = { 1, 0, 0 },
        .tick_seconds = 9.0f,
        .cost = { [RES_BRICKS] = 10, [RES_GOLD] = 180 },
        .hud_placeable = 1
    },
    [BUILDING_SPRING_WORKS] = {
        .name = "Spring Works",
        .category = BCAT_REFINERY,
        .tile_w = 2, .tile_h = 2,
        .placement_flags = PLACE_ANY_LAND,
        .col_r = 150, .col_g = 155, .col_b = 145,
        .produces = RES_SPRINGS, .produce_amt = 1,
        .consumes = { RES_IRON, RES_COUNT, RES_COUNT },
        .consume_amt = { 1, 0, 0 },
        .tick_seconds = 9.0f,
        .cost = { [RES_BRICKS] = 10, [RES_GOLD] = 180 },
        .hud_placeable = 1
    },
    [BUILDING_LAMP_WORKS] = {
        .name = "Lamp Works",
        .category = BCAT_FACTORY,
        .tile_w = 2, .tile_h = 2,
        .placement_flags = PLACE_ANY_LAND,
        .col_r = 235, .col_g = 215, .col_b = 130,
        .produces = RES_LAMPS, .produce_amt = 1,
        .consumes = { RES_GLASS, RES_WIRE, RES_COUNT },
        .consume_amt = { 1, 1, 0 },
        .tick_seconds = 14.0f,
        .cost = { [RES_STEEL_BEAMS] = 8, [RES_GOLD] = 300 },
        .hud_placeable = 1
    },
    /* First three-input building in the shipped table. All-or-nothing
     * like every other: no gold ore, no watches, however many springs
     * are on the shelf. */
    [BUILDING_WATCHMAKERS] = {
        .name = "Watchmaker's",
        .category = BCAT_LUXURY,
        .tile_w = 2, .tile_h = 2,
        .placement_flags = PLACE_ANY_LAND,
        .col_r = 215, .col_g = 190, .col_b = 110,
        .produces = RES_POCKET_WATCHES, .produce_amt = 1,
        .consumes = { RES_GOLD_ORE, RES_GLASS, RES_SPRINGS },
        .consume_amt = { 1, 1, 1 },
        .tick_seconds = 18.0f,
        .cost = { [RES_STEEL_BEAMS] = 10, [RES_GOLD] = 380 },
        .hud_placeable = 1
    },
    /* Brought forward from Phase 5's deferred southern list: it was
     * held back only because nothing consumed Shellac, and the
     * Gramophone Works now does. */
    [BUILDING_LAC_GROVE] = {
        .name = "Lac Grove",
        .category = BCAT_FARMING,
        .tile_w = 2, .tile_h = 2,
        .placement_flags = PLACE_ANY_LAND,
        .needs_fertility = FERTILE_LAC,
        .col_r = 150, .col_g = 90, .col_b = 70,
        .produces = RES_SHELLAC, .produce_amt = 1,
        .consumes = { RES_COUNT, RES_COUNT, RES_COUNT },
        .consume_amt = { 0, 0, 0 },
        .tick_seconds = 8.0f,
        .cost = { [RES_WOOD] = 10, [RES_GOLD] = 110 },
        .hud_placeable = 1
    },
    [BUILDING_GRAMOPHONE_WORKS] = {
        .name = "Gramophone Works",
        .category = BCAT_FACTORY,
        .tile_w = 2, .tile_h = 2,
        .placement_flags = PLACE_ANY_LAND,
        .col_r = 130, .col_g = 100, .col_b = 90,
        .produces = RES_GRAMOPHONES, .produce_amt = 1,
        .consumes = { RES_PLANKS, RES_BRASS, RES_SHELLAC },
        .consume_amt = { 1, 1, 1 },
        .tick_seconds = 18.0f,
        .cost = { [RES_STEEL_BEAMS] = 10, [RES_GOLD] = 400 },
        .hud_placeable = 1
    },
    [BUILDING_LOBSTER_POTS] = {
        .name = "Lobster Pots",
        .category = BCAT_FARMING,
        .tile_w = 1, .tile_h = 1,
        .placement_flags = PLACE_NEEDS_COAST,
        .col_r = 190, .col_g = 80, .col_b = 70,
        .produces = RES_LOBSTER, .produce_amt = 1,
        .consumes = { RES_COUNT, RES_COUNT, RES_COUNT },
        .consume_amt = { 0, 0, 0 },
        .tick_seconds = 10.0f,
        .cost = { [RES_WOOD] = 12, [RES_GOLD] = 110 },
        .hud_placeable = 1
    },
    [BUILDING_FINE_KITCHEN] = {
        .name = "Fine Kitchen",
        .category = BCAT_LUXURY,
        .tile_w = 2, .tile_h = 2,
        .placement_flags = PLACE_ANY_LAND,
        .col_r = 215, .col_g = 150, .col_b = 140,
        .produces = RES_BANQUET, .produce_amt = 1,
        .consumes = { RES_LOBSTER, RES_PRESERVES, RES_COUNT },
        .consume_amt = { 1, 1, 0 },
        .tick_seconds = 16.0f,
        .cost = { [RES_BRICKS] = 12, [RES_GOLD] = 320 },
        .hud_placeable = 1
    },
    /* Upgrade-only, like the Artisan's House: the second line's top. */
    [BUILDING_HOUSE_ENGINEER] = {
        .name = "Engineer's House",
        .category = BCAT_HOUSING,
        .tile_w = 1, .tile_h = 1,
        .placement_flags = PLACE_ANY_LAND,
        .col_r = 170, .col_g = 195, .col_b = 215,
        .produces = RES_COUNT, .produce_amt = 0,
        .consumes = { RES_COUNT, RES_COUNT, RES_COUNT },
        .consume_amt = { 0, 0, 0 },
        .tick_seconds = 0.0f,
        .cost = { [RES_STEEL_BEAMS] = 8, [RES_GOLD] = 320 },
        .hud_placeable = 0
    },

    /* ---- SUPPLY_CHAIN Phase 7: Merchants and Investors ----
     *
     * The third line, both halves. Every Merchants good begins in the
     * south -- coffee and plantains in the jungle, cane, maize and
     * alpaca on the plantations -- so the climate Phase 5 opened stops
     * being a novelty and becomes what the top of the economy runs on.
     *
     * The Investors goods are the scarcity tier: grapes grow only on
     * the highland, pearls lie only off an atoll, and Jewellery wants
     * gold ore and pearls together, which is the two rarest deposits
     * in the world in one building.
     *
     * Sails, Wool Cloaks and Plantain Fry are here because the plan
     * listed their chains and no tier ever asked for them. A producer
     * nothing consumes is a chain someone forgot to finish, so rather
     * than leave three of those in the table, Sails became what a
     * Shipyard is built from and the other two joined the Merchants
     * list -- taking it to six needs, and MAX_TIER_GOODS with it. */
    [BUILDING_COFFEE_GROVE] = {
        .name = "Coffee Grove",
        .category = BCAT_FARMING,
        .tile_w = 2, .tile_h = 2,
        .placement_flags = PLACE_ANY_LAND,
        .needs_fertility = FERTILE_COFFEE,
        .col_r = 90, .col_g = 70, .col_b = 50,
        .produces = RES_COFFEE_BEANS, .produce_amt = 1,
        .consumes = { RES_COUNT, RES_COUNT, RES_COUNT },
        .consume_amt = { 0, 0, 0 },
        .tick_seconds = 8.0f,
        .cost = { [RES_WOOD] = 10, [RES_GOLD] = 110 },
        .hud_placeable = 1
    },
    [BUILDING_ROASTERY] = {
        .name = "Roastery",
        .category = BCAT_REFINERY,
        .tile_w = 2, .tile_h = 2,
        .placement_flags = PLACE_ANY_LAND,
        .col_r = 110, .col_g = 80, .col_b = 60,
        .produces = RES_COFFEE, .produce_amt = 1,
        .consumes = { RES_COFFEE_BEANS, RES_COUNT, RES_COUNT },
        .consume_amt = { 1, 0, 0 },
        .tick_seconds = 9.0f,
        .cost = { [RES_BRICKS] = 8, [RES_GOLD] = 170 },
        .hud_placeable = 1
    },
    [BUILDING_CANE_FIELD] = {
        .name = "Cane Field",
        .category = BCAT_FARMING,
        .tile_w = 2, .tile_h = 2,
        .placement_flags = PLACE_ANY_LAND,
        .needs_fertility = FERTILE_CANE,
        .col_r = 170, .col_g = 190, .col_b = 90,
        .produces = RES_CANE, .produce_amt = 1,
        .consumes = { RES_COUNT, RES_COUNT, RES_COUNT },
        .consume_amt = { 0, 0, 0 },
        .tick_seconds = 7.0f,
        .cost = { [RES_WOOD] = 10, [RES_GOLD] = 100 },
        .hud_placeable = 1
    },
    [BUILDING_SUGAR_REFINERY] = {
        .name = "Sugar Refinery",
        .category = BCAT_REFINERY,
        .tile_w = 2, .tile_h = 2,
        .placement_flags = PLACE_ANY_LAND,
        .col_r = 225, .col_g = 220, .col_b = 205,
        .produces = RES_SUGAR, .produce_amt = 1,
        .consumes = { RES_CANE, RES_COUNT, RES_COUNT },
        .consume_amt = { 1, 0, 0 },
        .tick_seconds = 8.0f,
        .cost = { [RES_BRICKS] = 8, [RES_GOLD] = 170 },
        .hud_placeable = 1
    },
    [BUILDING_RUM_HOUSE] = {
        .name = "Rum House",
        .category = BCAT_LUXURY,
        .tile_w = 2, .tile_h = 2,
        .placement_flags = PLACE_ANY_LAND,
        .col_r = 160, .col_g = 90, .col_b = 50,
        .produces = RES_RUM, .produce_amt = 1,
        .consumes = { RES_SUGAR, RES_PLANKS, RES_COUNT },
        .consume_amt = { 1, 1, 0 },
        .tick_seconds = 12.0f,
        .cost = { [RES_BRICKS] = 10, [RES_GOLD] = 240 },
        .hud_placeable = 1
    },
    [BUILDING_MAIZE_FIELD] = {
        .name = "Maize Field",
        .category = BCAT_FARMING,
        .tile_w = 2, .tile_h = 2,
        .placement_flags = PLACE_ANY_LAND,
        .needs_fertility = FERTILE_MAIZE,
        .col_r = 210, .col_g = 190, .col_b = 80,
        .produces = RES_MAIZE, .produce_amt = 1,
        .consumes = { RES_COUNT, RES_COUNT, RES_COUNT },
        .consume_amt = { 0, 0, 0 },
        .tick_seconds = 7.0f,
        .cost = { [RES_WOOD] = 10, [RES_GOLD] = 100 },
        .hud_placeable = 1
    },
    [BUILDING_FLATBREAD_KITCHEN] = {
        .name = "Flatbread Kitchen",
        .category = BCAT_LUXURY,
        .tile_w = 2, .tile_h = 2,
        .placement_flags = PLACE_ANY_LAND,
        .col_r = 220, .col_g = 180, .col_b = 130,
        .produces = RES_FLATBREAD, .produce_amt = 1,
        .consumes = { RES_MAIZE, RES_CATTLE, RES_COUNT },
        .consume_amt = { 1, 1, 0 },
        .tick_seconds = 11.0f,
        .cost = { [RES_BRICKS] = 8, [RES_GOLD] = 200 },
        .hud_placeable = 1
    },
    [BUILDING_ALPACA_PASTURE] = {
        .name = "Alpaca Pasture",
        .category = BCAT_FARMING,
        .tile_w = 2, .tile_h = 2,
        .placement_flags = PLACE_ANY_LAND,
        .needs_fertility = FERTILE_ALPACA,
        .col_r = 190, .col_g = 175, .col_b = 160,
        .produces = RES_ALPACA_WOOL, .produce_amt = 1,
        .consumes = { RES_COUNT, RES_COUNT, RES_COUNT },
        .consume_amt = { 0, 0, 0 },
        .tick_seconds = 8.0f,
        .cost = { [RES_WOOD] = 12, [RES_GOLD] = 120 },
        .hud_placeable = 1
    },
    [BUILDING_FELT_WORKS] = {
        .name = "Felt Works",
        .category = BCAT_REFINERY,
        .tile_w = 2, .tile_h = 2,
        .placement_flags = PLACE_ANY_LAND,
        .col_r = 180, .col_g = 170, .col_b = 180,
        .produces = RES_FELT, .produce_amt = 1,
        .consumes = { RES_COTTON, RES_ALPACA_WOOL, RES_COUNT },
        .consume_amt = { 1, 1, 0 },
        .tick_seconds = 10.0f,
        .cost = { [RES_PLANKS] = 10, [RES_GOLD] = 190 },
        .hud_placeable = 1
    },
    [BUILDING_HATTER] = {
        .name = "Hatter",
        .category = BCAT_LUXURY,
        .tile_w = 2, .tile_h = 2,
        .placement_flags = PLACE_ANY_LAND,
        .col_r = 120, .col_g = 100, .col_b = 110,
        .produces = RES_MARSH_HATS, .produce_amt = 1,
        .consumes = { RES_FELT, RES_COUNT, RES_COUNT },
        .consume_amt = { 1, 0, 0 },
        .tick_seconds = 13.0f,
        .cost = { [RES_STEEL_BEAMS] = 6, [RES_GOLD] = 260 },
        .hud_placeable = 1
    },
    [BUILDING_DARNING_HOUSE] = {
        .name = "Darning House",
        .category = BCAT_LUXURY,
        .tile_w = 2, .tile_h = 2,
        .placement_flags = PLACE_ANY_LAND,
        .col_r = 170, .col_g = 140, .col_b = 150,
        .produces = RES_WOOL_CLOAKS, .produce_amt = 1,
        .consumes = { RES_ALPACA_WOOL, RES_COUNT, RES_COUNT },
        .consume_amt = { 1, 0, 0 },
        .tick_seconds = 11.0f,
        .cost = { [RES_PLANKS] = 10, [RES_GOLD] = 200 },
        .hud_placeable = 1
    },
    [BUILDING_PLANTAIN_GROVE] = {
        .name = "Plantain Grove",
        .category = BCAT_FARMING,
        .tile_w = 2, .tile_h = 2,
        .placement_flags = PLACE_ANY_LAND,
        .needs_fertility = FERTILE_PLANTAIN,
        .col_r = 140, .col_g = 180, .col_b = 90,
        .produces = RES_PLANTAIN, .produce_amt = 1,
        .consumes = { RES_COUNT, RES_COUNT, RES_COUNT },
        .consume_amt = { 0, 0, 0 },
        .tick_seconds = 7.0f,
        .cost = { [RES_WOOD] = 10, [RES_GOLD] = 100 },
        .hud_placeable = 1
    },
    [BUILDING_FISH_OIL_RENDERY] = {
        .name = "Fish Oil Rendery",
        .category = BCAT_REFINERY,
        .tile_w = 2, .tile_h = 2,
        .placement_flags = PLACE_NEEDS_COAST,
        .col_r = 150, .col_g = 155, .col_b = 120,
        .produces = RES_FISH_OIL, .produce_amt = 1,
        .consumes = { RES_FISH, RES_COUNT, RES_COUNT },
        .consume_amt = { 1, 0, 0 },
        .tick_seconds = 9.0f,
        .cost = { [RES_PLANKS] = 8, [RES_GOLD] = 150 },
        .hud_placeable = 1
    },
    [BUILDING_FRY_KITCHEN] = {
        .name = "Fry Kitchen",
        .category = BCAT_LUXURY,
        .tile_w = 2, .tile_h = 2,
        .placement_flags = PLACE_ANY_LAND,
        .col_r = 215, .col_g = 175, .col_b = 95,
        .produces = RES_PLANTAIN_FRY, .produce_amt = 1,
        .consumes = { RES_PLANTAIN, RES_FISH_OIL, RES_COUNT },
        .consume_amt = { 1, 1, 0 },
        .tick_seconds = 11.0f,
        .cost = { [RES_BRICKS] = 8, [RES_GOLD] = 200 },
        .hud_placeable = 1
    },
    [BUILDING_SAIL_LOFT] = {
        .name = "Sail Loft",
        .category = BCAT_REFINERY,
        .tile_w = 2, .tile_h = 2,
        .placement_flags = PLACE_NEEDS_COAST,
        .col_r = 230, .col_g = 230, .col_b = 220,
        .produces = RES_SAILS, .produce_amt = 1,
        .consumes = { RES_CLOTH, RES_COUNT, RES_COUNT },
        .consume_amt = { 1, 0, 0 },
        .tick_seconds = 11.0f,
        .cost = { [RES_PLANKS] = 12, [RES_GOLD] = 210 },
        .hud_placeable = 1
    },
    [BUILDING_VINEYARD] = {
        .name = "Vineyard",
        .category = BCAT_FARMING,
        .tile_w = 2, .tile_h = 2,
        .placement_flags = PLACE_ANY_LAND,
        .needs_fertility = FERTILE_GRAPES,
        .col_r = 130, .col_g = 90, .col_b = 140,
        .produces = RES_GRAPES, .produce_amt = 1,
        .consumes = { RES_COUNT, RES_COUNT, RES_COUNT },
        .consume_amt = { 0, 0, 0 },
        .tick_seconds = 8.0f,
        .cost = { [RES_WOOD] = 10, [RES_GOLD] = 120 },
        .hud_placeable = 1
    },
    [BUILDING_SPARKLING_CELLAR] = {
        .name = "Sparkling Cellar",
        .category = BCAT_LUXURY,
        .tile_w = 2, .tile_h = 2,
        .placement_flags = PLACE_ANY_LAND,
        .col_r = 200, .col_g = 190, .col_b = 215,
        .produces = RES_SPARKLING_WINE, .produce_amt = 1,
        .consumes = { RES_GRAPES, RES_COUNT, RES_COUNT },
        .consume_amt = { 1, 0, 0 },
        .tick_seconds = 15.0f,
        .cost = { [RES_BRICKS] = 12, [RES_GOLD] = 300 },
        .hud_placeable = 1
    },
    [BUILDING_TOBACCO_FIELD] = {
        .name = "Tobacco Field",
        .category = BCAT_FARMING,
        .tile_w = 2, .tile_h = 2,
        .placement_flags = PLACE_ANY_LAND,
        .needs_fertility = FERTILE_TOBACCO,
        .col_r = 150, .col_g = 160, .col_b = 80,
        .produces = RES_TOBACCO, .produce_amt = 1,
        .consumes = { RES_COUNT, RES_COUNT, RES_COUNT },
        .consume_amt = { 0, 0, 0 },
        .tick_seconds = 7.0f,
        .cost = { [RES_WOOD] = 10, [RES_GOLD] = 110 },
        .hud_placeable = 1
    },
    [BUILDING_CIGAR_HOUSE] = {
        .name = "Cigar House",
        .category = BCAT_LUXURY,
        .tile_w = 2, .tile_h = 2,
        .placement_flags = PLACE_ANY_LAND,
        .col_r = 120, .col_g = 90, .col_b = 60,
        .produces = RES_CIGARS, .produce_amt = 1,
        .consumes = { RES_TOBACCO, RES_PLANKS, RES_COUNT },
        .consume_amt = { 1, 1, 0 },
        .tick_seconds = 14.0f,
        .cost = { [RES_BRICKS] = 10, [RES_GOLD] = 280 },
        .hud_placeable = 1
    },
    [BUILDING_COCOA_GROVE] = {
        .name = "Cocoa Grove",
        .category = BCAT_FARMING,
        .tile_w = 2, .tile_h = 2,
        .placement_flags = PLACE_ANY_LAND,
        .needs_fertility = FERTILE_COCOA,
        .col_r = 110, .col_g = 80, .col_b = 60,
        .produces = RES_COCOA, .produce_amt = 1,
        .consumes = { RES_COUNT, RES_COUNT, RES_COUNT },
        .consume_amt = { 0, 0, 0 },
        .tick_seconds = 8.0f,
        .cost = { [RES_WOOD] = 10, [RES_GOLD] = 110 },
        .hud_placeable = 1
    },
    [BUILDING_CHOCOLATE_HOUSE] = {
        .name = "Chocolate House",
        .category = BCAT_LUXURY,
        .tile_w = 2, .tile_h = 2,
        .placement_flags = PLACE_ANY_LAND,
        .col_r = 95, .col_g = 60, .col_b = 45,
        .produces = RES_CHOCOLATE, .produce_amt = 1,
        .consumes = { RES_COCOA, RES_SUGAR, RES_COUNT },
        .consume_amt = { 1, 1, 0 },
        .tick_seconds = 14.0f,
        .cost = { [RES_BRICKS] = 10, [RES_GOLD] = 280 },
        .hud_placeable = 1
    },
    [BUILDING_PEARL_BEDS] = {
        .name = "Pearl Beds",
        .category = BCAT_EXTRACTION,
        .tile_w = 1, .tile_h = 1,
        .placement_flags = PLACE_NEEDS_COAST,
        .needs_adjacent_deposit = DEPOSIT_PEARLS,
        .col_r = 225, .col_g = 225, .col_b = 235,
        .produces = RES_PEARLS, .produce_amt = 1,
        .consumes = { RES_COUNT, RES_COUNT, RES_COUNT },
        .consume_amt = { 0, 0, 0 },
        .tick_seconds = 14.0f,
        .cost = { [RES_PLANKS] = 10, [RES_GOLD] = 240 },
        .hud_placeable = 1
    },
    [BUILDING_JEWELLER] = {
        .name = "Jeweller",
        .category = BCAT_LUXURY,
        .tile_w = 2, .tile_h = 2,
        .placement_flags = PLACE_ANY_LAND,
        .col_r = 235, .col_g = 205, .col_b = 110,
        .produces = RES_JEWELLERY, .produce_amt = 1,
        .consumes = { RES_GOLD_ORE, RES_PEARLS, RES_COUNT },
        .consume_amt = { 1, 1, 0 },
        .tick_seconds = 20.0f,
        .cost = { [RES_STEEL_BEAMS] = 10, [RES_GOLD] = 420 },
        .hud_placeable = 1
    },
    [BUILDING_FLOWER_FIELD] = {
        .name = "Flower Field",
        .category = BCAT_FARMING,
        .tile_w = 2, .tile_h = 2,
        .placement_flags = PLACE_ANY_LAND,
        .needs_fertility = FERTILE_FLOWERS,
        .col_r = 215, .col_g = 140, .col_b = 190,
        .produces = RES_FLOWERS, .produce_amt = 1,
        .consumes = { RES_COUNT, RES_COUNT, RES_COUNT },
        .consume_amt = { 0, 0, 0 },
        .tick_seconds = 7.0f,
        .cost = { [RES_WOOD] = 10, [RES_GOLD] = 100 },
        .hud_placeable = 1
    },
    [BUILDING_PERFUMERY] = {
        .name = "Perfumery",
        .category = BCAT_LUXURY,
        .tile_w = 2, .tile_h = 2,
        .placement_flags = PLACE_ANY_LAND,
        .col_r = 230, .col_g = 180, .col_b = 210,
        .produces = RES_PERFUME, .produce_amt = 1,
        .consumes = { RES_FLOWERS, RES_MARSH_GIN, RES_COUNT },
        .consume_amt = { 1, 1, 0 },
        .tick_seconds = 16.0f,
        .cost = { [RES_BRICKS] = 12, [RES_GOLD] = 340 },
        .hud_placeable = 1
    },
    [BUILDING_HOUSE_MERCHANT] = {
        .name = "Merchant House",
        .category = BCAT_HOUSING,
        .tile_w = 1, .tile_h = 1,
        .placement_flags = PLACE_ANY_LAND,
        .col_r = 215, .col_g = 200, .col_b = 155,
        .produces = RES_COUNT, .produce_amt = 0,
        .consumes = { RES_COUNT, RES_COUNT, RES_COUNT },
        .consume_amt = { 0, 0, 0 },
        .tick_seconds = 0.0f,
        .cost = { [RES_BRICKS] = 12, [RES_GOLD] = 320 },
        .hud_placeable = 1
    },
    [BUILDING_HOUSE_INVESTOR] = {
        .name = "Investor's House",
        .category = BCAT_HOUSING,
        .tile_w = 1, .tile_h = 1,
        .placement_flags = PLACE_ANY_LAND,
        .col_r = 230, .col_g = 215, .col_b = 235,
        .produces = RES_COUNT, .produce_amt = 0,
        .consumes = { RES_COUNT, RES_COUNT, RES_COUNT },
        .consume_amt = { 0, 0, 0 },
        .tick_seconds = 0.0f,
        .cost = { [RES_STEEL_BEAMS] = 10, [RES_GOLD] = 500 },
        .hud_placeable = 0
    },

    /* ---- SUPPLY_CHAIN Phase 8: the Academy and Scholars ---- */
    [BUILDING_INK_WORKS] = {
        .name = "Ink Works",
        .category = BCAT_REFINERY,
        .tile_w = 2, .tile_h = 2,
        .placement_flags = PLACE_ANY_LAND,
        .col_r = 60, .col_g = 55, .col_b = 75,
        .produces = RES_INK, .produce_amt = 1,
        .consumes = { RES_SHELLAC, RES_COUNT, RES_COUNT },
        .consume_amt = { 1, 0, 0 },
        .tick_seconds = 9.0f,
        .cost = { [RES_BRICKS] = 8, [RES_GOLD] = 170 },
        .hud_placeable = 1
    },
    [BUILDING_PAPER_MILL] = {
        .name = "Paper Mill",
        .category = BCAT_REFINERY,
        .tile_w = 2, .tile_h = 2,
        .placement_flags = PLACE_ANY_LAND,
        .col_r = 235, .col_g = 232, .col_b = 220,
        .produces = RES_PAPER, .produce_amt = 1,
        .consumes = { RES_WOOD, RES_COUNT, RES_COUNT },
        .consume_amt = { 1, 0, 0 },
        .tick_seconds = 8.0f,
        .cost = { [RES_PLANKS] = 10, [RES_GOLD] = 160 },
        .hud_placeable = 1
    },
    [BUILDING_BINDERY] = {
        .name = "Bindery",
        .category = BCAT_LUXURY,
        .tile_w = 2, .tile_h = 2,
        .placement_flags = PLACE_ANY_LAND,
        .col_r = 140, .col_g = 70, .col_b = 60,
        .produces = RES_BOOKS, .produce_amt = 1,
        .consumes = { RES_INK, RES_PAPER, RES_COUNT },
        .consume_amt = { 1, 1, 0 },
        .tick_seconds = 15.0f,
        .cost = { [RES_STEEL_BEAMS] = 6, [RES_GOLD] = 300 },
        .hud_placeable = 1
    },
    [BUILDING_CHART_HOUSE] = {
        .name = "Chart House",
        .category = BCAT_LUXURY,
        .tile_w = 2, .tile_h = 2,
        .placement_flags = PLACE_ANY_LAND,
        .col_r = 175, .col_g = 195, .col_b = 200,
        .produces = RES_CHARTS, .produce_amt = 1,
        .consumes = { RES_PAPER, RES_GLASS, RES_COUNT },
        .consume_amt = { 1, 1, 0 },
        .tick_seconds = 14.0f,
        .cost = { [RES_STEEL_BEAMS] = 6, [RES_GOLD] = 290 },
        .hud_placeable = 1
    },
    /* Produces nothing. Its whole output is that, while it stands and
     * is road-connected, every house on the island may take a second
     * road upward -- which is why it is INFRASTRUCTURE rather than a
     * workshop, and why demolishing one demotes nobody. */
    [BUILDING_ACADEMY] = {
        .name = "Academy",
        .category = BCAT_INFRASTRUCTURE,
        .tile_w = 2, .tile_h = 2,
        .placement_flags = PLACE_ANY_LAND,
        .col_r = 200, .col_g = 200, .col_b = 225,
        .produces = RES_COUNT, .produce_amt = 0,
        .consumes = { RES_COUNT, RES_COUNT, RES_COUNT },
        .consume_amt = { 0, 0, 0 },
        .tick_seconds = 0.0f,
        .cost = { [RES_BRICKS] = 20, [RES_STEEL_BEAMS] = 10, [RES_GOLD] = 600 },
        .hud_placeable = 1
    },
    [BUILDING_HOUSE_SCHOLAR] = {
        .name = "Scholar's House",
        .category = BCAT_HOUSING,
        .tile_w = 1, .tile_h = 1,
        .placement_flags = PLACE_ANY_LAND,
        .col_r = 195, .col_g = 205, .col_b = 230,
        .produces = RES_COUNT, .produce_amt = 0,
        .consumes = { RES_COUNT, RES_COUNT, RES_COUNT },
        .consume_amt = { 0, 0, 0 },
        .tick_seconds = 0.0f,
        .cost = { [RES_BRICKS] = 12, [RES_GOLD] = 450 },
        .hud_placeable = 0
    },
};

const char *building_category_name(BuildingCategory c)
{
    /* Designated, like everything else indexed by an enum here. */
    static const char *const NAMES[BCAT_COUNT] = {
        [BCAT_NONE]           = "Other",
        [BCAT_FARMING]        = "Farming",
        [BCAT_EXTRACTION]     = "Extraction",
        [BCAT_WORKSHOP]       = "Workshops",
        [BCAT_FACTORY]        = "Factories",
        [BCAT_REFINERY]       = "Refineries",
        [BCAT_LUXURY]         = "Luxuries",
        [BCAT_HOUSING]        = "Housing",
        [BCAT_INFRASTRUCTURE] = "Infrastructure",
        [BCAT_MARITIME]       = "Maritime"
    };
    if (c < 0 || c >= BCAT_COUNT || !NAMES[c]) return "Other";
    return NAMES[c];
}

/* ---- how many people a workplace holds (LIFE_PLAN Phase 1) ----
 * See building.h for why this is derived from the category rather than
 * written into ninety def rows.
 *
 * The numbers are a feel decision and NOTHING depends on them
 * economically: production scales linearly with headcount, so output per
 * worker is unchanged and tests/test_closure.c's workers-per-resident
 * ratio does not move whatever is written here. What they decide is how
 * many BUILDINGS an island needs for a given population — land and
 * capital, not labour. They are cheap to retune for that reason.
 *
 * A gang digs; a bench does not. The order below is the crew you would
 * expect to find on the site, and it follows the category comments in
 * building.h rather than being invented alongside them. */
int building_worker_cap(const BuildingDef *def)
{
    static const int CREW[BCAT_COUNT] = {
        [BCAT_FARMING]    = 5,   /* boat crews, farmhands, herds        */
        [BCAT_EXTRACTION] = 5,   /* a gang at a face or a stand of trees */
        [BCAT_WORKSHOP]   = 3,   /* "one artisan's worth of processing" */
        [BCAT_FACTORY]    = 6,   /* heavy industry, and the largest     */
        [BCAT_REFINERY]   = 4,   /* bulk stock, tended rather than made */
        [BCAT_LUXURY]     = 3    /* a finished thing, by few hands      */
    };

    /* Not a producer: the Warehouse, the Road, the Harbor. They employ
     * nobody today and this phase does not change that. */
    if (!def || def->tick_seconds <= 0.0f) return 0;

    if (def->category < 0 || def->category >= BCAT_COUNT
        || CREW[def->category] <= 0)
        return 1;    /* fail SAFE: today's behaviour, not a dead building */

    return CREW[def->category];
}

/* ---- what a full crew is worth (LIFE_PLAN Phase 2) ----------
 * See building.h for the overhead story the formula tells.
 *
 * Integer, and it has to be: this advances Building.timer, which is
 * hashed world state. A multiplier expressed as a rate per tick needs
 * no division and no float, so two machines cannot round it apart.
 *
 * Clamped to the crew size rather than trusted. worker_count is
 * retallied every tick from whoever is physically present, and a
 * demolition mid-reassignment is exactly the sort of thing that could
 * briefly put one more body in a building than it holds — which would
 * otherwise be a free production bonus for knocking a workplace down. */
int building_work_advance(const BuildingDef *def, int workers)
{
    int cap = building_worker_cap(def);

    if (cap <= 0 || workers <= 0) return 0;
    if (workers > cap) workers = cap;

    return 2 * workers - 1;
}

/* =========================================================
 * Helper: is tile (r,c) occupied by any placed building?
 * We check every active building's footprint.
 * ========================================================= */
static int tile_is_occupied(const Building buildings[], int count,
                             int r, int c)
{
    int i, br, bc;
    for (i = 0; i < count; i++) {
        const Building    *b   = &buildings[i];
        const BuildingDef *def = &BUILDING_DEFS[b->type];
        if (!b->active) continue;
        for (br = b->row; br < b->row + def->tile_h; br++)
            for (bc = b->col; bc < b->col + def->tile_w; bc++)
                if (br == r && bc == c) return 1;
    }
    return 0;
}

/* =========================================================
 * Helper: does the footprint have an adjacent tile carrying `dep`?
 * Same cardinal-neighbour sweep as footprint_has_adjacent below, but
 * asking about what is IN the tile rather than what it is — for a
 * building that works a deposit it cannot stand on (pearl beds in
 * shallow water). Kept separate rather than generalised into one
 * predicate-taking sweep: two four-line loops read better than one
 * loop plus a callback, at this size.
 * ========================================================= */
static int footprint_has_adjacent_deposit(const Map *map,
                                          int row, int col,
                                          int fw, int fh,
                                          uint8_t dep)
{
    static const int dr[4] = { -1, 1,  0, 0 };
    static const int dc[4] = {  0, 0,  1,-1 };

    int r, c, d;
    for (r = row; r < row + fh; r++)
        for (c = col; c < col + fw; c++)
            for (d = 0; d < 4; d++) {
                const Tile *nb = map_get_tile(
                    (Map *)map, r + dr[d], c + dc[d]);
                if (nb && nb->deposit == dep) return 1;
            }
    return 0;
}

/* =========================================================
 * Helper: does the footprint have an adjacent tile of type t?
 * Checks all four cardinal neighbours of every footprint tile.
 * ========================================================= */
static int footprint_has_adjacent(const Map *map,
                                  int row, int col,
                                  int fw, int fh,
                                  TileType t)
{
    /* Cardinal direction offsets: N, S, E, W */
    static const int dr[4] = { -1, 1,  0, 0 };
    static const int dc[4] = {  0, 0,  1,-1 };

    int r, c, d;
    for (r = row; r < row + fh; r++) {
        for (c = col; c < col + fw; c++) {
            for (d = 0; d < 4; d++) {
                const Tile *nb = map_get_tile(
                    (Map *)map, r + dr[d], c + dc[d]);
                if (nb && nb->type == t) return 1;
            }
        }
    }
    return 0;
}

/* =========================================================
 * building_can_place
 * ========================================================= */
RejectReason building_place_check(const Map *map,
                                  BuildingType type,
                                  int row, int col)
{
    return building_place_check_def(map, &BUILDING_DEFS[type], row, col);
}

RejectReason building_place_check_def(const Map *map,
                                      const BuildingDef *def,
                                      int row, int col)
{
    int r, c;

    /* --- Pass 1: bounds --------------------------------- */
    if (row < 0 || col < 0 ||
        row + def->tile_h > map->rows ||
        col + def->tile_w > map->cols) {
        return REJ_OUT_OF_BOUNDS;
    }

    /* --- Pass 2: per-tile checks ------------------------ */
    for (r = row; r < row + def->tile_h; r++) {
        for (c = col; c < col + def->tile_w; c++) {
            const Tile *t = map_get_tile((Map *)map, r, c);

            if (!t || !t->buildable) {
                return REJ_NOT_BUILDABLE;
            }

            /* Fertility check for farms: does this soil grow anything? */
            if (def->placement_flags & PLACE_NEEDS_FERTILE) {
                if (t->fertility == FERTILE_NONE) {
                    return REJ_NEEDS_FERTILE;
                }
            }

            /* ...and does it grow THIS? Separate from the loose check
             * above so Farm's "any fertile ground" behaviour is
             * untouched by a def naming a crop. */
            if (def->needs_fertility &&
                (t->fertility & def->needs_fertility) != def->needs_fertility) {
                return REJ_NEEDS_CROP;
            }

            if (def->needs_deposit != DEPOSIT_NONE &&
                t->deposit != def->needs_deposit) {
                return REJ_NEEDS_DEPOSIT;
            }

            /* Occupied check — pass NULL for buildings when
             * called from the hover ghost (no buildings ptr).
             * We handle that by checking count == 0. */
        }
    }

    /* --- Pass 3: adjacency ------------------------------ */
    if (def->placement_flags & PLACE_NEEDS_COAST) {
        if (!footprint_has_adjacent(map, row, col,
                                    def->tile_w, def->tile_h,
                                    TILE_WATER)) {
            return REJ_NEEDS_COAST;
        }
    }

    if (def->placement_flags & PLACE_NEEDS_FOREST) {
        if (!footprint_has_adjacent(map, row, col,
                                    def->tile_w, def->tile_h,
                                    TILE_FOREST)) {
            return REJ_NEEDS_FOREST;
        }
    }

    /* The deposit you work from beside rather than stand on. Same
     * refusal as the under-footprint case: "nothing to work here" is
     * the same sentence either way, and the player knows which
     * building they are holding — the argument that collapsed fourteen
     * crops into one REJ_NEEDS_CROP. */
    if (def->needs_adjacent_deposit != DEPOSIT_NONE) {
        if (!footprint_has_adjacent_deposit(map, row, col,
                                            def->tile_w, def->tile_h,
                                            def->needs_adjacent_deposit)) {
            return REJ_NEEDS_DEPOSIT;
        }
    }

    return REJ_OK;   /* all checks passed */
}

int building_can_place(const Map *map, BuildingType type, int row, int col)
{
    return building_place_check(map, type, row, col) == REJ_OK;
}

/* =========================================================
 * building_place
 * ========================================================= */
int building_place(Building buildings[], int *count,
                   const Map *map,
                   BuildingType type, int row, int col)
{
    int i, slot;

    /* Check for tile overlap with existing buildings */
    {
        const BuildingDef *def = &BUILDING_DEFS[type];
        int r, c;
        for (r = row; r < row + def->tile_h; r++)
            for (c = col; c < col + def->tile_w; c++)
                if (tile_is_occupied(buildings, *count, r, c))
                    return -1;
    }

    if (!building_can_place(map, type, row, col))
        return -1;

    /* Reuse a demolished building's slot before appending — without
     * this, repeated build/destroy cycles on the same spot would
     * burn through MAX_BUILDINGS even though the live count stays
     * low. (The cap only applies when no free slot exists, so
     * reusing one works even with *count already at MAX_BUILDINGS.) */
    slot = -1;
    for (i = 0; i < *count; i++) {
        if (!buildings[i].active) { slot = i; break; }
    }
    if (slot < 0) {
        if (*count >= MAX_BUILDINGS) return -1;
        slot = (*count)++;
    }

    buildings[slot].type         = type;
    buildings[slot].row          = row;
    buildings[slot].col          = col;
    buildings[slot].active       = 1;
    buildings[slot].timer        = 0;   /* integer tick accumulator (1b) */
    buildings[slot].connected    = 0;
    buildings[slot].worker_count = 0;

    return slot;
}

/* =========================================================
 * building_can_afford
 * ========================================================= */
int building_can_afford(const Stockpile *s, BuildingType type)
{
    const BuildingDef *def = &BUILDING_DEFS[type];
    int i;

    for (i = 0; i < RES_COUNT; i++)
        if (s->amount[i] < def->cost[i])
            return 0;

    return 1;
}

/* =========================================================
 * building_gold_equivalent_cost
 * ========================================================= */
int building_gold_equivalent_cost(BuildingType type, const Faction *f)
{
    const BuildingDef *def = &BUILDING_DEFS[type];
    int i, total = def->cost[RES_GOLD];

    for (i = 0; i < RES_COUNT; i++)
        if (i != RES_GOLD)
            total += def->cost[i] * faction_ask(f, (ResourceType)i);

    return total;
}

/* ---- building_missing_input -------------------------------
 * All-or-nothing: a building only ticks when EVERY non-RES_COUNT slot
 * has enough stock, so nothing ever half-consumes one input while
 * short of another.
 *
 * Returns the first slot it cannot pay for, or -1 when it can run. A
 * slot index rather than a bool because the caller logs which input is
 * missing, and the alternative was that loop living in island.c where
 * a test could not reach it with a def of its own — the seam
 * building_place_check_def() opened for the same reason.
 */
int building_missing_input(const BuildingDef *def, const Stockpile *s)
{
    int j;
    for (j = 0; j < MAX_BUILDING_INPUTS; j++) {
        if (def->consumes[j] == RES_COUNT) continue;
        if (s->amount[def->consumes[j]] < def->consume_amt[j]) return j;
    }
    return -1;
}
