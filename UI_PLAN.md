# UI/UX Reorganisation Plan — v2, aligned with MMO_PLAN.md

> Status: **planned, not started.** Written for a future session to pick up
> cold. Nothing in here has been implemented.
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

### Phase 0 — `ui_kit` + `UiSnapshot`
As v1 (layout cursor, `UiList`, canonical `ui_point_in`, measured-then-
clamped geometry) with the signature decided up front:
`*_build(UiList *, const UiSnapshot *, const UiState *)`. Define
`UiSnapshot` in SDL-free `src/ui_snapshot.h`.
**Verify:** headless `ui_row()`/`ui_split_h()` asserts; harness links UI
`.o` files without SDL/SDL_ttf.

### Phase 0.5 — RejectReason conversion (shippable today, pre-funnel)
Convert `building_can_place()`'s dead `(char *reason, size_t)` channel to a
returned `RejectReason`; delete `set_reason()`; add the enum→string table
in ui.c; wire it into the HUD hover tooltip. Kills v1's bug #2, fixes the
enum's home before `sim_apply` exists to adopt it.
**Verify:** headless assert per placement-failure case returns the right
enum; tooltip shows it in-window.

### Phase 1 — Exchange screen rewrite (retires the cliff)
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

### Phase 2 — Data model
Unchanged from v1: `BuildingCategory` on `BuildingDef`, resource-category
table, designated initialisers everywhere (the `RES_COL` lesson).
**Verify:** headless assert every enum value has a non-default category.

### Phase 3 — HUD category tabs
Unchanged from v1 (HUD_HEIGHT 80 → ~112, 28px tab strip, sticky tab,
greyed-not-hidden unavailable buildings) with one upgrade: the hover
tooltip's "why can't I build this" now calls `sim_validate()` once the
funnel exists (Phase 0.5's enum until then).
**Verify:** as v1 (synthetic 40-entry def table, per-tab slot fit and
hit-test).

### Phase 4 — Vitals, inventory, overlay arbiter
As v1 (rule-driven vitals strip capped at 8 rows with `+k more`; inventory
overlay; `game_topmost_overlay()`; **fix the mouse-wheel bug**), plus: the
vitals rule engine reserves **sim-health rows** rendered by the same
alert machinery — last F9 result, tick-accumulator backlog, and (from M4)
feed age. The player is the monitoring system; a stall is visible seconds
after it starts.
**Verify:** as v1, plus a synthetic snapshot with a stalled accumulator
asserts the health row appears.

### Phase 5 — Island context
Unchanged from v1 (`‹ Island Name ›` header, chevrons over settled islands,
per-island hue, island name in overlay titles).

### Phase 6 — Confirm consolidation → command preview
v1's collapse of demolish/tier-upgrade/ship-build/build-confirm into one
`confirm_ui.c`, with a new job: the popup renders the *literal Command it
will submit* (kind, decoded payload, apply tick). The confirm layer and
the wire format become the same rendering code — screenshots become
forensics, and the UI cannot drift from what `sim_apply` receives.
**Verify:** hit-test results identical before/after; rendered preview
matches the submitted Command byte-for-byte in the headless harness.

### Phase M1 — with MMO Phase 1 (command funnel)
- UI wrappers emit Commands via `command_submit()`; pending ring
  (`{seq, anchor}`), rejection drain, `fx_reject.c` flash-at-anchor.
- Pending-vs-confirmed queued rendering (decision 5) for placements and
  trades.
- **INTENT lines in the `.smlog`**: mouse x/y, clicks/wheel/keys, and the
  exact `sim_tick_no` the frame's snapshot was taken at, interleaved with
  CMD lines.
- **CI UI replay**: the replay harness re-simulates to each intent's tick,
  takes the snapshot, drives the real `*_build` + `*_hit_test` with the
  evolving UiState, and asserts (a) the emitted Command is byte-identical
  to the next CMD line, (b) every rect lies inside 1920x1080. This is a
  full click-through UI regression suite on three OSes, in an environment
  with no xdotool. v1 built the purity; MMO_PLAN built the log; together
  they are this.
- Golden UiList diffs: serialise each frame's `UiList` (id, rect, label)
  to canonical text, diff against committed goldens — pixel-free visual
  regression for the "Prev button moved off-page at 27 goods" class.

### Phase M3 — with MMO Phase 3 (elastic market)
- `exchange_view_faction()`; refusal rendering (greyed cells + reason,
  reusing the existing unaffordable-buy greying path); "faction out of
  gold" message lands here.
- Limit-order price stamping + `REJ_PRICE_MOVED` flash.
- **Price-history sparkline column** (~48px per row): the faction keeps a
  small per-resource ring buffer of mid-price sampled every K ticks — sim
  state, in `sim_hash`, so replay covers it; `ExchangeView` carries a
  copy. Sell-Max leaves a visible scar that mean-reversion visibly heals —
  this is the mitigation for MMO_PLAN's "rigged slot machine" risk, and
  the Phase 3 debug/tuning overlay renders from the same buffer (the
  tuning UI and the player UI cannot disagree about the quote).

### Phase M4 — with MMO Phase 4 (shared feed)
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

### Phase M5 — with MMO Phase 5 (lockstep co-op)
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
| `src/fonts.c` | M4 | `TTF_Text` migration (scheduled) |
