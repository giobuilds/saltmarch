# Saltmarch accounts — the identity model

The design for authentication. Nothing here is built yet; SERVER.md's
"No authentication" is still true, and this is the plan for making it
false.

**Read [VISIBILITY.md](VISIBILITY.md) first if strangers are going to
play.** This document settles who a player *is*; that one asks what a
client is allowed to *know*, and it is the more consequential of the
two. Under lockstep every client receives every player's stockpiles,
buildings and ship cargoes, because it cannot run the simulation
without them — so a perfectly authenticated stranger still holds
everyone's books. Which of its options you choose decides what accounts
are even for.

## The problem, precisely

`host_assign_id()` (net.c) grants an identity to anyone who asks for
it:

```c
if (resume != PLAYER_NONE && resume != gs->local_player_id &&
    !id_connected(ns, resume) && id_owns_island(gs, resume))
    return resume;
```

Send `--as 3` and you are player 3, if player 3 owns an island and is
not currently connected. Ids are small integers from 1, so they are
enumerated rather than guessed. The only guard is "not currently
connected", so the window is exactly while a player is offline — and a
world that ticks while its players sleep is a world where that is most
of the time.

The ownership gates in `sim_apply` are sound and are not the problem.
They faithfully enforce `isl->owner == c->player_id`; nobody checks
that the connection is entitled to that `player_id`.

## Three layers, and the line between them

The single most important constraint, because getting it wrong leaks
every player's credentials at once:

```
account       handle + credential          SERVER-SIDE ONLY
    │                                      never hashed, never sent
    │ owns
player_id     owns islands and ships       WORLD STATE
    │                                      hashed, replayed, in snapshots
    │ presented as
display name  what the feed shows          COSMETIC
                                           self-asserted today
```

`player_id` must stay a small deterministic integer: `isl->owner` and
`sh->owner` are read by `sim_hash` and written into snapshots, and
that is correct.

**Nothing above that line may enter the snapshot.** `MSG_WORLD` sends
the snapshot to every joining client, so a credential stored there
would be handed to everyone on join. It must also stay out of
`sim_hash` (it is not world state, and hashing it would desync clients
that cannot know it) and out of the command log (which is replayed and
shared). Account state lives in a sidecar the host reads and never
transmits.

The display name is a third thing again. `feed_init` already derives an
id from a name taken from `$SALTMARCH_PLAYER` — a handle anybody can
assert. Under this model the account owns the name and the server
vouches for it, which is what stops one player appearing in the feed as
another.

## The credential: tokens before passwords

The instinct is username and password, because that is what a launcher
asks for. The design deliberately starts one step earlier, for a reason
worth writing down:

**A key derivation function exists to defend low-entropy human
secrets.** A 256-bit machine-generated token has no entropy problem, so
it needs no Argon2, no scrypt, no bcrypt — a plain hash at rest and a
constant-time comparison are enough. Which means the token design needs
**no cryptographic dependency at all**, and this project currently
links exactly SDL3, SDL3_ttf and `ws2_32`.

Human passwords change that in two ways at once. They need a real KDF —
and hand-rolling one is the classic way to get this quietly wrong. And
they must not cross a plaintext wire: a leaked token compromises one
world, while a leaked password is a credential the human has probably
reused elsewhere, so a hobby server becomes the source of somebody's
email breach. **Passwords imply TLS. Tokens do not.**

A launcher does not change this. A real MMO launcher does not send the
password to the game server: it authenticates against an auth service,
receives a *session token*, and hands that to the game client. The
launcher is how a human obtains a token — so the token mechanism is the
foundation of the account design, not an alternative to it.

## The model

**An account is a row**: id, display name, credential hash, salt,
created-at, and the `player_id` it owns. One account owns one
`player_id` per world. The table is `account → player_id[]` from the
start so that multiple characters is a later extension rather than a
migration, but the server grants exactly one.

**Registration** is trust-on-first-use by default: a client that
connects with no account is given one, and told its token in `WELCOME`.
That is today's behaviour plus a returned secret, which keeps a
friends-and-family server as easy to run as it is now. A public server
sets `--registration invite` and accounts are created by an admin.

**Resuming** replaces `--as N`. The client presents its account id and
token; the server verifies, then looks up the `player_id` that account
owns. `--as` stops being an assertion and becomes a lookup key —
which is the whole fix.

