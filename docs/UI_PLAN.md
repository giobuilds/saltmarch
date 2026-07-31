# UI/UX Reorganisation Plan — v2, aligned with MMO_PLAN.md

> Status: **complete.** All numbered phases, all M-phases and all
> N-phases (N1–N8) have landed. What remains is in the "explicitly out of scope" list below,
> plus the deferrals noted per phase. Phases 0 (`ui_kit` + `UiSnapshot`), 0.5
> (RejectReason), 1 (exchange screen), 2 (data model), 3 (HUD tabs) and
> 4 (vitals, stores, overlay arbiter), 5 (island context) and 6 (confirm
> consolidation) have landed; everything below
> them is still as planned.
> Written for a future session to pick up cold.
>
> Note for later phases: MMO_PLAN Phases 1–6 are all shipped, so the
> M-phases are unblocked and some of their content has already arrived
> early. In particular the fixed price tables are already gone, so
> Phase 1's `exchange_view_fixed()` is moot — the exchange view reads the
> faction's live quotes from the snapshot (what the plan called
> `exchange_view_faction()` at M3).
>
> Supersedes the v1 plan (in git history), which was written before
> MMO_PLAN.md existed. The capacity measurements, the two verified bugs, and
> the no-layout-library decision carry over unchanged; the phase structure
> and several core decisions are re-cut around the MMO architecture.

## Why the redesign

v1 solved capacity cliffs for a single-player UI that reads live `GameState`
and calls mutators directly. MMO_PLAN.md changes three ground truths:

1. **Every mutation becomes a `Command` through `sim_apply()`.** UI buttons
   stop being callers and become command *emitters*. Click-to-effect gains
   real latency: a tick boundary now (100ms), an N-tick lockstep delay in
   co-op later. A UI that pretends actions are instantaneous will read as
   laggy and will hide rejected commands as eaten clicks.
2. **The sim becomes SDL-free, tick-driven and deterministic.** UI code that
   reads sim state mid-tick, mutates anything, or steps the RNG is a desync
   source — MMO_PLAN's risk register item #1 is "a mutation path escapes the
   funnel", and the UI is where those paths live.
3. **Fixed price tables die** (elastic faction market, Phase 3), and
   surfaces arrive that v1 never imagined: command rejection feedback, ghost
   ships from an untrusted feed, harbor-escrow offers, desync/staleness
   readouts.

What survives from v1 intact: the capacity math (trade screen is the
nearest cliff at 10 goods; dock bar at 22; resource panel at ~43), both
pre-existing bugs (overlay-blind mouse wheel; `building_can_place()`'s dead
`reason` string), pagination-not-scrolling, category tabs on the dock bar,
rule-driven vitals, and the phase-per-PR verification convention.

### The no-layout-library decision is *strengthened*

v1 rejected Clay/Nuklear because retained layout would break the headless
pure-function hit tests. The MMO plan raises the stakes on exactly that
property: a UI built from pure functions over plain structs is one that
*cannot* mutate sim state, can be driven by the `.smlog` replay harness in
CI, and can render a past tick or a remote server's state without knowing
the difference. Purity stops being a testing convenience and becomes part
of the determinism doctrine. Clay remains a documented fallback under the
same conditions as v1.

---

## The five load-bearing decisions

### 1. UI is a pure function of a snapshot

New SDL-free header `src/ui_snapshot.h`: a `UiSnapshot` of plain structs,
copied once per frame **after** the tick-accumulator loop — per settled
island: name/hue, stockpile amounts + capacities, faction bid/ask, resident
totals, compacted building list `{type,row,col,active,ticks_remaining}`;
plus ships and `sim_tick_no`. A curated snapshot is <10 KB; even a blind
copy is ~100 KB/frame — negligible.

Every overlay's `*_build(UiList *, const UiSnapshot *, const UiState *)`
takes the snapshot, **never** `GameState *`. This makes "UI stepped the RNG
/ mutated the stockpile" a compile error instead of a grep target, and
makes the snapshot's origin — live sim, replayed past tick, remote server —
invisible to UI code.

`UiState` (a new struct: `hud_category`, trade page, `inventory_open`, open
overlay) is client state: excluded from `sim_hash`, never serialised into
saves, and itself a pure fold over the input stream (see decision 5's CI
harness).

**Hard rule, enforced by the harness build:** no layout decision may
consult TTF text measurement. Rows are fixed-height, columns fixed-width.
The headless harness links the UI `.o` files *without* SDL_ttf; if a layout
needs a font metric, the harness fails to link and the rule has been
broken.

### 2. The UI speaks Command, in stable identities

