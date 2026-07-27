# Personal data, and the shape the architecture has to keep

Written because strangers are going to play, which means EU residents
are going to play, which means the GDPR applies to whoever runs the
server. This is an engineering note about what that requires of the
code — not legal advice, and an operator taking payment or scaling up
should get some.

Read alongside [AUTH_PLAN.md](AUTH_PLAN.md) (who a player is) and
[VISIBILITY.md](VISIBILITY.md) (what a client may know). This one is
about what the *operator* holds and what they must be able to do with
it.

## Who is responsible

**The operator of a `saltmarch_host` is the data controller, not this
project.** Anyone can run one; each instance decides its own retention,
its own registration policy, its own users. What the repository owes is
therefore not compliance but *capability*: an operator must be able to
export a player's data, erase it, and know what is held where —
without patching the game to do it.

Anything below phrased as a requirement is a requirement on the
software, so that the operator's obligations are possible to meet.

## What would exist, and where

Nothing in this list exists today; it is what AUTH_PLAN.md would
create.

| Data | Where | Personal data? |
|---|---|---|
| Display name | accounts sidecar, and published in the feed | Yes — a pseudonym |
| Credential hash + salt | accounts sidecar | Yes |
| Email, if recovery is offered | accounts sidecar | Yes |
| `account → player_id` | accounts sidecar | Yes — it is the link |
| `player_id` | world state: `isl->owner`, every `Command` | Pseudonymous |
| Islands, buildings, trades, voyages | world state, command log, checkpoints | Pseudonymous behaviour |
| Voyage lines (name, lanes, cargo) | `feed_out.jsonl`, synced to peers | Yes — published |
| Connection addresses | **nowhere** | — |

That last row is worth dwelling on. `accept(ns->listen_fd, NULL, NULL)`
discards the peer address, and nothing anywhere captures or logs an IP.
That is data minimisation by construction, and it should be treated as
a property to defend rather than an omission to fix.

## The one real tension, and why it resolves

**Erasure (Art. 17) against a deterministic append-only log.**

The world is a pure function of `(seed, ordered command log)`, every
command carries a `player_id`, and checkpoints record `isl->owner`.
Deleting a player's commands is not an option: it would change the
world, break every replay, invalidate every checkpoint, and take other
players' economies with it.

It resolves because **the world holds no identities, only pseudonyms.**
`player_id` is an integer. The only thing that connects it to a human
is the account row — which AUTH_PLAN.md already puts in a sidecar that
never enters the snapshot, never enters `sim_hash`, and is never
transmitted.

So erasure is: **delete the account row.** Name, credential, email and
the mapping go. `player_id 7` keeps its islands and its history, now
referring to nobody, and is genuinely unlinkable rather than merely
obscured. The command log is untouched, determinism is untouched, and
every checkpoint and replay still verifies.

Two consequences follow, and both are load-bearing:

**World backups carry no obligation.** They contain pseudonyms only, so
an erasure request does not have to reach them. Only backups of the
accounts sidecar must be purged. That is a large simplification, and it
is a direct dividend of a boundary already drawn for security reasons —
the same line, holding for a second reason.

**No player-supplied free text may ever enter world state.** This is
the invariant to write down before somebody adds it in good faith. Ship
names, island renaming, chat, notes on a warehouse: any of them puts
unerasable personal data into a replicated, replayed, checkpointed log,
and turns a one-line erasure into an impossible one. Island names come
from a fixed table today. **Keep it that way.** If in-game text is ever
wanted, it must live beside the world like the account does, keyed by
`player_id`, and be deletable independently.

## Data minimisation argues for tokens

AUTH_PLAN.md prefers machine tokens to passwords on cryptographic
grounds. The privacy argument points the same way and is worth stating
separately: **token authentication needs no email, and need not require
a real name or any name at all.** The minimum viable account is an id
and a hash.

Passwords bring account recovery, recovery in practice needs email, and
email is personal data with a lawful basis, a retention period and a
breach exposure attached. That is a legitimate trade for a public
server — but it should be made deliberately, not arrived at because
passwords felt normal.

## What the software must provide

1. **Export** (Art. 15, 20). One command producing everything held
   about an account: the row, its `player_id`, and that player's
   islands and commands, in a portable format. `saltmarch_host
   --account export <id>`.
2. **Erasure** (Art. 17). Deletes the account row and any published
   feed lines the operator still controls. Leaves the world intact.
   `--account erase <id>`.
3. **Rectification** (Art. 16). Change a display name without touching
   the world.
4. **Retention.** If addresses are ever captured, a configured maximum
   age and an automatic purge. Not a policy in a document — a timer in
   the code.
5. **Security** (Art. 32). TLS in transit, which VISIBILITY.md and the
   strangers decision already require; Argon2id at rest if passwords
   arrive; constant-time comparison; the accounts sidecar readable only
   by the server user.
6. **Breach detectability** (Art. 33). Failed-authentication counts
   and their timing, so an operator can notice a credential-stuffing
   run inside 72 hours. Aggregate counters, not a log of who tried
   what.

## If addresses are ever captured

A public server will eventually want per-IP rate limiting or bans, and
that creates personal data where none exists now. If it happens:
pseudonymise at the boundary — rate-limit against a keyed hash of the
address with a rotating salt, keep the raw address only in memory for
the life of the connection, never write one to a log file, and set a
retention period for whatever survives. The current per-peer budget
from the transport hardening already does most of what rate limiting
needs without knowing an address at all.

## The feed is publication

`feed_out.jsonl` carries a display name, lanes and cargo, and is synced
to other players. That is disclosure to third parties, and it needs a
lawful basis and a mention in the operator's privacy notice.

It also has a limit worth being honest about: once a line has synced to
other players' machines, erasure cannot recall it, any more than any
distributed system can. The mitigations are the ordinary ones — publish
the minimum, let a player opt out of the feed entirely, and prefer a
pseudonym to a name. The feed already clamps names and treats inbound
lines as untrusted; opting out is the piece it lacks.

## Things that would break this

- Putting the account table, or any part of it, inside the snapshot.
  It is broadcast to every joining client. This is already forbidden
  for security; it would also be an unbounded disclosure.
- Hashing any identity into `sim_hash`. It would make the credential
  world state, and world state is replicated to everyone.
- Free text from players in the command log. See above; this is the
  one that will look harmless.
- Logging peer addresses "for debugging". It creates a retention
  obligation and a breach surface that does not currently exist.