**Storage** is a sidecar beside the world: `world.smlog` +
`world.accounts`. That boundary now carries two jobs: it keeps
credentials out of a snapshot that is broadcast to every joiner, and it
is what makes erasure possible at all — see
[PRIVACY.md](PRIVACY.md). Line-oriented text with hex fields, on purpose: it is
an administrative surface read once at startup, not a hot path, and an
admin who has to ban or reset somebody at 2am should be able to do it
with an editor rather than a tool that does not exist yet. The
security-sensitive part is the credential, not the container.

**Co-op is unaffected.** A player hosting a friend with `--host` has no
account file and authentication stays off — the default is today's
behaviour. `saltmarch_host` enables it by having one.

## Phases

**Phase 1 — tokens.** `MSG_HELLO` carries `{proto, account_id,
token[32]}`; `WELCOME` returns a newly issued token on first join. The
sidecar, constant-time comparison, and rate limiting on failed
attempts (which the per-peer budget from the transport hardening
already has a place for). No dependencies. This alone closes the hole
SERVER.md names.

**Phase 2 — the client remembers.** There is no client-side config
today: the game reads argv and one environment variable, so a token
has nowhere to live. This phase adds one (`SDL_GetPrefPath`), which is
also where a server list and a display name belong. Without it every
player pastes a 64-character hex string on the command line, which is
not a design so much as a dare.

**Phase 3 — TLS.** The prerequisite for anything involving a human
password, and the answer to "the token crosses the wire in the clear".
A dependency decision, and the point at which the project stops having
two.

**Phase 4 — accounts proper, and a launcher.** Handles, passwords, a
KDF, registration and recovery. Only worth building if strangers are
going to play here; for a private world Phase 1 and 2 are the whole
answer.

## Invariants any implementation must hold

1. No credential in a snapshot, in `sim_hash`, or in the command log.
2. The sim learns nothing. This is entirely net.c and `saltmarch_host`,
   which is where the existing client/sim boundary already puts it.
3. Comparison is constant-time. A byte-at-a-time `memcmp` on a token
   is a timing oracle.
4. Failed attempts are rate-limited per connection and per account.
   Ids are enumerable and so are account ids.
5. Authentication is optional and off by default, or co-op regresses.
6. A refused login is refused at `HELLO`, before any identity is
   assigned and before `MSG_WORLD` is sent — an unauthenticated peer
   must never receive a snapshot.
7. No player-supplied free text reaches world state. A name in the
   command log is personal data in a replicated, replayed,
   checkpointed structure, and cannot be erased on request
   ([PRIVACY.md](PRIVACY.md)).

## Migration

Existing worlds have `player_id`s owning islands and no accounts at
all. This has to be explicit or people lose their islands: on first
run with `--accounts`, the host writes a file mapping each existing
`player_id` to a fresh account and prints the tokens once, for the
admin to distribute. Silent adoption — first caller to claim an id
gets it — would reintroduce exactly the hole this document exists to
close, on the one day it is most likely to be exploited.

## If strangers play

The plan above is shaped for a private world, and two of its choices
invert for a public one. Tokens-before-passwords assumes a player you
can hand a secret to out of band; a stranger who loses theirs has no
relationship with you through which to recover it, so human
credentials, a KDF and account recovery become requirements rather than
a later phase. And a WireGuard tunnel — the cheapest way to encrypt a
friends server — needs a key handed to each participant, which for
strangers *is* an account system with worse ergonomics.

So for a public server the order becomes: TLS in both binaries
(mbedTLS is the fit — small, CMake-native, permissive, and its
callback-based non-blocking model maps onto `peer_flush` /
`recv_into_buf`), Argon2id for passwords (libsodium's `crypto_pwhash`;
mbedTLS offers only PBKDF2), then accounts and a launcher. Not
OpenSSL — the largest API surface and the worst packaging story of the
three on Windows, where CI installs prebuilt release zips and
deliberately avoids vcpkg.

None of which addresses VISIBILITY.md. Decide that first.

## Non-goals

Cross-world accounts. Password recovery without TLS. Multiple
characters per account (the table shape allows it; nothing grants it).
Anti-cheat: `sim_apply`'s ownership gates remain the security model for
what a player may *do*, and authentication only settles who they are.
