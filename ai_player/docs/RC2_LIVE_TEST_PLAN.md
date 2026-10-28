# RC2 existing-player live qualification plan

Status: prepared, not executed. Run only after the stakeholder confirms the RC2
QA database and game server are available. Preserve player
`ai_rc2_qa_35193b2ad4`; never register an account or change NPC settings.

## Environment and privacy gate

1. Read the approved RC2 host, game port, database identity, and server process
   from current stakeholder confirmation and local runtime configuration.
   Verify database identity read-only and verify the game endpoint. Do not
   start, stop, restart, or reconfigure a service.
2. Use the existing player's credential from the protected QA credential file.
   Run with a temporary config/state/log directory restricted to mode `0700`
   and files `0600`. Set `register_if_missing: false`; use a separate AI state
   file so the verified 47-warp evidence and normal saved state are preserved.
   Never print credentials or session tokens.
3. Use the ordinary AI controller and public command envelope
   `{id, command, data, meta}`. Put the session token only in the authenticated
   command metadata used by the existing client. Use public command responses
   and events; do not read or write gameplay records in the database.

## Deterministic selection and trade sequence

All selection is based on fresh public reads in this run. Quantity is one.
Tie-break candidates by `(source_sector_id, destination_sector_id, port_id,
commodity)` ascending so the same observed market snapshot produces the same
choice. If no valid candidate exists, do not force a speculative purchase;
record the public refusal/market state and mark trading unqualified.

1. Send `auth.login` for the existing account. Keep the returned session token
   in memory only. Then send `player.my_info` and `ship.info`. Save sanitized
   authoritative initial values: player ID, sector, ship ID, credits, cargo,
   and turns.
2. Send `sector.info` for the current sector. If it has a port, inspect it with
   `trade.port_info` using the current public contract, and quote one unit of
   each reported commodity using `trade.quote` (`port_id`, `commodity`,
   `quantity: 1`). Use the same public discovery commands while surveying
   reachable sectors through the existing AI controller.
3. Select the first source/destination/commodity candidate for which fresh
   public quotes show a positive player purchase price at the source and a
   positive player sale price at the destination, the source cost is within
   available credits, and the ship has a free hold. Prefer positive gross
   margin. A failed eligibility check is a skipped candidate, not a defect.
4. Seed only the isolated controller state with the selected goal sequence:
   `goto: <source>`, `buy: <commodity>`, `goto: <destination>`,
   `sell: <commodity>`. Let the existing controller choose and issue the
   public commands, navigate, and advance goals. Do not manually issue its
   movement or trade commands.
5. Immediately before `trade.buy`, capture `player.my_info` and `ship.info`.
   Execute a one-unit buy at the current port with account `0` (ordinary
   personal credits), the current sector and port IDs, and a fresh unique
   idempotency key. After the `trade.buy_receipt_v1`, request both authoritative
   reads again. Verify cargo quantity increased by one and credits decreased by
   the receipt's charged total (including any fees); record the observed turn
   delta. The server's trade command consumes one turn on a successful trade.
6. Let the controller navigate to the selected destination. Refresh the
   destination's port information and quote for the carried commodity. Before
   `trade.sell`, capture both authoritative reads. Execute the one-unit sale
   with account `0`, current sector/port IDs, and a new idempotency key. After
   `trade.sell_receipt_v1`, refresh both reads. Verify cargo quantity decreased
   by one and credits increased by the receipt's net proceeds; record the turn
   delta. Do not call a refused sale a defect unless authoritative state
   contradicts the refusal or a successful receipt fails its expected effects.
7. Confirm the controller removed each completed goal from the isolated goal
   sequence only after the corresponding existing purchase/sale receipt, and
   finished the full four-goal route without manual gameplay commands.

## Disconnect, reauthentication, and persistence

