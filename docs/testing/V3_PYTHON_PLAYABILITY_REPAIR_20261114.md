# V3 Python Playability Repair — 2026-11-14

## Scope and result

Started from `05de7ede59375c935aed6b1ae442e7af54010e33` on `v3-development`.
This pass targeted the 54 planet/landing failures first, then trading, ship
roster, navigation, combat and hazards. The prior baseline was 716 passed and
83 failed (799 tests). The final run was **802 passed, 0 failed**: the original
799 tests plus three reconnect regression tests. No beta, production or host
database was used.

## Root-cause groups

| Group | Finding | Treatment |
|---|---|---|
| Planet/landing fixtures | Positive landing cases omitted a required typed sector refresh, omitted active `player.ship_id`, or treated `ship.status.onplanet` as a planet ID instead of its V3 0/1 flag. Some fixtures also used untyped/malformed responses as positive snapshots. | Corrected only positive fixtures to match V3 server responses. Malformed, wrong-type, cross-player and identity-mismatch tests remain fail-closed. |
| Planet/landing client | Landing state could not be restored after reconnect because `player.my_info` omitted persisted `lastplanet`. Ship identity checks also had to use the identity refreshed from `player.my_info`, rather than adopting a possibly mismatched ship response. | Added `last_planet_id` to the V3 player response; correlated player, ship, planet and sector snapshots before reopening SURFACE. Added three focused reconnect tests. |
| Trading | Port fixtures lacked `trade.port_info`; several success fixtures omitted the logged-in player identity or contradicted it in the receipt. Old expected direction labels and an exception diagnostic no longer matched the client contract. | Added canonical types/IDs to valid fixtures, retained identity and receipt validation, and split receipt type/line/player diagnostics so failures identify the specific rejected contract. |
| Ship roster | Tests expected no recovery read after ambiguous operations, used noncanonical player snapshots, or treated an empty roster as sufficient proof that a player had no active ship. A malformed reply triggered an unnecessary second roster query. | Reconciled tests with authoritative recovery; empty roster now requires a typed active-ship check. Malformed roster replies recover via `ship.status` without reissuing `ship.list`. |
| Navigation, combat and hazards | Several fixtures used pre-contract transwarp fields, omitted `transwarp_complete`, omitted typed error envelopes, or asserted raw pre-normalized sector dictionaries. A combat scenario used FedSpace, where player attacks are prohibited. | Corrected the canonical fixtures and asserted normalized authoritative state. Kept FedSpace attack restrictions and refresh validation intact. |

The test-only corrections fixed stale protocol expectations; they are not
evidence that every trade, combat, planet or ship lifecycle operation works at
runtime.

## Runtime journey

Disposable resources were isolated as:

- Image: `twclone-planettriage-20261114`
- PostgreSQL container: `twclone-planettriage-db-20261114`
- V3 server container: `twclone-planettriage-game-20261114`
- Private network: `twclone-planettriage-net-20261114`
- Local-only server port: `127.0.0.1:12487`

The ordinary Python client registered `PlanetJourney1794651252`, landed on
Terra in sector 1 and displayed the planet overview. After logout, another
ordinary client login restored the surface menu from server state; the player
then launched and returned to the main menu. A separate pre-rebuild landed
account (`PlanetTriagePlayer`) had a read-only SQL snapshot confirming
`lastplanet=1` and `onplanet=true`; the completed journey account's final SQL
snapshot confirmed departure (`lastplanet=NULL`, `onplanet=false`). Transcripts:

- `/tmp/twclone-planet-landing-triage-20261114/landing-surface-departure-client.txt`
- `/tmp/twclone-planet-landing-triage-20261114/reconnect-surface-departure-client.txt`
- Original protocol capture: `/tmp/twclone-planet-landing-triage-20261114/land-client.txt`

The first transcript ends at the post-landing surface prompt because the
automation expected a menu prompt at the wrong boundary; the second transcript
is the successful reconnect, surface and departure journey. Database checks
before departure showed `players.lastplanet=1` and the active ship's
`onplanet=true`. After departure, SQL showed `lastplanet IS NULL`,
`onplanet=false`, sector 1, active ship 40 and 119 turns. This was read-only
verification against the disposable database.

## Verification

Executed:

```text
python3 -m pytest -q client/python_client/tests --tb=no
802 passed in 2.17s
```

The actual V3 Docker image built successfully with PostgreSQL support from the
working source. Python tests ran against the local test doubles; the live
ordinary-player journey used the actual Python client, server and isolated
PostgreSQL database. No complete trading or combat journey was run against the
live server in this pass. Godot runtime remains unverified.

## Remaining ordinary-player blockers

1. Full live two-player combat, retreat, destruction, escape-pod recovery and
   subsequent ship replacement have not been demonstrated in this pass.
2. Planet colonisation, worker production, harvesting, citadel lifecycle and
   PTA have not been demonstrated in this pass. Fresh-world production rules
   remain a separate approved-contract issue.
3. Purchase, sale, repair and replacement must still be demonstrated as a
   complete live lifecycle through the ordinary Python client.
4. Corporation invitations, permissions, treasury and transfers need an
   actual Python-client end-to-end run; database acceptance evidence for member
   asset transfers does not prove client playability.
5. Toll fighters, photon missiles, StarPort construction/capacity upgrades
   and StarPort combat/destruction remain original-TW2002 parity gaps.
6. Godot runtime remains unverified.

The V3 playability tracker remains conservative; these client-test results do
not promote server parity rows to `VERIFIED`. Beta readiness is not established.
