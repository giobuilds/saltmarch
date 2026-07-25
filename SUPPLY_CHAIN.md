# Saltmarch Supply Chain Plan — seven tiers, two climates

> Status: **planned, not started.** Written from the notes now kept as
> Appendix A, and structured to be executed phase by phase like
> MMO_PLAN.md and UI_PLAN.md — each phase independently shippable, each
> verified before the next begins.

## What this is

The game today has seven resources and one production chain worth the
name (Grain + Hops → Malt → Beer), feeding two population tiers. This
plan takes it to roughly **seventy goods, sixty buildings and seven
tiers across two climates** — the economy the notes describe.

Three things make that tractable rather than reckless:

1. **The needs system is already data-driven.** `TIER_DEFS` in
   population.c maps a house type to the goods it wants;
   `tier_def_for()` is the only lookup. Adding a tier is a row, not a
   branch.
2. **The def table is already designated and tested.** BUILDING_DEFS
   rows name their fields (UI_PLAN Phase 2), and tests/test_defs.c
   asserts every type has a name, a category and a sane footprint. A
   fifty-row table is the same shape as a fifteen-row one.
3. **The UI was built for this.** The exchange screen paginates and is
   tested at 40 goods; the HUD tabs are tested at 40 buildings; the
   stores overlay pages. UI_PLAN's capacity work was done *because* of
   this plan, and it is the reason this does not need a UI phase of its
   own.

What is genuinely missing is **terrain**. Every chain here starts at a
crop or a mineral, and the map has two crop bits and no minerals at
all. That is Phase 1, and it is the phase most likely to take longer
than it looks.

## Ground rules (read before every phase)

1. **Respect CLAUDE.md.** Subsystem pattern, no game logic in main.c,
   `sim_log` not `SDL_Log` in the sim, builders take snapshots.
2. **Warnings are bugs**, and build under **both GCC and clang** before
   pushing. They disagree about enum conversions, which has cost one
   red CI run already.
3. **Every mutation is a Command.** No new mechanic reaches world state
   any other way.
4. **New sim state is hashed state.** If a phase adds something the
   world can be in, `sim_hash` covers it and the F9 test proves it. A
   replay that reproduced everything except the new field would be a
   replay with a hole in it — see `faction_init`'s memset, which exists
   because that exact mistake was made in UI_PLAN M3.
5. **Content changes break old logs, and that is fine.** Inserting a
   resource shifts every index after it, so a `.smlog` recorded before
   the change replays as different commands. Every content phase
   therefore **bumps `SAVE_VERSION`**, and the determinism fixture's
   hash changes. Say so in the commit; do not try to preserve it.
6. **One phase per PR**, green before the next starts.

## Naming

The notes use Anno 1800's vocabulary. Commit `6837892` deliberately
renamed this game and dropped those references, so the chains are kept
and the distinctive names are replaced. Generic goods — bread, beer,
soap, bricks, coffee, glass, iron — keep their names, because they are
nobody's invention.

| Notes | Saltmarch |
|---|---|
| Farmers / Workers | **Marshfolk / Wrights** |
| Artisans / Engineers / Investors / Scholars | unchanged |
| (new base tier) | **Merchants** |
| Work Clothes ← Framework Knitters | **Oilskins** ← **Knitting House** |
| Schnapps ← Distillery | **Marsh Gin** ← **Still** |
| Sausages ← Slaughterhouse | Sausages ← **Butchery** |
| Soap ← Rendering Works | Soap ← **Tallow Works** + **Soap Boilery** |
| Steel Beams ← Furnace/Steelworks | **Steel Beams** ← **Bloomery** + **Ironworks** |
| Ponchos ← Poncho Darner | **Wool Cloaks** ← **Darning House** |
| Tortillas ← Tortilla Maker | **Flatbread** ← **Flatbread Kitchen** |
| Bowler Hats ← Felt/Weaver | **Marsh Hats** ← **Felt Works** + **Hatter** |
| Canned Food ← Cannery | **Preserves** ← **Cannery** |
| Light Bulbs ← Light Bulb Factory | **Lamps** ← **Lamp Works** |
| Pocket Watches ← Watchmakers | Pocket Watches ← **Watchmaker's** |
| Phonographs ← Phonograph Factory | **Gramophones** ← **Gramophone Works** |
| Champagne ← Champagne Cellar | **Sparkling Wine** ← **Sparkling Cellar** |
| High-end food | **Banquet** ← **Fine Kitchen** (Lobster + Preserves) |
| Fried Plantains | **Plantain Fry** ← **Fry Kitchen** |