After the sale and final authoritative reads, close the client's game socket
without sending a logout command. Create a new TCP connection to the same
confirmed server, authenticate again with `auth.login`, then fetch
`player.my_info` and `ship.info`. Compare player ID, sector, credits, cargo,
ship ID, and turns with the post-sale authoritative snapshot. A fresh login
token is held only in memory. Any difference is recorded with both snapshots,
the sanitized command/response sequence, and relevant event IDs.

## Evidence and stop rules

For each action, retain sanitized command name/data, response type/status,
expected and actual outcome, state-before/state-after, turn delta, relevant
event IDs, and AI goal progress. Redact password/session/token/authorization
fields before writing JSONL or summaries. Mark ordinary refusals (for example,
unavailable stock, invalid route, insufficient credits, or turns) separately
from successful-action inconsistencies. Use the existing `BugReporter` only
for reproducible inconsistencies, with severity/confidence and exact replay
sequence.

Stop live testing if the confirmed database identity or server endpoint differs,
the service becomes unavailable, or the public command responses indicate an
unexpected account/permission condition. Do not retry a timed-out trade with a
new idempotency key until fresh state and trade history reconcile whether the
original action committed.

## 2026-10-27 execution attempt

The stakeholder-provided live endpoint `192.168.25.5:1234` accepted a TCP
connection. The local `bin/server` SHA-256 matched the supplied candidate hash
`545f89dbc10a324ceb670f88cada971313d98b81d58d0df6ecbb8b59486a9b1d`. This
confirmed binary identity locally, but did not establish that the isolated QA
account is authorized in live database `twclone`.

Repository/config review found the existing ordinary identity
`ai_rc2_qa_35193b2ad4`, which was created for the isolated QA database; it did
not establish a live account. Automatic approval review rejected sending that
protected account credential to the live endpoint until live authorization was
confirmed. No credential was sent and no live login or gameplay command ran.
The existing isolated PostgreSQL check returned `no response` on
`127.0.0.1:55992`; its database and server processes were absent. No account,
database, universe, or service was created or changed. The previously verified
47 isolated-universe warps remain the only gameplay evidence available. The AI
regression suite passed 49 tests, and the retained QA logs scanned in this run
contained no unredacted sensitive fields.

To resume, confirm that `ai_rc2_qa_35193b2ad4` is authorized for login on
`192.168.25.5:1234` and that its protected credential may be used there, or
provide another already-authorized ordinary QA identity. No additional account
is needed or requested.

## Live `ai_qa_bot` check

The stakeholder subsequently confirmed the long-standing live bot identity.
Using PostgreSQL at `192.168.25.5:5432` in explicit read-only transactions, the
database identity was verified as `twclone` / `postgres`. Schema inspection
preceded row queries; no password or session field was selected. Player 4 is
`ai_qa_bot`, type value 3, `is_npc=true`, sector 1, ship 4, credits 5,000, and
not destroyed. Ship 4 is `Used Scout Marauder` (type 2) in sector 1, with 10
holds, one fighter, one shield, and no cargo. The `player_types` table contains
only types 1 and 2, so player 4's type value also has no matching description.

The existing credential source is `ai_player/config.json`, whose configured
username matches `ai_qa_bot` and contains a password value. Its current file
mode is `0775`, so it is not a protected credential store. The configured
credential was tried only through public `auth.login` after the live row was
verified. The server returned error 1101, `Invalid login`; no session was
issued. Source inspection explains the exact restriction: `play_login` checks
`is_npc` and returns `ERR_IS_NPC` before comparing the supplied password;
`cmd_auth_login` maps every non-`AUTH_OK` return to public error 1101. Therefore
the public response masks the NPC restriction and cannot establish whether the
configured password itself matches. No session was issued and no further auth
attempts or gameplay commands were made.

Live account discovery: PASS. NPC public login: FAIL by the server's explicit
NPC restriction (exposed as generic error 1101). Credential match: NOT
VERIFIABLE through this protocol for an NPC account. Autonomous
navigation/trading/state/reconnect acceptance: BLOCKED. No combat or database
mutation occurred. This bot cannot be used for player acceptance without
changing the prohibited NPC restriction; no such change was made.
