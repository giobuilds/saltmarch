# What a client is allowed to know

A design note, prompted by a single new fact: **strangers will play
this game.** Everything below was fine while "multiplayer" meant a
friend you invited, and stops being fine the moment it doesn't.

This is not about authentication. AUTH_PLAN.md settles *who you are*.
This settles *what you can see*, and no amount of the former fixes the
latter.

## What every client receives today

Two lines of the transport decide it. On join, `snapshot_encode`
writes the whole archipelago:

```c
w_i32(&w, (int32_t)MAX_ISLANDS);
for (i = 0; i < MAX_ISLANDS; i++) put_island(&w, &gs->islands[i]);
```

and thereafter every command from every player reaches every peer:

```c
if (!command_log_append(gs, &stamped)) return 0;
broadcast(ns, MSG_CMD, &stamped, (uint32_t)sizeof(stamped));
```

So a connected client holds, for **every** player: island stockpiles
and escrow, every building and its production timer, population and
agents, and every ship's owner, cargo, origin, destination and
`departure_tick`. Plus the faction's inventory, its live prices, and
its per-lane insurance premiums — a map of where ships have been lost.

And it holds the future: commands are stamped
`NET_CMD_DELAY_TICKS` ahead, so a client sees a rival's order *before
it applies*.

## This is inherent, not an oversight

Lockstep means every client runs the identical deterministic sim.
Identical outputs require identical inputs — that is the whole
mechanism. A server that sent each client only its own business would
be sending different inputs to each, and they would compute different
worlds. The desync detector would fire immediately and correctly.

So the data is not being leaked by accident. **It is being sent because
the client cannot run the simulation without it.** Filtering is not a
patch to this architecture; it is a different architecture.

It is worth being precise about what the existing claim covers.
SERVER.md says *"Privacy is validation"*, and that is true as written:
`sim_apply` refuses commands against islands you do not own, so nobody
can *act* on your island. It says nothing about reading, and was never
meant to.

## What that costs among strangers

Three mechanics turn total knowledge directly into advantage:

**Interception.** `CMD_INTERCEPT` takes a target ship and its exact
departure tick — the tick binds the command to one specific voyage.
Both numbers are in every client's copy of the world. With full
knowledge, choosing which convoy to raid stops being a judgement and
becomes a query: sort every rival voyage by cargo value, intercept the
best one. The mechanic was designed for a world where finding a target
was part of the risk.

**The market.** Faction prices move elastically with its inventory, and
every buy and sell crosses the wire stamped four ticks early. A client
that watches the command stream can front-run a large sale it has been
told is coming.

**Everything else.** Which island is rich, which is undefended, which
warehouse is full, which colony is about to lapse its charter. In a
trading game, most of the game is knowing that.

None of this needs a cheat engine. The data arrives, correct and
complete, at every client; reading it requires only a modified build,
and there is no way to detect one that has been modified only to
*look*.

## What authentication does and does not fix

AUTH_PLAN.md is still worth building — it stops one player *being*
another, which is a real hole. But a perfectly authenticated stranger
still receives everyone's books. TLS encrypts the link against third
parties and delivers the same payload to the endpoint. Accounts,
Argon2, a launcher: none of them touch this.

Any design that ends with "and then we authenticate the client" has not
addressed it, because the client is the adversary.

## The options, honestly costed

**A. Accept it. Design for open books.**
Make total visibility a rule of the game rather than a leak: everyone
sees every ledger, and the skill is in what you do about it. Some
trading games work exactly this way. Costs nothing, changes no code,
and rebalances the mechanics that assumed concealment — interception
especially, which becomes a contest of timing and escorts rather than
of discovery. This is the only option that keeps the architecture
whole.

**B. Server-authoritative simulation with per-client views.**
The server owns the world; clients render what they are told and send
input. This is what most MMOs do and it is the only complete fix.

It also removes the foundation the rest of the project is built on.
Determinism stops being a client property, so client-side prediction
has to be built from scratch; the F9 self-check no longer describes
anything a client can verify; the snapshot format stops being a world
and becomes a per-recipient projection; the replay harness, the
scrubber and the UI-click replay all lose their meaning on the client
side. MMO_PLAN chose lockstep deliberately and got determinism, cheap
replay, verifiable checkpoints and a tiny protocol in exchange. This
option trades all of it back.

Not "a big change" — a different program that shares some art.

**C. Two multiplayer models, which the tree already has.**
`saltmarch_host` is the *invited* world: lockstep, full replication,
full trust — precisely right for people who know each other.

For strangers, use the model in feed.c. Every client runs its own
world, and only coarse public events cross between them: a departure,
its lane, a name. The feed is already built on exactly the right
principles — it lives in `App` rather than `GameState`, never enters
`sim_hash`, renders other players as non-interactive ghosts, and treats
every inbound line as untrusted. It leaks nothing because it carries
nothing worth stealing.

A stranger-facing service would extend that, not lockstep: a
server-mediated market and a shared feed, with each player's island
private because it is genuinely on their own machine. Direct piracy
between strangers is the casualty — you cannot intercept a ship in a
world you are not in — and that is the honest cost.

**D. Shards.** Strangers grouped into small lockstep worlds. Does
nothing about visibility *within* a shard; it only bounds how many
people see your books. A scaling answer to a trust question.

## Recommendation

**Decide this before building accounts**, because it decides what
accounts are for.

For a public game I would take **C**, and keep **A** in reserve if
direct player-versus-player piracy turns out to be the point of the
game. C keeps everything the architecture is good at, uses a model
already in the codebase, and gives strangers a shared ocean without
giving them each other's ledgers. B is defensible only if
server-authoritative play is the actual goal, and in that case it
should be chosen deliberately and early rather than arrived at by
patching.

What I would not do is ship lockstep to strangers and treat the
visibility as an acceptable risk. It is not a risk; it is a certainty,
undetectable, and it lands hardest on exactly the mechanics — piracy,
insurance, the market — that the last several phases were spent
building.

## What does not work

Worth writing down so it is not proposed later:

- **Encrypting the snapshot per client.** The client must decrypt it to
  simulate. The key is on the adversary's machine.
- **Obfuscating the format.** `snapshot.c` is explicit and documented
  precisely so it can be reasoned about; a scrambled encoding is a
  speed bump, not a boundary.
- **Trusting a signed client.** The binary runs on their hardware.
- **Filtering only the snapshot but still broadcasting commands.** The
  commands reconstruct the world; that is the definition of the log.