One honest gap: the notes list **Scholars** as a tier but give it no
needs, and the Merchants → Investors split asks for a needs list the
notes never had to write. Both are proposed below and marked as
proposals; everything else is translation, and the original is in
Appendix A.

## The chains

Northern islands (temperate, highland, woodland, atoll — today's four):

| Good | Chain |
|---|---|
| Timber | Lumberjack (exists) |
| Planks | Timber → **Sawmill** |
| Fish | Fisher's Hut (exists) |
| Grain | Farm (exists) |
| Flour | Grain → **Windmill** |
| Bread | Flour → **Bakehouse** |
| Oilskins | **Sheep Pasture** → **Knitting House** |
| Marsh Gin | **Potato Field** → **Still** |
| Bricks | **Clay Pit** → **Brickworks** |
| Sausages | **Pig Pen** → **Butchery** |
| Tallow | Pig Pen → **Tallow Works** |
| Soap | Tallow → **Soap Boilery** |
| Beer | Grain + Hops → Malthouse → Brewery (exists) |
| Charcoal | Timber → **Charcoal Kiln** |
| Iron | **Iron Mine** + Charcoal → **Bloomery** |
| Steel Beams | Iron → **Ironworks** |
| Glass | **Sand Pit** → **Glassworks** |
| Brass | Iron + Charcoal → **Brass Foundry** |
| Windows | Glass + Planks → **Window Shop** |
| Spectacles | Glass + Brass → **Spectacle Shop** |
| Pelts | **Trapper's Lodge** |
| Cloth | Cotton → **Spinning Mill** |
| Fur Coats | Pelts + Cloth → **Furrier** |
| Preserves | **Cattle Pen** + **Pepper Field** → **Kitchen** → **Cannery** |
| Sewing Machines | Iron + Coal (**Coal Mine**) → **Foundry** → **Machine Shop** |
| Wire | Iron → **Wire Mill** |
| Springs | Iron → **Spring Works** |
| Lamps | Glass + Wire → **Lamp Works** |
| Pocket Watches | **Gold Ore** + Glass + Springs → **Watchmaker's** |
| Gramophones | Planks + Brass + Shellac → **Gramophone Works** |
| Lobster | **Lobster Pots** (coast) |
| Banquet | Lobster + Preserves → **Fine Kitchen** |
| Sparkling Wine | **Vineyard** → **Sparkling Cellar** |
| Jewellery | Gold Ore + **Pearls** → **Jeweller** |
| Perfume | **Flower Field** + Marsh Gin → **Perfumery** |

Southern islands (Phase 5's two new profiles):

| Good | Chain |
|---|---|
| Cotton | **Cotton Field** |
| Sails | Cloth → **Sail Loft** |
| Sugar | **Cane Field** → **Sugar Refinery** |
| Rum | Sugar + Planks → **Rum House** |
| Chocolate | **Cocoa Grove** + Sugar → **Chocolate House** |
| Coffee | **Coffee Grove** → **Roastery** |
| Cigars | **Tobacco Field** + Planks → **Cigar House** |
| Wool Cloaks | **Alpaca Pasture** → **Darning House** |
| Flatbread | **Maize Field** + Cattle → **Flatbread Kitchen** |
| Plantain Fry | **Plantain Grove** + **Fish Oil Rendery** → **Fry Kitchen** |
| Felt | Cotton + Alpaca wool → **Felt Works** |
| Marsh Hats | Felt → **Hatter** |
| Shellac | **Lac Grove** |

## Tiers: three houses, six tiers, one side door

Not one house walking up six steps. **Three house types, each
upgradeable once**, plus a building that opens a seventh path:

| House | Base tier | Upgrades to |
|---|---|---|
| Marsh Cottage | **Marshfolk** | **Artisans** |
| Wright's House | **Wrights** | **Engineers** |
| Merchant House | **Merchants** | **Investors** |
| *(any of the three)* | — | **Scholars**, via the Academy |

| Tier | Needs |
|---|---|
| Marshfolk | Fish, Oilskins, Marsh Gin |
| Artisans | Preserves, Sewing Machines, Fur Coats, Spectacles, Windows |
| Wrights | Sausages, Bread, Soap, Beer |
| Engineers | Lamps, Pocket Watches, Gramophones, Banquet |
| Merchants | Coffee, Rum, Flatbread, Marsh Hats |
| Investors | Sparkling Wine, Cigars, Chocolate, Jewellery, Perfume |
| Scholars | Books, Charts, Coffee, Spectacles |

Five goods is the widest list, which sets `MAX_TIER_GOODS`.

**Two of those rows are invented, and should be read as proposals.**
The notes give needs for Farmers, Workers, Artisans, Engineers and
Investors, and name Scholars without them. Splitting Investors into a
Merchants → Investors line means Merchants needs a list of its own:
the colonial goods are the obvious fit, since they arrive by ship and
give the southern islands a customer. Scholars is invented outright
(Phase 8). Redirect either freely.

### Upgrading

**A house upgrades when the next tier's needs are on the island**, not
when a number is reached. The requirement is that every good the next
tier wants is in the island's stockpile at the moment of the upgrade,
plus the tier's Gold cost — so the way to promote a neighbourhood is to
build the chains that will keep feeding it, which is the same lesson
the game already teaches with production inputs.

Clicking a house opens the existing confirm popup showing that
checklist — one row per required good, present or missing, in the
`ui_reject_text` vocabulary when it is missing. `sim_apply` enforces
exactly the same rule, so the checklist and the verdict cannot drift
(UI_PLAN decision 3: prediction and authority are the same function).

**The three house types are all placeable from the start**, at
escalating cost: a Wright's House is dearer than a Marsh Cottage and a
Merchant House dearer again. The gate is the economy rather than a tech
tree — you *can* put down a Merchant House on turn one, and it will sit
unhappy until Coffee and Rum reach the island. If you would rather they
unlocked on progression, that is a one-line change to the def table's
`hud_placeable` and a rule in the HUD builder; say so and it moves.

### The Academy

A building, not a house. **While an island has an active, road-connected
Academy, any house on it may be upgraded to Scholars** — from any of the
six tiers, skipping whatever line it was on. Scholars have their own
needs like every other tier.

Two consequences worth stating: the Academy is a *prerequisite*, not an
action (it does not convert houses by itself, and demolishing it does
not demote anyone), and it makes Scholars reachable from Marshfolk
directly, which is deliberate — a scholar's household need not have
been a merchant's first.

---

## Phase 1 — the terrain the chains need — **DONE**

**Goal:** the map can say "there is clay here" and "this soil grows
potatoes". No new goods yet.

- `Tile.fertility` becomes a wider bitmask with one bit per crop
  (grain, hops, potato, cotton, cane, cocoa, coffee, tobacco, maize,
  grapes, flowers, plantain, lac, pasture). It is already `unsigned`
  and unsaved — the map regenerates from its seed — so widening it
  costs nothing on disk.
- **New:** `Tile.deposit`, a small enum (`DEPOSIT_NONE`, `IRON`,
  `COAL`, `CLAY`, `SAND`, `GOLD_ORE`, `PEARLS`). A tile has at most one
  mineral, so an enum rather than a bitmask.
- `BuildingDef` gains `uint32_t needs_fertility` and `uint8_t
  needs_deposit`, and **`PLACE_NEEDS_HOP_FERTILE` is deleted**. That
  flag exists only because there was no way to say "this crop
  specifically"; with a field there is. `PLACE_NEEDS_FERTILE` stays for
  "any fertile soil".
- `building_place_check()` consults both, with new reasons
  `REJ_NEEDS_CROP` and `REJ_NEEDS_DEPOSIT` — so the hover tooltip and
  the flash channel explain a failed placement in the same vocabulary
  as everything else (UI_PLAN decision 3).
- `PROFILE_PARAMS` (map.c) gains per-profile crop weights and deposit
  counts. Deposits scatter deterministically from the island seed.

**Verify:** Farm and Hop Farm behave exactly as before (their rules
move to the new fields, same result); a test asserts each profile
yields the crops and deposits its intended chains need, and that a
deposit-requiring building is refused with `REJ_NEEDS_DEPOSIT` on bare
grass. Fixture hash changes once, deliberately: tile metadata is
hashed through building placement validity.

### As built

`tests/test_terrain.c`, 34 assertions across four profiles and five
seeds. Four things came out differently from the plan above.

**The fixture hash did not change.** The plan expected it to, and it
would have if the new passes had drawn from `lcg_next()` — every
island's shape depends on that draw order, so consuming from it would
have silently reshaped every map in every existing save. Instead crops
and deposits hash `(seed, coordinates)` and run *after* the heightmap
loop: inserted rather than interleaved. Terrain is byte-identical,
`42affc4b13c29881` still holds, and old logs replay.

**`building_place_check_def()`**, taking a def instead of a table row,
with `building_place_check()` as a one-line wrapper over it. Phase 1
ships no building that wants a deposit — that is the point of the
phase — so without this seam the deposit rule could not have been
proven until something wanted it. The test writes its own defs. Worth
keeping as the table grows past sixty rows.

**Pasture is not exclusive with the other crops.** Every grass tile
carries `FERTILE_PASTURE` on top of its grain-or-hop bit, so grazing
and arable compete for the same ground. Secondary crops come one per
8×8 patch rather than one per tile, so a district can be planned around
them; there is an assertion that they are patches and not speckle, and
it fails if the patch size goes to 1.

**Deposits are drawn**, as a 40%-scale diamond in the tile centre
(`DEPOSIT_COLOURS`, render.c), and hovering one names it in a small box
just above — `render_deposit_label()`, anchored to the *tile* rather
than the cursor, because the question is what is under that ground
rather than where the pointer is. Placement goes through
`ui_tooltip_rect()` like the HUD's tooltip, so a seam near the top or
edge of the window is not drawn off it. Not in the plan, but Phase 1
would otherwise ship nothing a human could see, and the terrain it adds
is exactly the kind that wants checking by eye. Never seen rendered.

**Each mineral has its own ground, and the beach belongs to sand.**
Clay is inland low ground only; it was originally allowed on beach
too, which had it competing for the one terrain sand has. Deposits
also scatter most-constrained-first rather than in enum order, so a
loose rule cannot eat the tiles a strict one needs. Asserted over four
profiles and five seeds.

**Pearl beds lie in shallow water**, and are the one deposit nothing
can be built on. They were briefly put on the beach, which made them
the only marker on the map that did not describe a real place. Water
is not buildable and roads cannot cross it, so rather than teach
connectivity about offshore buildings, `BuildingDef` gained
`needs_adjacent_deposit`: the Pearl Beds station stands on the shore
and works the bed alongside it. Exactly the shape of
`PLACE_NEEDS_COAST`, which is the Fisher's Hut standing on land next
to the sea it fishes. `REJ_NEEDS_DEPOSIT` covers both the under-foot
and the alongside case — one sentence, since the player knows which
building they are holding.

## Phase 2 — the structural limits

**Goal:** the ceilings the content will hit, raised before it hits
them. No new content.

- `MAX_BUILDING_INPUTS` 2 → **3** (Watchmaker's needs Gold Ore + Glass
  + Springs; Gramophone Works needs Planks + Brass + Shellac).
- `MAX_TIER_GOODS` 3 → **5** (Artisans and Investors both list five).
- `EXCHANGE_MAX_ROWS` and `INVENTORY_MAX_ROWS` 64 → **96**.
- **The tier model becomes a graph, not a ladder.** `TierDef` gains
  `next_tier`, `upgrade_gold` and `requires_building` (the Academy's
  slot); `game_upgrade_house` follows the edge from whatever tier the
  house is on rather than hardcoding House → Worker's House. Three
  lines and one side door are four edges in a table, not four branches
  in code.
- **Upgrading checks the next tier's needs.** A new
  `sim_can_upgrade(gs, island, building_idx, RejectReason *why)` —
  shared, per UI_PLAN decision 3, between the confirm popup's checklist
  and `sim_apply`'s verdict. New reasons: `REJ_NEEDS_GOODS` and
  `REJ_NEEDS_BUILDING` (no Academy).
- The confirm popup renders the checklist. It already draws a list of
  rows from a value struct and shows the literal command it will
  submit, so this is a `ConfirmView` with a needs list rather than a
  new overlay.
- **HUD categories widened** so no tab overflows: `BCAT_FARMING`,
  `BCAT_EXTRACTION`, `BCAT_WORKSHOP`, `BCAT_FACTORY`, `BCAT_HOUSING`,
  `BCAT_INFRASTRUCTURE`, `BCAT_MARITIME`. At ~21 slots per tab and ~60
  buildings, seven categories leave room.

**Verify:** existing game unchanged (fixture hash **stable** — this
phase adds no state); synthetic tests for a 3-input building and a
5-need tier; test_hud's 40-building case re-run against the new
category set.

## Phase 3 — the two northern base tiers

**Goal:** the first genuinely deep economy on the northern islands.

Sawmill/Planks, Sheep Pasture → Knitting House → Oilskins, Potato Field
→ Still → Marsh Gin, Clay Pit → Brickworks → Bricks, Pig Pen →
Butchery → Sausages, Pig Pen → Tallow Works → Soap Boilery → Soap,
Windmill → Bakehouse → Bread.

Both northern **base** tiers land here — Marsh Cottage (Marshfolk) and
Wright's House (Wrights) — as separate placeable buildings. Neither
upgrades yet: their upgrade targets are Artisans (Phase 4) and
Engineers (Phase 6), so until then the confirm popup correctly reports
that there is nowhere to go.

These are today's `BUILDING_HOUSE` and `BUILDING_HOUSE_WORKER` renamed
and re-rooted, not new slots — but note the change in kind: **Worker's
House stops being something you upgrade *into* and becomes something
you build.** That is the visible cost of the three-line model, and the
existing House → Worker's House upgrade path goes away with it. If
you would rather keep a ladder between the two northern lines as well,
it is one extra edge in the tier table — but it would mean a Marsh
Cottage has two possible futures, and the confirm popup would need to
ask which.

**Verify:** a test that a Wrights house can actually be *satisfied* —
every good in its needs list is producible from what a
`PROFILE_TEMPERATE` island plus one neighbour can grow. That assertion
is the one that catches a chain specified but not reachable, which is
the failure this plan is most likely to produce.

## Phase 4 — iron, glass, and the Artisans

Iron Mine, Coal Mine, Charcoal Kiln, Bloomery, Ironworks, Sand Pit,
Glassworks, Brass Foundry, Window Shop, Spectacle Shop, Trapper's
Lodge, Furrier, Cattle Pen, Pepper Field, Kitchen, Cannery, Foundry,
Machine Shop.

**Artisans** arrives as the upgrade of a Marsh Cottage — the first
working example of the whole upgrade rule, and the phase where the
checklist popup earns its keep.

**Verify:** as Phase 3, for Artisans. Plus the first three-input
building in real content (none of these need three yet — the assertion
is that Phase 2's limit is exercised by tests until Phase 6 uses it).

## Phase 5 — the southern islands

**Goal:** a second climate, and a reason for the shipping lanes the
game already models.

- `MAX_ISLANDS` 4 → **8**. This touches more than it looks:
  `SAVE_VERSION` (a v8 log means a four-island world), `NET_PROTO_VERSION`
  (a four-island client and an eight-island server disagree about the
  world), `MAX_ISLANDS_FOR_LANES` in faction.h, `NODE_POS` in
  world_ui.c (four hardcoded map positions), and the snapshot's
  per-island arrays (~40 KB at eight islands — still nothing).
- Two new profiles: `PROFILE_PLANTATION` and `PROFILE_JUNGLE`, carrying
  the southern crops.
- The southern chains from the table above.

**Verify:** a southern island can be chartered, its goods shipped
north, and a Wrights house on the home island satisfied from them;
eight islands render on the world map without overlapping; the co-op
and server tests still pass at the new island count.

## Phase 6 — Engineers

Wire Mill, Spring Works, Lamp Works, **Watchmaker's (three inputs)**,
Gramophone Works, Lobster Pots, Fine Kitchen. **Engineers** arrives as
the upgrade of a Wright's House, completing the second line.

**Verify:** the three-input path in real content, and that Phase 2's
`MAX_BUILDING_INPUTS` did not need raising again.

## Phase 7 — Merchants and Investors

Vineyard → Sparkling Cellar, Pearl Beds (atoll deposit), Jeweller,
Flower Field → Perfumery, Cigar House, Chocolate House.

The third line, both halves: **Merchant House (Merchants)** as a
placeable building wanting the colonial goods Phase 5 made shippable,
upgrading to **Investors** on the luxuries this phase adds. This is the
phase where the southern islands stop being a novelty and become the
thing the top of the economy runs on.

## Phase 8 — the Academy and Scholars

The **Academy**: a building that, while active and road-connected, lets
any house on its island upgrade to **Scholars** regardless of which
line it was on.

Its goods are invented, since the notes name the tier and stop: **Ink**
(Lac Grove → **Ink Works**), **Paper** (Timber → **Paper Mill**),
**Books** (Ink + Paper → **Bindery**), **Charts** (Paper + Glass →
**Chart House**). Scholars need Books, Charts, Coffee and Spectacles.

**Verify:** a house of each of the six tiers can be upgraded to
Scholars with an Academy present and none without it — the
`REJ_NEEDS_BUILDING` path — and demolishing the Academy demotes
nobody.

---

## Risks

| Risk | Mitigation |
|---|---|
| A chain is specified but unreachable — no island grows one of its inputs | The per-tier "can this actually be satisfied?" test in Phases 3–7. This is the likeliest failure and the cheapest to catch. |
| Terrain generation makes a crop too rare, and a chain starves silently | `map_init` already validates a profile's minimum-resource contract and reseeds; extend it per crop rather than hoping. |
| A tier's needs are unreachable, so its houses can never be upgraded at all | The per-tier reachability test now doubles as an upgrade test: if the goods cannot be made, the upgrade cannot happen, and the assertion fails in the phase that added the tier rather than in play. |
| Balance is untestable automatically | It is. Tests prove reachability and determinism, not fun. Every content phase needs a human at the keyboard before it merges. |
| `RES_COUNT` growth quietly slows the per-tick loops | Production is O(buildings), needs are O(houses); neither scales with RES_COUNT. The stockpile does, and it is an int array. Measure at Phase 5 rather than assume. |
| Old `.smlog` files stop replaying at every content phase | Stated in ground rule 5: bump `SAVE_VERSION`, expect the fixture hash to change, say so in the commit. |
| Sixty buildings is a lot of HUD | Phase 2's seven categories, and test_hud's existing 40-building fit assertion raised to 60. |

## Non-goals

- No new *mechanics*. This plan adds goods, buildings, terrain and
  tiers. Trade, ownership, charters, insurance and PvP already exist
  and are not touched.
- No second currency, no scrip. MMO_PLAN's non-goals still stand.
- No sprites. Buildings stay flat-shaded diamonds coloured per def.
- No rebalancing of the existing Beer chain to fit the new one. It is
  the reference chain; if it needs tuning, that is its own commit.

---

## Appendix A — the source notes

Kept verbatim, because this plan is a reading of them and the reading
should stay checkable.

```
Farmers, Workers, aristocrats
Next tiers: Artisans, Engineers, Investors, Scholars

Timber: Lumberjack → Sawmill
Fish: Fishery
Work Clothes: Sheep Farm → Framework Knitters
Schnapps: Potato Farm → Distillery
Bricks: Clay Pit → Brick Factory
Sausages: Pig Farm → Slaughterhouse
Bread: Grain Farm → Mill → Bakery
Soap: Pig Farm → Rendering Works → Soap Factory
Beer: Grain + Hops → Malthouse → Brewery
Steel Beams: Iron Mine + Charcoal/Coal → Furnace → Steelworks
Timber: Lumberjack → Sawmill
Fried Plantains: Plantain Plantation + Fish Oil Factory → Kitchen
Sails: Cotton Plantation → Cotton Mill → Sailmakers
Rum: Sugar Cane → Rum Distillery + Lumber
Ponchos: Alpaca Farm → Poncho Darner
Tortillas: Corn Farm + Cattle Farm → Tortilla Maker
Coffee: Coffee Plantation → Coffee Roaster
Bowler Hats: Cotton + Alpaca → Felt + Weaver
Cigars: Tobacco + Lumber/Marquetry → Cigar Factory
Chocolate: Cocoa + Sugar Cane → Sugar Refinery → Chocolate Factory
Canned Food: Cattle + Red Pepper → Artisanal Kitchen → Cannery
Sewing Machines: Iron + Coal → Furnace → Sewing Machine Factory + Wood
Fur Coats: Cotton → Cotton Mill + Hunting Cabin → Fur Dealer
Glasses: Sand → Glassmakers + Brass → Spectacle Factory
Windows: Sand → Glassmakers → Window Makers + Wood
Light Bulbs: Glass + Filament → Light Bulb Factory
Pocket Watches: Gold + Glass + Springs → Watchmakers
Phonographs: Wood + Brass + Shellac → Phonograph Factory
High-end food: (e.g. Lobster / Fine meals from advanced kitchens)
Champagne: Grapes → Champagne Cellar
Jewellery: Gold + Pearls / Gems → Jewellers
Perfume: Flowers + Alcohol → Perfume Mixer

Needs by tier:
Farmers
Fish, Work Clothes, Schnapps
Workers
Sausages, Bread, Soap, Beer
Artisans
Canned Food, Sewing Machines, Fur Coats, Glasses, Windows
Engineers
Light Bulbs, Pocket Watches, Phonographs, High-end food
Investors
Champagne, Cigars, Chocolate, Jewellery, Perfume
```

## Appendix B — what "aristocrats" became

The notes' first line reads "Farmers, Workers, aristocrats" and the
second lists four more tiers. "Aristocrats" appears nowhere else — no
chain feeds it and no needs are given — so this plan reads it as an
early sketch of the top of the ladder, superseded by Investors.

The three-line house model makes that reading easy to revisit: a fourth
line (base → Aristocrats) is one more row in the tier table and one
more house in the def table, not a restructure. If aristocrats were
meant as a real seventh tier, say so and it becomes Phase 9.