Every player action ends in `command_submit()`. The confirm-popup flows,
the road-drag loop, trade buttons — none of them call sim mutators (this is
MMO_PLAN Phase 1a's split, stated from the UI side).

**Ban positional indices at the UI-to-command boundary.** UiList ids encode
*identity* — the resource enum value, the building type, the entity id —
never "row 3 on page 2" or "slot 5 under this tab". Pagination plus a
growing `RES_COUNT` means positional encoding silently changes meaning
across versions, and a click recorded positionally becomes a wrong-resource
`CMD_SELL` when an old `.smlog` replays against a newer def table — the
nastiest desync class, invisible until replay. Additionally: **list
ordering is frozen while an overlay is open**; rule-driven reflow (alert
sorting, pagination) may re-sort only between open/close, so the row cannot
move between the frame that drew it and the click.

### 3. Rejection is a first-class rendered signal

MMO_PLAN requires `sim_apply()` to return 0 and do nothing on invalid
commands (replays must fail identically) — which means every rejection is
currently a silently eaten click, and players who get no feedback re-click
and flood the log.

- `RejectReason` enum in `src/command.h` (`<stdint.h>`-only, sim-legal):
  `REJ_OK`, `REJ_OUT_OF_BOUNDS`, `REJ_NOT_BUILDABLE`, `REJ_CANT_AFFORD`,
  `REJ_NOT_OWNER`, `REJ_COUNTERPARTY_NO_GOLD`, `REJ_NO_STOCK`,
  `REJ_PRICE_MOVED`, `REJ_ESCROW_REFUSED`, … The reason→string table lives
  UI-side.
- `sim_apply()` returns the enum. Its validation front-half is split out as
  **`sim_validate(gs, cmd)` — a shared pure function** called both
  per-frame by the UI (hover prediction, greyed-button tooltips, red ghost
  tint on a failing tile) and authoritatively inside `sim_apply`. One
  validator serving prediction and authority is structurally immune to the
  drift that a separate client-side pre-check would guarantee. It must stay
  side-effect-free — it may not step the RNG — or hover itself becomes a
  desync source.
- **Correlation across the tick boundary:** `command_submit()` stamps each
  locally emitted command with a client-local sequence number; the UI keeps
  `{seq, anchor}` (a screen rect or a tile) in a small pending ring — UiState,
  never hashed. When the tick applies commands, `(seq, reason)` results are
  drained by the UI and rendered as a ~0.5s decaying flash + reason text
  *at the emitting widget or tile* (localized, not a global toast), via a
  tiny cosmetic `fx_reject.c` (per-frame, `delta_time`, outside the sim).
  Replayed and remote commands have no pending entry, so recurring
  rejections during F9/load/resync are recomputed deterministically but
  silently — feedback is inherently local-only with no special-casing.
- This unifies the dead `building_can_place()` reason string, faction
  out-of-gold refusals, escrow rejections, ownership rejections
  (`REJ_NOT_OWNER` is how co-op privacy teaches its boundaries), and stale
  price rejections into **one vocabulary shared by the UI and the sim** —
  the message shown is definitionally the reason the sim refused.

### 4. One exchange surface, parameterised by counterparty

MMO_PLAN's thesis — "a bot is indistinguishable from a slow player" —
carried into the UI: the trade screen (fixed prices today, elastic faction
at MMO Phase 3) and the harbor-escrow accept/reject panel (MMO Phase 5)
are **the same ui_kit overlay** parameterised by an `ExchangeView` value
struct:

```c
typedef struct {
    const char  *title;
    ExchangeKind kind;                 /* EXCHANGE_QUOTES | EXCHANGE_OFFER */
    int32_t      their_gold;           /* INT32_MAX = infinite counterparty */
    int32_t      their_stock[RES_COUNT];
    int32_t      bid[RES_COUNT], ask[RES_COUNT];
    uint8_t      refuse[RES_COUNT];    /* RejectReason per row, or REJ_OK */
} ExchangeView;
```

A pure value snapshot — never pointers into live sim state — so
`trade_ui_hit_test(screen_w, screen_h, view, x, y)` stays headless-pure.
Builders arrive one per era, against the same struct:
- `exchange_view_fixed()` — Phase 1, copies `SELL_PRICE`/`BUY_PRICE`,
  infinite sentinels. **Behaviour-identical to today**, so the rewrite
  lands before `faction.c` exists.
- `exchange_view_faction()` — MMO Phase 3, real gold/inventory/quotes,
  `refuse[]` set when the faction is out of gold or stock.
- `exchange_view_offer()` — MMO Phase 5, rows are escrowed cargo lines.

Row layout (one 34px row per good, from v1) gains fixed columns from day
one: swatch | name | yours | theirs | bid | ask | action cluster. Phase 1
renders "theirs" empty via the sentinel, so **no geometry changes at Phase
3**. The only per-kind divergence allowed: the action cluster
(`[-10][-1][+1][+10][Max]` for QUOTES, none for OFFER) and the footer
(Close vs Accept/Reject) — two designated `kind` switch points. If
`ExchangeKind` branches start appearing per-column, the unification has
failed and should be split (see risks).

**Limit-order semantics:** every emitted trade hit carries the price the
row displayed; the UI wrapper stamps it into the Command's spare payload
ints as a worst-acceptable limit. `sim_apply` recomputes the live quote and
rejects with `REJ_PRICE_MOVED` if it moved adversely — the stale-screen
race across the tick boundary becomes a logged, replayable, *visible*
non-event instead of a mis-fill or an exploit.

### 5. Pending vs confirmed is the visual grammar

Commands apply at tick boundaries now and N ticks late under lockstep.
Rather than hiding that: everything submitted-but-unapplied renders in a
distinct queued style — translucent ghost building, greyed in-flight trade
row — that hardens when `sim_apply` lands. A stuck co-op session shows a
growing pile of unconfirmed ghosts instead of silently eaten clicks, so
"is it lag or is it broken" is answerable from the screen. Cancelling a
not-yet-applied command is free undo, which shrinks what the confirm
popups need to do (Phase 6).

---

## Phases

v1 phases re-cut, plus **M-phases pinned to MMO_PLAN phases** (an M-phase
lands with, or immediately after, its MMO counterpart — never before).
Each remains independently shippable and verifiable.

### Phase 0 — `ui_kit` + `UiSnapshot` — **DONE**
As v1 (layout cursor, `UiList`, canonical `ui_point_in`, measured-then-
clamped geometry) with the signature decided up front:
`*_build(UiList *, const UiSnapshot *, const UiState *)`. Define
`UiSnapshot` in SDL-free `src/ui_snapshot.h`.
**Verify:** headless `ui_row()`/`ui_split_h()` asserts; harness links UI
`.o` files without SDL/SDL_ttf.

*As built:* the purity rule is enforced by the build system rather than
by convention — `ui_kit.c` and `ui_snapshot.c` compile into
`libsaltmarch_ui`, which links no SDL, and `tests/test_ui_kit.c` links
that archive alone. `ci/sim-sdl-free.sh` covers it alongside the sim and
the server. Widget labels are copied into the list rather than borrowed,
since a `UiList` outlives its builder (golden diffs serialise it).

### Phase 0.5 — RejectReason conversion — **DONE**
Convert `building_can_place()`'s dead `(char *reason, size_t)` channel to a
returned `RejectReason`; delete `set_reason()`; add the enum→string table
in ui.c; wire it into the HUD hover tooltip. Kills v1's bug #2, fixes the
enum's home before `sim_apply` exists to adopt it.
**Verify:** headless assert per placement-failure case returns the right
enum; tooltip shows it in-window.

*As built, with two deviations:* the enum→string table lives in
`ui_kit.c`, not `ui.c`, so the headless test can assert every reason has
a distinct string without linking SDL. And the reason renders **at the
cursor** rather than in the HUD bar — the answer belongs to the tile
being pointed at (decision 3's "localized, not a global toast"), and a
player hunting for a legal spot reads it without looking away.
`building_can_place()` survives as a boolean wrapper over the new
`building_place_check()`: `REJ_OK` is 0, so converting the existing
`if (building_can_place(...))` call sites in place would have silently
inverted every one of them.

### Phase 1 — Exchange screen rewrite — **DONE**
The v1 trade rewrite (34px rows, `TRADE_W` → ~760, height computed then
clamped, pagination `[Prev] 1/2 [Next]`, category grouping) built as the
generic exchange surface: `ExchangeView` + `exchange_view_fixed()`;
trade_ui.c stops including the price tables directly. Ids are resource
identities, never row/page indices.
**Verify headless:** for N in {6,10,25,40}: rect containment in 1920x1080,
hit round-trip to `(resource, qty)`, **and** the fixed builder reproduces
today's prices with `refuse[]` all REJ_OK. Plus the miniature harness: a
synthetic snapshot driven through build+hit_test with a scripted click
sequence, asserting the emitted command sequence.

*As built:* panel 860 wide, one 34px row per good, columns
swatch|name|yours|theirs|bid|ask and a right-anchored
`[All][-10][-1][+1][+10][Max]` cluster. trade_ui.c is now only a
drawer — it consumes the `UiList` that `exchange_build()` produced, so
the rects that are drawn are literally the rects that are hit-tested.

Deviations:
- `exchange_view_fixed()` was skipped and `exchange_view_market()`
  written instead: the fixed price tables died with MMO Phase 3, so
  quotes come from the faction via the snapshot. Category grouping is
  not implemented — it belongs with Phase 2's resource categories.
- Rows are a list carrying their own identity rather than parallel
  `[RES_COUNT]` arrays. That is what lets the test build a 40-row view
  without RES_COUNT growing to 40 first — i.e. what lets the cliff be
  *proven* gone rather than argued gone — and it is the shape an escrow
  offer's cargo lines need at M5 anyway.
- Direction (sell vs buy) is carried by the widget's id group, and the
  quantity stays a plain count with `-1` meaning "as much as possible",
  matching what `game_sell_resource`/`game_buy_resource` already accept.
- Per-button refusals are computed in the builder from the numbers it
  already has; `refuse[]` on the row carries only the counterparty's
  side (out of gold, out of stock). One truth per refusal, not two.

### Phase 2 — Data model — **DONE**
Unchanged from v1: `BuildingCategory` on `BuildingDef`, resource-category
table, designated initialisers everywhere (the `RES_COL` lesson).
**Verify:** headless assert every enum value has a non-default category.

*As built:* five building categories (Gathering, Production, Housing,
Infrastructure, Maritime) and three resource ones (raw, refined,
currency), both with `*_NONE = 0` so a row added without a category
fails the test rather than being filed under a real one. test_defs.c
also asserts no category is empty of placeable buildings — a tab that
opens onto nothing is the other half of the same mistake.

The fields inside each `BUILDING_DEFS` row are designated now, not just
the row indices. Before touching them the compiled table was dumped to
text, and the dump was diffed after the conversion: byte-for-byte
identical, and the determinism fixture still hashes 41f8ca6fde89c2ae.

Phase 1's deferred category grouping landed with it: exchange rows are
built in category order (two passes, no comparator), so a page holds
related goods and enum order is preserved inside each group.

### Phase 3 — HUD category tabs — **DONE**
Unchanged from v1 (HUD_HEIGHT 80 → ~112, 28px tab strip, sticky tab,
greyed-not-hidden unavailable buildings) with one upgrade: the hover
tooltip's "why can't I build this" now calls `sim_validate()` once the
funnel exists (Phase 0.5's enum until then).
**Verify:** as v1 (synthetic 40-entry def table, per-tab slot fit and
hit-test).

*As built:* `hud_view.c` (SDL-free) owns the bar's layout, tabs,
affordability and hit decoding; `ui.c` is its drawer. The four separate
`ui_hit_test` / `ui_cog_hit_test` / `ui_demolish_hit_test` /
`ui_world_hit_test` entry points are gone — main.c makes one `hud_hit()`
call against the list that was drawn, so the right-hand buttons are part
of the same list as the slots.

Two decisions worth recording:
- **Greyed means muted, not disabled.** An unaffordable building stays
  clickable, because the build-confirm popup can still offer to pay in
  Gold — refusing the click would remove a real option. `UI_W_MUTED`
  exists for exactly this distinction; `UI_W_DISABLED` remains for
  things that genuinely cannot be done.
- **HUD metrics moved to `hud_view.h`**, which ui.h now includes. They
  had to leave ui.h because ui.h carries SDL and the layout that uses
  them may not. Every existing reader of `HUD_HEIGHT` (client.c's hover
  cutoff, ui.c) is unchanged.

Deferred deliberately: the tooltip calls `building_place_check()`, not
`sim_validate()`, which does not exist yet — the plan says the enum
suffices until it does. A tab holding more slots than fit shows a
"+k" marker rather than paginating; if a category ever gets that big it
wants splitting, and saying so on screen is how that gets noticed.

### Phase 4 — Vitals, inventory, overlay arbiter — **DONE**
As v1 (rule-driven vitals strip capped at 8 rows with `+k more`; inventory
overlay; `game_topmost_overlay()`; **fix the mouse-wheel bug**), plus: the
vitals rule engine reserves **sim-health rows** rendered by the same
alert machinery — last F9 result, tick-accumulator backlog, and (from M4)
feed age. The player is the monitoring system; a stall is visible seconds
after it starts.
**Verify:** as v1, plus a synthetic snapshot with a stalled accumulator
asserts the health row appears.

*As built:*
- `vitals.c` — six island rules plus four health rules, each a function
  of the snapshot producing at most one row; ranked by severity with
  insertion order as the deliberate tiebreak, so a row never swaps with
  a peer because a count changed. Feed age landed early (the feed has
  existed since MMO Phase 4) rather than waiting for M4.
- `inventory_view.c` + `inventory_ui.c` — the stores overlay on `I`,
  grouped by category, paged, with capacity as a bar rather than a
  fraction. It counts goods **at sea and in escrow** as well as stored:
  "where did my Wood go" is a question the corner panel cannot answer.
- `game_topmost_overlay()` in game.c, with `game_overlay_open()` beside
  it. The wheel bug was never a wheel bug — the zoom code simply never
  asked whether anything was open. Hover highlighting and road-drag
  now ask the same question, replacing a hand-maintained list of three
  flags in client.c that was already missing four.
- The speculative `UiOverlay` enum from Phase 0 was deleted rather than
  kept in parallel: two enums naming the same thing is how they drift.

Deferred: the strip is display-only. Clicking an alert to jump to the
building it names wants a camera-focus path that does not exist yet.

### Phase 5 — Island context — **DONE**
Unchanged from v1 (`‹ Island Name ›` header, chevrons over settled islands,
per-island hue, island name in overlay titles).

*As built:* `island_bar.c` (SDL-free) builds the header; the drawer sits
beside the vitals strip in inventory_ui.c. Chevron widgets carry the
island they switch TO rather than a direction, so a click needs no
arithmetic to interpret and cannot be misread if the settled set changed
between frames. They step over unsettled islands — an unclaimed island
is something you look at from the world map, not somewhere you are — and
with only one island they are greyed rather than hidden, so the header
does not change shape when you found your second colony.

`island_hue()` is a fixed table indexed by island slot, not a hash or a
settlement-order counter: the colour has to be the same for every client
looking at the same world, or two players describing "the blue island"
would mean different places. The exchange and stores overlays carry the
hue on their view structs and draw it as a stripe down the title bar.

### Phase 6 — Confirm consolidation → command preview — **DONE**
v1's collapse of demolish/tier-upgrade/ship-build/build-confirm into one
`confirm_ui.c`, with a new job: the popup renders the *literal Command it
will submit* (kind, decoded payload, apply tick). The confirm layer and
the wire format become the same rendering code — screenshots become
forensics, and the UI cannot drift from what `sim_apply` receives.
**Verify:** hit-test results identical before/after; rendered preview
matches the submitted Command byte-for-byte in the headless harness.

*As built:* `game_confirm_build/demolish/upgrade/ship()` construct the
Command when the popup OPENS and store it in `GameState.confirm`;
accepting submits that struct verbatim. The preview text comes from
`command_describe()` in command.c — decoding that lives beside the
encoding it mirrors, so the popup cannot describe one action while
sim_apply receives another. The test compares the rendered command with
the one that reached the log, as bytes.

Storing the command rather than the ingredients also made structural
the property the old build popup maintained by hand: the tile is
captured at open time, so moving the cursor to the buttons — or
selecting a different building type — cannot change what gets built.
There is a test for that specifically.

The openers validate: a confirmation for a building that no longer
exists never opens, instead of opening and submitting a command the sim
would reject.

Three files went away (build_confirm_ui, demolish_confirm_ui,
tier_upgrade_ui — the last of which was already doing double duty for
ship building), along with four popup flag sets in GameState, four
branches in the click cascade and four in the draw order. Right-click
dismissal now reads the layering from `game_topmost_overlay()` rather
than repeating it as a second hand-maintained list.

Deferred: the preview shows the earliest tick a command can apply, not
the exact one — under lockstep the host adds its delay and the client
cannot know it. Stated as "or later" rather than guessed at.

### Phase M1 — with MMO Phase 1 (command funnel) — **DONE**
- UI wrappers emit Commands via `command_submit()`; pending ring
  (`{seq, anchor}`), rejection drain, `fx_reject.c` flash-at-anchor.
  **Done.** `Command` gained a client-local `seq` (save v7, net proto 3);
  `sim_apply_reason()` reports why, with `sim_apply()` kept as the
  boolean form because REJ_OK is 0. The reasons come from the mutators
  themselves — no second validator, per the dual-validation risk. Ship,
  escrow and route commands still answer `REJ_UNAVAILABLE`; converting
  those bodies belongs with M5, which is the phase that renders them.
- Pending-vs-confirmed queued rendering (decision 5) for placements and
  trades. **Placements done** — drawn straight from the log's unapplied
  tail, so there is one queue rather than a mirror of it. Trade rows are
  not yet marked.
- **INTENT lines in the `.smlog`**: mouse x/y, clicks/wheel/keys, and the
  exact `sim_tick_no` the frame's snapshot was taken at, interleaved with
  CMD lines. **Done** — a second binary section (save v8) rather than
  text lines, carrying tick, position, the view state (page, tab,
  overlay) and the hovered tile, plus the `seq` of whatever command the
  click produced. The hovered tile is recorded because it comes from the
  camera, and the camera never enters the log.
- **CI UI replay**: **done**, running on all three platforms
  (`--record-ui` then `--replay --verify-ui`). The fixture's trades are
  performed by hit-testing the real exchange screen, so what is recorded
  is a genuine (frame, position) pair. On replay the harness rebuilds
  each frame's snapshot at its recorded tick, runs the real builders and
  hit-tests, and checks both that every widget is on screen and that the
  click still emits the command the log holds.

  Verified to actually fail: making rows 10px taller makes two of the
  four recorded clicks hit nothing, and the run exits 1. An earlier
  version of the check passed that silently — a click that hits nothing
  produced no expected command, and "no expectation" compared equal to
  everything.

  Limited to the exchange screen for now. Map clicks and the confirm
  popup route through main.c's cascade, which is SDL-side; widening the
  harness means extracting that cascade into a pure function, which is
  its own piece of work and not a side effect of writing this one.
- Golden UiList diffs: `--dump-ui FILE` writes the canonical text
  (id, rect, flags, reason, value, label per widget, per recorded
  click). Not yet committed as goldens or diffed in CI — the geometry
  assertions cover the same class today, and a golden file is only worth
  having once the layout has stopped moving every phase.

### Phase M3 — with MMO Phase 3 (elastic market) — **DONE**
- `exchange_view_faction()`; refusal rendering (greyed cells + reason,
  reusing the existing unaffordable-buy greying path); "faction out of
  gold" message lands here. **Landed early**, in Phase 1: the fixed
  price tables were already gone, so the exchange view read the
  faction's live quotes from the start.
- Limit-order price stamping + `REJ_PRICE_MOVED` flash. **Done.** The
  price the clicked row was DISPLAYING rides in the command's spare
  slot; sim_sell/sim_buy recompute the live quote and refuse when it has
  moved against the player. Zero means no limit, which is what replayed
  and scripted commands carry, so old logs are unaffected.
- **Price-history sparkline column** (~48px per row): the faction keeps a
  small per-resource ring buffer of mid-price sampled every K ticks — sim
  state, in `sim_hash`, so replay covers it; `ExchangeView` carries a
  copy. Sell-Max leaves a visible scar that mean-reversion visibly heals —
  this is the mitigation for MMO_PLAN's "rigged slot machine" risk, and
  the Phase 3 debug/tuning overlay renders from the same buffer (the
  tuning UI and the player UI cannot disagree about the quote).
  **Done** — 24 samples per good, one every 50 ticks; the trade screen
  and the F10 overlay call the same `render_sparkline()`.

*Two things worth recording.* The determinism fixture's hash changed
(41f8ca6fde89c2ae → 0577606f5f7a9676) for the first time in this whole
effort: new sim state is hashed state, and that is the intended cost of
making the history replayable rather than cosmetic.

And adding it immediately exposed a latent bug. `faction_init()` set its
fields one by one and GameState is malloc'd, so the new array was
uninitialised memory entering `sim_hash` — two clients of the same world
disagreeing for reasons neither could see. It memsets first now. The
co-op resync test caught it the instant the field appeared, which is
that test earning its keep.

### Phase M4 — with MMO Phase 4 (shared feed) — **DONE**
The feed is out-of-process, wall-clock, and **untrusted input**:
- Every feed-derived element carries an age stamp and decay visual; a feed
  heartbeat chip (island header area) goes stale-coloured when feedsync
  stops appending. Staleness is a rendered property, or the ocean quietly
  becomes a museum of hours-old ships.
- Hygiene at the UI boundary: clamp owner-name strings before fonts.c
  sees them; cap the ghost draw list with the `+k more` overflow pattern;
  count malformed VoyageRecords into a visible debug counter instead of
  dropping silently.
- Ghosts render in a distinct muted style (v1's "non-self" is now a real
  category); tooltip info only.
- **TTF_Text migration in fonts.c happens here at the latest** — see
  risks; untrusted text makes worst-case text throughput
  adversary-controlled, which converts v1's "act reactively" into a
  scheduled prerequisite.

*As built:*
- `font_draw_text()` keeps its signature and now draws through a cache
  of `TTF_Text` objects (256 slots, keyed by size and string, colour
  applied per draw since `TTF_SetTextColor` is cheap). The old
  rasterise-upload-destroy path survives for strings too long to cache
  and for the case where the text engine cannot be created — correctness
  first. The F10 overlay shows frame time and the cache's hit/miss
  counts, so the claim is checkable rather than asserted.
- `ui_clean_label()` replaces anything outside printable ASCII with '?'
  and clamps. feed.c runs every parsed string through it, so a peer's
  name reaches the renderer with no control bytes in it. Length was
  already clamped; this closes the content half. Rejecting non-ASCII is
  a real limitation, taken deliberately: the bundled font is a Latin
  subset, and a name that renders as blank boxes is no better than one
  that renders as question marks.
- The map draws at most `WORLD_MAX_DRAWN_GHOSTS` (24) voyages and states
  the remainder as "+k more ships at sea".
- Malformed feed records become a vitals row rather than a silent drop:
  a sync script writing garbage otherwise looks exactly like a quiet
  ocean.
- A feed heartbeat chip sits beside the island header, stale-coloured
  past two minutes. Ghosts fade out on their own, so without it a sync
  script that died an hour ago is indistinguishable from an empty sea.

Ghost styling was already distinct and tooltip-only from MMO Phase 4, so
that bullet needed nothing.

### Phase M5 — with MMO Phase 5 (lockstep co-op) — **DONE**
- `exchange_view_offer()` + Accept/Reject footer for harbor escrow; the
  docking-permission toggle on the island panel.
- Escrow offers are nonce-stamped; the accept Command references the
  nonce; the confirm layer gains a generic "offer changed — re-review"
  invalidation that greys Accept when the referenced state mutates under
  an open popup (also fixes single-player's version: gold draining under
  an open build-confirm).
- Pending-order grammar at N-tick delay becomes the primary feedback;
  `REJ_NOT_OWNER` renders as an owner-coloured border pulse at the
  clicked tile — privacy-by-validation taught through the rejection
  channel, visible only to the prober.

*As built:*
- `exchange_view_escrow()` replaces escrow_ui.c entirely: the harbour is
  the marketplace screen with a different counterparty. The two kinds
  diverge in exactly the two places decision 4 permits — the action
  cluster (take/stage rather than six quantities) and the footer (the
  blockade lever rather than a pager). If a third divergence ever
  appears, the widget should be split; the test asserts today's two.
  Gold is a row here and never on the marketplace, because leaving coin
  on a quay is how a visitor pays.
- Offers are nonce-stamped. `island_escrow_nonce()` lives in the sim and
  the snapshot carries it, so there is one implementation rather than a
  UI copy that could disagree. A stale stamp is refused with
  `REJ_OFFER_CHANGED`; an unstamped command (zero) still applies, which
  is what replayed and scripted logs carry.
- The ship, escrow and route mutators now return real reasons — the gap
  left open at M1. A blockade and a missing harbour both say
  "Harbour closed to you"; an empty quay and a full warehouse are told
  apart.
- `REJ_NOT_OWNER` draws an island-coloured diamond outline at the tile
  as well as the words, fading with the flash.

Deferred: the confirm layer's "offer changed — re-review" invalidation,
which would grey an open popup's Accept when the state it references
moves. The nonce makes that a display concern rather than a correctness
one now — the command is refused either way — so it can wait for a
phase that is looking at the confirm layer anyway.

---

## The maritime UI — the sim has outrun the screen

Everything above is done. Since it was written the simulation gained an
order book, merchants and hulls as capital, an NPC market with home
ports, three routes between every pair of islands, charts and per-player
knowledge of the sea, per-route insurance, survey expeditions, chart
expiry, ship classes and escorts, huntable pirate fleets, server
authority and per-client concealment.

**None of it has a screen.** A player running the game today cannot post
an order, buy a chart, send an expedition, choose a hull, form a convoy,
or hunt a fleet. All of it works, all of it is tested, and none of it is
reachable. That is now the largest gap in the project and it is in no
other plan.

### The five decisions survive, and three are stronger

Nothing below asks to revisit them.

**Decision 1 (UI is a pure function of a snapshot) has quietly become a
security property.** When it was written it was hygiene: a `UiSnapshot`
argument made "the UI stepped the RNG" a compile error. Since
SERVER_AUTHORITY Phase 3 the client's own `GameState` is *already*
redacted — a rival's stockpile is not hidden from the UI, it is absent
from the process. So the snapshot builder cannot leak what it does not
have, and concealment needs no discipline from UI code at all. This is
worth stating because the opposite instinct is very natural and would be
a disaster: **the UI must never become the thing that hides.** The
moment a screen decides not to draw something it holds, concealment is
one modified client away from failing.

**Decision 5 (pending vs confirmed) now carries prediction too.** It was
written for lockstep latency. Under server authority the client is
guessing between pushes, so "submitted but unapplied" and "predicted but
unconfirmed" want the same visual grammar, and the grammar already
exists.

**Decision 2's frozen list ordering was written for pagination and now
has to carry concurrency** — see below.

### Three problems the plan has never faced

**1. Absence is not zero, and the UI has no word for it.**

This is the important one. Every existing overlay renders state the
player owns or the market publishes, so every number on screen is
*true*. Under concealment a foreign island arrives with an empty
building list and a zeroed stockpile — and those mean **"you were not
told"**, not "there is nothing there".

Rendering unknown as zero is not a cosmetic problem. It is the screen
telling the player something false about a rival, and the player will
act on it: an island showing 0 Planks reads as a market to sell into,
not as an island you know nothing about. Every foreign-data surface
needs a vocabulary for absence before it shows any foreign data at all,
which is why it is a phase of its own and comes first.

The distinction the snapshot must carry is not per-field. It is one flag
per island — *is this mine* — because the redaction is all-or-nothing
per island and pretending otherwise invites a UI that guesses.

**2. The world changes without you.**

Every surface built so far renders things only the player moves: their
stockpile, their buildings, a faction quote that drifts on a slow timer.
The order book is the first screen where **another player's action
changes what is in front of you mid-read** — a resting order is filled
by somebody else and the row you were about to click is gone.

Decision 2 already says list ordering is frozen while an overlay is
open, and that rule was written to stop *pagination* reflowing a row
under a click. It now has to carry real concurrency, and it is nearly
enough: freeze the order of what is displayed, but let rows go stale
visibly (struck through, greyed) rather than vanishing. A row that
disappears between the frame that drew it and the click that hits it is
the one failure this must not have — and the `RejectReason` path already
exists to say *that order is gone* when the click lands anyway.

**3. The sea is a place and the map is a node graph.**

`world_ui.c` draws eight nodes and lines between them. The sim now has
positions, named waypoints, three routes per pair with real geometry,
pirate lairs, and shipments moving along paths at known positions. Most
of the new content is *spatial*, and the map is both its natural home
and the least developed surface in the project.

This is also where concealment becomes legible rather than abstract:
a chart is worth buying when you can see the passage it opens, and a
pirate lair matters when you can see your lane runs through it.

### Phases

Ordered by what gates what, not by what is most interesting.

**N1 — the snapshot grows.** ~~Everything else is blocked on it.~~
**Done.** Add to
`UiSnapshot`: open orders and in-flight bookings; the charts the local
player holds and the routes they know; the routes currently in play per
pair; expeditions in progress; the fleets the player has reason to know
about; ship class, guns, hull and escort; trade and scholar capacity;
the insurance flag. Plus the one flag decision 1 now needs: whether each
island is the local player's.

*Do not copy the sea into the snapshot* — and it does not. Routes are ~200 entries of
fixed geometry, regenerated from the seed and mutable only in one byte
per pair. Copying them every frame would take the snapshot from under
10 KB to 60 KB for data that does not change. The UI should read `Sea`
directly and take only the per-pair cursor through the snapshot — the
one exception to decision 1, and it is safe precisely because a `Sea` is
generated rather than owned.

**N2 — absence has a look.** ~~The vocabulary for "not told"~~ **Done.**
`ui_fmt_known()` in `ui_kit`, so every surface says it the same way and
a player learns the mark once. An em dash sits where the digits would
be; the alternatives were dimming (still reads as a value, and is the
most likely to be believed) and leaving a gap (the eye skips it, and
"I did not see a number" is a different thought from "there was no
number"). The mark wins because it is the only one that is *present*.

It found a real one on the way in. The world map labelled every rival
island **"Pop 0"** — `pop_total()` over a redacted building list is
zero, so a held, populated colony read as deserted. That line was the
plan's hypothetical example and it turned out to be live code.

The stores overlay marks rather than hides: rows are still built and
laid out, so scrolling and hit-testing do not grow a second shape that
only foreign islands ever take. The capacity bar is the one thing
dropped entirely — a bar is a quantity with no way to say it does not
know one, and an empty track reads as an empty warehouse.

**N3 — the order book. Done.** Post a buy or a sell with a limit price,
see your resting orders, cancel one, on `B`. Your side of the book only
— depth is a later question and possibly an intelligence one.

First because it gates the rest: charts are bought and sold through it,
so nothing downstream is reachable without it.

The simulation half is already finished and N1 already carries every
field the screen needs, so this is a UI phase in the strict sense:
`sim_place_order` / `sim_cancel_order` reserve, cap and refund;
`game_place_order` / `game_cancel_order` are through the funnel with
`seq` stamped; `UiOrder` has id, side, qty, limit and `placed_tick`.
Four things had to be decided before any of it was written.

*The price is entered with buttons, because there is no text input in
this game.* `InputState` carries clicks, a wheel and a few function
keys; a text field would mean keystroke capture, a caret, focus, and
recording every keypress as an Intent. So the book gets a draft strip —
side, quantity, limit, Post — over stepper buttons, pre-filled from the
live quote so the ordinary case is two clicks and the steppers cover the
rest. Offsets from the quote alone ("bid −5%") were the cheaper option
and were rejected: they cannot express a limit far from the market,
which is most of what a book is for.

That makes the draft the first *composed* UI state in the project.
Everything until now was a page index. It lives in `UiState`, which is a
pure fold over the input stream, so `IntentUiState` records it and the
save version moves — a recorded log's bytes no longer line up.

*The book is its own view, not a third `ExchangeKind`.* Decision 4's
prediction was that per-column branching would mean the unification had
failed, and it fails here before a line is written: the columns are
disjoint (side, resting quantity, limit, age against yours, theirs, bid,
ask, trend), the action cluster is one Cancel against six quantities,
the composer exists only here, and the row identity is an order id
rather than a `ResourceType`. `book_view.c` + `book_ui.c`, on the same
builder/drawer convention as everything else. The plan named this
`exchange_view_book()`; the plan's own mitigation says to split rather
than defend, and the evidence arrived early exactly as it predicted.

*Rows are retained, not rebuilt.* The snapshot carries live orders only,
so a filled order is simply absent on the next frame and every row below
it slides up under the cursor — the one failure this must not have. The
builder therefore folds (previous view, snapshot) rather than reading
the snapshot alone: an order that leaves is kept in place, struck
through and greyed, and its Cancel is dead. Still a pure function, still
headlessly testable, and the ordering is frozen by ascending order id
rather than by array position. Where a click lands on a stale row
anyway, `sim_cancel_order` refuses and the flash says so.

*A widget id has sixteen bits and an order id has thirty-two.*
`Order.id` is a monotonic counter that the NPC market burns through fast
— it withdraws and re-posts its quotes at every refresh, across ports ×
goods × two sides — so the low half wraps inside a long session. The
identity in the widget id keeps the low sixteen bits and the full id
rides in the widget's value, which is what the cancel hit reads. Content
derived, never positional, so decision 2 still holds.

Two smaller things. `sim_cancel_order` answered a vanished order with
"Not possible right now"; the book needs *that order is gone*, so the
vocabulary grows one entry. And chart orders can be seen and cancelled
here but not posted: choosing which passage to sell a map of is a route
picker, and route pickers are N4 — until then a chart row names itself
by id.

What the book does NOT do is hide. Every resting order in the world is
in this client's memory: `redact_for()` does not touch the book, and
showing your own orders only is a scoping decision about what is useful,
never a concealment mechanism. If depth should be secret it is redacted
at the server, not omitted by a drawer — the mistake this document warns
about twice.

Two things the building of it turned up. The retained fold's first
version marked rows by INDEX as it matched them against the snapshot,
which is wrong the moment a row is inserted or evicted: every mark after
that point describes its neighbour, and a live order would read as gone
for a frame. Ids do not move, so the fold collects the live ids and
compares afterwards. And the recorded CI session had to be taught to
post UNDER the market's ask — a buy priced at the quote crosses it
immediately, so the first version of that recording posted an order that
had already filled by the time the next click tried to cancel it, and
tested nothing. Both are in `tests/test_book.c` now.

**N4 — charts and routes. Done.** What you know, what you hold, what a
passage saves, what a chart costs on the book, and how long that water
stays in use — on `C`. `chart_view.c` + `chart_ui.c`, on the same
builder/drawer convention as everything else.

It closes the hole N3 left open. The order book could show and cancel a
chart order but not post one, because choosing which passage to sell a
map of needs somewhere a passage has a name. Here a **row is the
picker**: Buy and Sell emit `(TRADE_ROUTE_CHART, route id)` orders
through the funnel at the price the row was displaying, and the
composer stays a resource-only affair. One map per click — a chart is
not a commodity you buy ten of, each crossing spends exactly one, and
anything cleverer wants a composer that already exists elsewhere.

*This is the phase N1's exception was made for.* The sea is read
directly rather than copied per frame: ~200 entries of fixed geometry
regenerated from the seed, against one mutable byte per pair that comes
through the snapshot. Safe precisely because a `Sea` is generated rather
than owned — there is nothing in it a UI could mutate that would not be
rebuilt identically.

*The rows are retained, for a sharper reason than N3's.* When a passage
retires, the pair's variant-1 slot starts naming **different water** —
so a screen rebuilt from the sea alone would not merely slide a row, it
would swap one map for another under a cursor already travelling toward
Buy. The retired row stays where it stood, struck through and saying
"out of use", and the passage that replaced it is appended inside the
same destination group. This is also the one event the screen exists to
warn about, so having it happen *on screen* rather than between frames
is the feature.

*The expiry clock and the event that fires it are now one function.*
The rotation schedule lived inside `game.c`'s update loop, where a
screen could only have copied it — and a copied schedule is one edit
away from telling a player their charts have hours left on the morning
they become waste paper. It is `sea_pair_next_rotation()` now, called
by the sim to ask "is it now?" and by the row to ask "how long left?".
The test runs the world to the tick the row counted down to and asserts
the sim rotates on it; perturbing the countdown by fifty ticks fails it
from both sides.

*Which cells are marks is the whole design of the row.* A passage you
have not learned hides its name, its crossing and what it saves —
those are what a chart tells you — but its **price and its expiry are
plain numbers**, because the market's resting offer and the rotation
schedule are public facts about water you have never seen. Marking all
six would have been tidier and would have lied in the other direction:
it would say the market has nothing on the counter when your map is
sitting there priced. Buying that map is, in fact, the main way a
passage becomes known.

Two things stated rather than assumed, both of which this document warns
about elsewhere. **The screen does not hide**: this client's `Sea` holds
every passage in the world by name, because it is generated rather than
redacted, so drawing an unlearned one as a mark is a scoping decision
exactly like the book showing your own orders — what actually conceals
is the sim, which will not route a booking down a passage its seller
does not know. And **it does not refuse what the sim would accept**: the
market will sell a map of water nobody sails, and a retired row says so
in words rather than by greying its buttons, because a screen that
refuses a click the sim would take is the drift decision 3 exists to
prevent.

Deferred: the insurance premium per route (it is N8's number, and one
column is not worth splitting it from the toggle beside it), and
dispatching an expedition — N7's, though a destination whose expedition
is already out says so here, never naming the passage it is looking for,
because not knowing is what you are paying for.

**N5 — the sea. Done.** `world_ui` grows routes as paths (harbour, the
waypoints they thread, harbour), waypoints by name, shipments where
`sea_route_point` actually puts them, and the lairs on the way.

The geometry moved out of the drawing. This document puts `world_ui`
out of scope for the widget kit and that stands — a map does not want a
layout cursor — but "out of scope for rows and columns" turned out not
to mean "untestable": a route drawn off the edge of the screen, a marker
that runs backwards, or a passage plotted that the player has never seen
are all catchable headlessly, and none of them could be caught while the
geometry lived inside a function that also called SDL. `sea_view.c` (in
the UI library) answers where everything goes; `world_ui.c` paints it.
The projection went with it unchanged, because everything spatial is
derived from it.

*Water you have never seen is not plotted*, and that is scoping rather
than hiding for a reason worth writing down: a list can print "unknown"
in a cell, but a line cannot be drawn unknown without inventing where it
goes, and inventing it is worse than omitting it. The passages screen
still shows the row, still counts down its expiry, and still sells you
the map — and buying the map is what puts the water on this one. A
passage you know but hold no chart for draws dimmer than one you can
sail, because knowing and holding are different things.

Shipments are everyone's, in the muted style the feed's ghosts already
taught: a booking is the public half of the order book, which is what
makes the book the honest channel. A raided one stays on the map, since
"nothing arrived, and here is where it stopped" is the information.

**N6 — the yard and the fleet. Done.** On `Y`: the three hulls with
guns, what they survive, what they carry and what they cost, side by
side; and the fleet, each ship with its class, condition, position and
who it is guarding.

The phase existed because a decision the sim makes was unreachable.
`game_build_ship()` is `game_build_ship_class(SHIP_MERCHANTMAN)` and the
confirm popup called it with no way to say otherwise — so **every ship
in every game since MARITIME_PLAN Phase 5 has been a merchantman**, not
by choice but for want of a screen. Escorts were the same: the command
has existed since Phase 5a with nothing to issue it.

Condition is the other half of it. A hull is the only thing about a ship
that moves and it only moves down; until now that number lived in the
sim alone, which meant sending a half-wrecked warship at a fleet without
knowing it was half-wrecked. The guns column is
`ship_fighting_strength()` — what the hull is worth *now* — not the class
table's figure, because the first is what the bet is made against.

There is **no refit**: the sim has no command that repairs a hull, and
inventing one here would mean inventing it in the sim, which is a design
decision and not a UI phase's to make.

*The escort cycler could not release, at first.* Modulo arithmetic round
the fleet lands back on the ship you are already guarding when there are
two of them, so a convoy could never be dissolved. It walks ascending
and off the end into "nobody" now, which terminates by construction; the
test cycles the whole fleet and asserts it comes back round, so a cycle
that could not release would hang rather than pass.

**N7 — expeditions. Done.** On the passages screen, because that is
where a destination already has a row and where the answer will appear:
one button per island, and a line saying once what an expedition commits
(a scholar, a research boat, a blank chart) and how often it finds
nothing — the odds read from the sim's own constant, since a screen
quoting its own number would be a second opinion about a gamble. You
still cannot name the passage; the command carries an island.

**The vocabulary grew by three, and that is the substance of the
phase.** `sim_survey` refused with `REJ_UNAVAILABLE` whether you had no
scholar, no boat, or had already charted every passage on that crossing
— three different facts wearing one sentence, and the last of them is
not a problem to be fixed but a thing to be told. It answers
`REJ_NO_CREW`, `REJ_NO_BOAT` and `REJ_NOTHING_TO_FIND` now.

*Which made the ORDER of the checks load-bearing.* The screen tested
"nothing left to find" first, because that is the most useful thing to
know — while the sim tests the crew first, so a harbour with no scholar
AND nothing to find showed one sentence and would have been refused with
another. Sorting refusals by helpfulness produces a screen that is right
about the world and wrong about the refusal, which is exactly the drift
decision 3 exists to prevent. The checks run in the sim's order now, and
the test compares the button's reason against `sim_apply_reason()`'s
across several broken worlds rather than asserting either alone.

Deferred: a failed expedition says so only in the log. The vitals strip
is a pure function of a snapshot and cannot see a transition, which
wants a small piece of sim state rather than a cleverer rule.

**N8 — insurance and capacity. Done.** In the stores overlay, because
goods are only half of what an island holds: merchants, hulls, scholars
and research boats are the other half — capital rather than stock — and
until now the only way to discover that every merchant was committed was
to watch an order sit unfilled.

Committed *and* capacity, always as a pair: "2 merchants" cannot say
whether that is comfortable or the whole of what you have, and which it
is decides whether to post another order. A research boat is shown as a
count rather than a pair, because there is nothing to be out of until
one sails and `sim_survey` counts them the same way.

The standing policy lever sits with them: it is the same kind of fact —
a property of the harbour rather than of any one voyage — and the
premium it pays is per route, so throwing it is a decision about
everything this port sends. It is labelled with what it will DO rather
than what it is, and its widget value is what it sets, because a toggle
labelled with its current state is the oldest ambiguity in a panel like
this.

### Risks

**The ExchangeKind if-ladder.** Decision 4 warned that if per-kind
branches start appearing per-column, the unification has failed. N3 adds
a *third* kind to a struct designed around two, and an order book has
genuinely different columns (limit price, quantity resting, age) from a
quote screen. This is the most likely place for that prediction to come
true. The mitigation is to notice early and split rather than to defend
the unification: one screen that is three screens in a trench coat is
worse than three screens.

*It came true, at N3, and the mitigation was taken* — see the phase
above. The prediction was right down to the mechanism: the divergence
showed up in the columns first. Worth recording that the warning was
worth writing, since the cost of the split was one file and the cost of
defending the unification would have been every screen after it.

**Snapshot growth.** Under 10 KB was the number decision 1 was argued
on. Orders, bookings, charts and expeditions are all bounded and small;
the sea is not, which is why N1 keeps it out. If the snapshot passes
~50 KB per frame the argument needs re-making rather than assuming.

**The temptation to conceal in the UI.** Stated above and repeated here
because it is the one mistake in this document that would undo work
already done.

**Text throughput.** Inherited and still unaddressed. Every phase here
adds rows of text, and the order book adds the most.

---

## Explicitly out of scope

- `world_ui.c` stays projected map geometry (v1 decision stands); it gains
  ghost rendering at M4 but not the row/column kit.
- **Time-travel scrubber UI** — the snapshot signature makes "open the
  trade screen at tick 40,000, hit-testing live, submission disabled" a
  one-flag change later; MMO_PLAN lists the scrubber as a later phase, so
  only the *signature* is in scope here, not the feature.
- Speculative what-if previews (fork the state, run N ticks, show the
  diff) — cheap to imagine over a deterministic sim, real scope. Revisit
  after M3 ships; `sim_validate()` hover feedback covers the load-bearing
  case (why is this action invalid) without it.
- Client-side adaptive/learned layouts of any kind: moving click targets
  breaks muscle memory *and* invalidates recorded intent replays.

## Throwaway work to avoid

- v1's list stands (no standalone TRADE_H hotfix; no porting confirm
  popups before Phase 1 validates the kit; no pixel-tuning before
  pagination).
- Don't build `exchange_view_faction()` speculation into Phase 1 beyond
  the struct — the struct is the contract; the builder waits for the
  faction.
- Don't record INTENT lines before the snapshot seam exists (M1) — intents
  without the observed `sim_tick_no` are unreplayable and worse than
  nothing.

---

## Risks

### Dual-validation drift (new headline correctness risk)
Lockstep stretches click-to-verdict to hundreds of ms, and the tempting
fix — a separate client-side pre-check for instant feedback — will diverge
from `sim_apply`'s authoritative check. The design only holds if
prediction and authority are **literally the same function**
(`sim_validate()`), and that function stays side-effect-free (no RNG
stepping), or hover-validation itself becomes the desync. Enforce: the
harness's intent fuzzer sprays random clicks over replayed snapshots and
asserts every emitted Command either applies cleanly or is rejected with
no state change.

### The frame/tick seam in intent recording
Intents happen at frame times; commands apply at tick boundaries. Each
INTENT line must record the exact `sim_tick_no` its frame's snapshot was
taken at, or CI rebuilds a different snapshot than the player saw and
hit-tests against stale prices/pagination. This is a format decision made
once at M1; get it right there.

### ExchangeView if-ladder collapse
The unification pays off only while the shared part (rows, pagination,
clamp, purity, refusal rendering) dominates the divergent part (action
cluster, footer). If `kind` branches leak into per-column code, split the
widget — two small files beat one conditional swamp. Checkpoint at M5
before building the offer view.

### Text throughput — inherited from v1, now adversary-adjacent
v1's analysis stands (`font_draw_text` rasterises every call; heavy
screens are modal; `TTF_Text` objects in SDL3_ttf 3.2.2 are the fix,
confined behind the `font_draw_text` signature). Changed: feed-supplied
strings make worst case externally controlled, so the migration is
**scheduled at M4** rather than "act reactively" — with the frame-time
readout baseline still landing before Phase 1.

### Save fragility — dissolved, replaced
v1 worried about `sizeof(Stockpile)` invalidating byte-for-byte saves.
MMO_PLAN's save v2 (seeds + command log) dissolves that — and replaces it
with **log-vs-def-table versioning**: an old `.smlog` replayed against a
reordered enum or changed def table. Stable-identity command payloads
(decision 2) are the UI's contribution; the sim side (a def-table
version/hash in the log header) belongs to MMO Phase 1d.

### Visual verification
v1's constraint stands (no xdotool; colour/legibility need human eyes) but
shrinks: golden UiList diffs + CI intent replay move geometry, ordering,
pagination and command-emission regressions into CI, leaving only actual
appearance for manual eyeballing.

---

## Verification convention

As v1 (clean rebuild under `-Wall -Wextra -Wpedantic -Wshadow
-Wconversion`, run the binary, headless C programs linking built `.o`
files asserting real behaviour), plus two new instruments once M1 lands:
recorded-session CI replay driving the real UI functions, and golden
UiList diffs. The UI `.o` files must stay linkable without SDL/SDL_ttf —
that link failure *is* the purity test.

## Critical files

| file | phase | what |
|---|---|---|
| `src/ui_kit.c` / `.h` | 0 | new — layout cursor, widget list, `ui_point_in` |
| `src/ui_snapshot.h` | 0 | new — SDL-free snapshot + UiState structs |
| `src/building.c` / `.h` | 0.5 | `RejectReason` return replaces dead string |
| `src/trade_ui.c` / `.h` | 1 | ExchangeView rewrite, the cliff |
| `src/building.h`, `src/resource.c` | 2 | categories |
| `src/ui.c` / `.h` | 3 | tabs, `sim_validate` tooltips |
| `src/render.c`, `src/game.c` | 4 | vitals+health rows, wheel guard, `game_topmost_overlay()` |
| `src/confirm_ui.c` / `.h` | 6 | new — unified command-preview confirm |
| `src/command.c` / `.h` | M1 | (MMO-owned) `RejectReason` home, seq stamping |
| `src/fx_reject.c` / `.h` | M1 | new — cosmetic rejection flashes |
| `src/exchange_view.c` / `.h` | M3/M5 | faction + offer builders |
| `src/book_view.c` / `.h` | N3 | new — resting orders, retained rows, the draft composer |
| `src/book_ui.c` / `.h` | N3 | new — the book's drawer |
| `src/chart_view.c` / `.h` | N4 | new — passages, the expiry clock, the route picker |
| `src/chart_ui.c` / `.h` | N4 | new — the passages' drawer |
| `src/sea.c` / `.h` | N4 | `sea_pair_next_rotation()` — one schedule, two callers |
| `src/sea_view.c` / `.h` | N5 | new — paths, waypoints, shipments, lairs; the projection |
| `src/world_ui.c` | N5 | becomes the sea's drawer |
| `src/yard_view.c` / `.h` | N6 | new — the hulls on offer, the fleet afloat |
| `src/yard_ui.c` / `.h` | N6 | new — the yard's drawer |
| `src/chart_view.c` | N7 | the expedition button and its refusals |
| `src/inventory_view.c` | N8 | the harbour block and the policy lever |
| `src/fonts.c` | M4 | `TTF_Text` migration (scheduled) |
