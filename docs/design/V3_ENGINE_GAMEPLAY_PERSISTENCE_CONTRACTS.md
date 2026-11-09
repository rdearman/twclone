# V3 Engine, Database, and Gameplay Foundations

Status: approved contracts; migration 115 implements XP and global tax persistence foundations; NPC negotiation remains design-only.
Audit date: 2026-09-29.

This document records the MySQL runtime gap and gameplay decisions for issues
#407, #288, #270, #277, and #312. It is intended for review by the
database/engine owner and Codex #2 before server/API contracts are committed.

## 1. MySQL runtime status

### Implemented at the DB API layer

`src/db/mysql/db_mysql.c` is a real optional driver, not an empty stub. When
configured with `--enable-mysql`, it provides:

- connection open/close and child-process cleanup;
- per-call connection serialization, UTC session time zone, and `utf8mb4`;
- `$N` positional parameter rewriting and native prepared statements;
- exec, affected-row counts, auto-increment insert IDs, and buffered query
  results;
- typed accessors for signed/unsigned integers, booleans, doubles, text, and
  blobs, plus NULL handling;
- outer transaction begin/commit/rollback through the shared DB wrapper and
  backend error classification.

The isolated `tests/mysql/test_db_mysql.c` suite exercises DB API connection,
binding, reads, NULLs, generated IDs, commit, and rollback. It does not exercise
the game server, application repositories, migrations, or gameplay.

Known DB API gaps visible in the driver:

- `ship_repair_atomic` returns `ERR_NOT_IMPLEMENTED`.
- PostgreSQL `RETURNING` is not translated. `exec_insert_id` returns the
  MySQL auto-increment ID and ignores the requested `id_col`; it does not
  implement arbitrary returned-row semantics.
- The `RETURNING` detection is a case-sensitive substring check, so it is not
  a SQL parser and must not be treated as a compatibility layer.
- `DB_TX_IMMEDIATE` has no MySQL-specific behavior. Shared nested transactions
  are flat nesting, not savepoints; nested rollback aborts the outer unit.
- Driver behavior is not certified against both Oracle MySQL and MariaDB, and
  same-handle multi-thread transaction isolation has not been demonstrated.

### Runtime activation blockers

- `src/game_db.c` caches only the PostgreSQL connection string and creates
  thread-local PostgreSQL handles. No MySQL runtime configuration is loaded.
- `src/server_main.c` rejects every backend except PostgreSQL before normal
  server operation.
- `bigbang` is built from `bigbang_pg_main.c` and is explicitly PostgreSQL
  only. It cannot provision a fresh MySQL game database.
- `sql/my` currently contains migrations 098–108. PostgreSQL migrations
  109–114 have no MySQL counterparts, so the MySQL schema does not represent
  the V2 feature set required by the current server.
- A static scan found PostgreSQL-specific constructs in ten repository
  modules, including `RETURNING`, `ON CONFLICT`, casts, and PostgreSQL
  expressions. `sql_driver` has a few dialect helpers, but it is not a complete
  repository dialect abstraction; unsupported conflict helpers fail fast.
- MySQL integration tests cover the low-level DB API only. No disposable
  full-server fresh-install, migration-upgrade, S2S, cron, or gameplay test
  currently certifies MySQL operation.

### Completion plan and gates

1. **Set supported server versions and scope.** Decide whether V3 promises
   Oracle MySQL, MariaDB, or both, including minimum versions. Keep PostgreSQL
   as the unchanged reference backend.
2. **Activate runtime configuration.** Add a backend selector and validated
   MySQL connection fields to startup configuration, preferably environment
   overrides for secrets. Thread the selected config through `game_db_init`
   and thread-local connection creation. Preserve PostgreSQL defaults and
   fail clearly for incomplete/unsupported settings. Add startup tests that
   prove a MySQL selection reaches the MySQL driver and PostgreSQL selection
   remains unchanged.
3. **Define DB API conformance semantics.** Specify generated-ID behavior,
   `exec_returning`, NULL/type/overflow behavior, row counts, transaction
   nesting, cancellation, connection loss, and fork/thread lifecycle. Resolve
   `ship_repair_atomic` using row locks and a transaction or explicitly keep
   that server operation unsupported until the repository is rewritten.
4. **Inventory and convert application SQL.** Classify each repository query
   and stored procedure by dialect feature. Prefer portable SQL where it has
   identical semantics; otherwise add narrow backend-specific functions with
   paired tests. Do not build a global text-rewrite layer for SQL.
5. **Add schema and migration parity.** Implement fresh-install schema and
   upgrade migrations for V2 objects 109–114 before claiming whole-server
   support. Add tracked migration IDs/checksums and explicit fresh/upgrade
   modes; MySQL DDL can auto-commit, so preflight and resumable step reporting
   must not promise PostgreSQL-style rollback.
6. **Build backend conformance infrastructure.** Run the same DB API and
   repository behavior suites on isolated PostgreSQL and the chosen MySQL
   service(s). Add migration fresh install, upgrade fixture, replay/drift,
   data preservation, and information-schema assertions.
7. **Pass full application validation.** Start the server and engine on the
   migrated database; test authenticated login, S2S hello/ack, database-backed
   cron, critical repositories, and restart/reconnect. Do not mark MySQL
   supported before this gate passes.

## 2. Gameplay design contracts

All gameplay changes remain DB-authoritative. The engine owns deterministic
scheduled calculations and persisted outcomes. The server owns player
commands, authentication/authorization, and wire contracts. Codex #2 should
review command names, request/response fields, and event delivery before any
server/API implementation begins.

### #407 Corporate systems epic decomposition

Current foundations include `corporations`, `corp_members`, `corp_log`,
`corp_mail`, corporate accounts/transactions, public stocks and dividends,
plus daily corporate taxation. These systems are separate implementations;
they do not satisfy the entire epic.

Live child issues already cover:

- **#410 corporate votes:** proposals, eligibility, votes, tally/result,
  durable outcome, and a major-decision event/news record.
- **#411 planetary bonuses/auto-defence:** policy and credit-rating inputs,
  production-time calculation, and defence integration. This must use the
  existing planet production and fighter/defence paths, not create another
  combat or production loop.
- **#412 stock crashes:** deterministic/tunable trigger and magnitude,
  consistent share/holding valuation, audit history, and news.
- **#413 insider-trading penalties:** tunable suspicious-trade input,
  idempotent penalty, existing fine/alignment/bans path, and audit record.
- **#414 local planetary politics:** mayor/election and planet policy voting.
  This crosses into planet ownership and tax policy and should be separated
  from corporation governance or explicitly retained as a distinct child.

The epic still needs bounded stories for corporate HQ lifecycle and diplomacy,
and likely a history/scoreboard story if that scope is retained. Decomposition
should make each child independently acceptable and specify its persistence,
authorization, event, and test contracts. Existing stock work in #412/#413
must not be recreated under the epic. #410 should own corporate votes; #414
should own local planet elections/policy unless the stakeholder chooses a
single shared voting model.

### #288 Periodic colony taxation

**Current state:** V2 migration 112 created per-planet `planet_tax_policies`,
`planet_economic_activity`, and daily `planet_tax_assessments`. The cron
assesses captured economic activity by UTC date, prevents repeat assessment by
planet/date, credits player owners, and records corporate receipts through
`corp_tx`. Server market paths record some planet activity. This is distinct
from `h_daily_corp_tax`, which currently applies a fixed corporate treasury
tax.

**Stakeholder decision:** Tax is assessed daily. Independent planet receipts
belong to the planet owner; corporate planet receipts belong to the
corporation. All defined economic activity is taxable. Use global defaults
initially; planet/corporation policy overrides are a later extension.

**Remaining gaps:** V2 activity capture needs coverage proven for all defined
taxable activity. There is no planet morale system. Keep work in the existing
daily assessment path; do not add morale effects or per-planet policy commands
without an explicit contract.

**Final acceptance criteria:**

1. One UTC daily assessment taxes all defined economic activity recorded for
   the assessment date using global defaults.
2. Independent planet proceeds go to the planet owner; corporate planet
   proceeds go to the corporation.
3. The assessed rate, taxable base, destination owner, and resulting amount
   are auditable for each planet/date.
4. Retries and repeated daily cron execution produce exactly one assessment
   and one receipt per applicable planet/date.
5. No player/corporation policy override is required in this scope.

**Database changes required:** Migration 115 adds singleton
`planet_tax_global_config` with a disabled 0-bps default. Reuse
`planet_economic_activity` and
`planet_tax_assessments`; use the existing `config` key/value store for global
rate/enabled defaults only if operators need non-typed additional settings.
The daily repository path reads this global row; legacy per-planet policy rows
are retained but are not applied. Extend assessment/receipt persistence only
if it cannot record the applied global rate, base, and destination owner.
Ensure activity capture covers every defined economic activity and commit
assessment plus receipt atomically/idempotently. Do not add planet/corporation
policy overrides in this phase.

**API changes required:** No player tax-policy write command. Server/API work
must preserve existing owner statement/transaction visibility for player and
corporation receipts; add a read projection only if existing statement APIs do
not expose the receipt and assessment details needed by owners. Taxable
activity event/persistence inputs require a stable server-to-engine contract.
Coordinate those event and read contracts with Codex #2.

**Test plan:** Test global-default lookup and fallback, independent and
corporate ownership routing, every taxable activity source, UTC day boundary,
zero/disabled configuration, repeat execution, retry after transaction
failure, and audit data. Assert that daily activity is charged once and the
owner ledger gets one matching receipt.

### #270 NPC trader negotiation

**Stakeholder decision:** There is no traditional player haggling at ports.
Negotiation is only between a player and an NPC trader. Outcomes use player
reputation, market conditions, and trader personality. Reuse existing NPC
trader, reputation, and market systems; do not add a parallel reputation or
market model.

**Acceptance criteria:**

1. Only an eligible NPC interaction negotiates; normal player-port trades do
not gain a haggle command or price modifier.
2. Fixed reputation, market state, personality, and negotiation input produce
a deterministic offer/outcome.
3. Tests cover input boundaries, each personality, insufficient stock/funds,
invalid context, and duplicate settlement.
4. Settlement uses existing trade/deal persistence and updates credits,
inventory, and trade history exactly once.

**Database changes required:** Reuse NPC relationship/reputation, trader
personality, market-condition, inventory, and existing trade/deal persistence.
Add a negotiation outcome record only if those existing tables cannot persist
the unique request key, agreed offer, and settled result needed for retries or
audit. No parallel reputation or market schema.

**API changes required:** No player-to-port haggling command or modifier. If no
NPC negotiation operation exists, add a server-owned request/response contract
scoped to an eligible NPC interaction. It must carry a request ID and trader
identity, return the negotiated terms/outcome, and settle through the existing
trade/deal path. Coordinate its event and response shape with Codex #2.

**Test plan:** Deterministic tests vary reputation, personality, and market
conditions independently and at boundaries; cover invalid interaction,
stock/credit limits, duplicate request settlement, persistence/restart, and
confirm ordinary player-port trades do not negotiate.

### #277 Experience, rank, and title progression

**Stakeholder decision:** XP is stored permanently and determines trader
rank/title. Alignment selects the good or evil title path. XP does not affect
damage, ship statistics, or RPG attributes; there are no skill points, trees,
or character classes. Only existing TradeWars-style rank privileges may be
used.

**Existing rank/title table — preserve exactly:** `commission`, seeded in
`sql/pg/091_seed_essential.sql`, is the authoritative good/evil title table
for this work. Preserve every existing ID, name, alignment flag, and XP
threshold exactly. The table has 22 good/evil pairs, starting at Civilian
(0 XP), Cadet/Thug (100), Ensign/Pirate (400), Lieutenant/Raider (1,000),
and ending at Galactic Admiral/Death Lord (5,000,000). All intervening rows
remain as currently seeded; do not substitute another rank list or adjust
thresholds.

| Rank row | Good threshold | Good title (ID) | Evil threshold | Evil title (ID) |
| ---: | ---: | --- | ---: | --- |
| 1 | 0 | Civilian (1) | 0 | Civilian (2) |
| 2 | 100 | Cadet (3) | 100 | Thug (4) |
| 3 | 400 | Ensign (5) | 400 | Pirate (6) |
| 4 | 1,000 | Lieutenant (7) | 1,000 | Raider (8) |
| 5 | 2,500 | Lt. Commander (9) | 2,500 | Marauder (10) |
| 6 | 5,000 | Commander (11) | 5,000 | Buccaneer (12) |
| 7 | 10,000 | Captain (13) | 10,000 | Corsair (14) |
| 8 | 20,000 | Commodore (15) | 20,000 | Terrorist (16) |
| 9 | 35,000 | Rear Admiral (17) | 35,000 | Anarchist (18) |
| 10 | 75,000 | Vice Admiral (19) | 75,000 | Warlord (20) |
| 11 | 100,000 | Admiral (21) | 100,000 | Despot (22) |
| 12 | 150,000 | Fleet Admiral (23) | 150,000 | Tyrant (24) |
| 13 | 20,000 | Grand Admiral (25) | 200,000 | Warmonger (26) |
| 14 | 300,000 | Lord Commander (27) | 300,000 | Dread Pirate (28) |
| 15 | 400,000 | High Commander (29) | 400,000 | Cosmic Destroyer (30) |
| 16 | 550,000 | Star Marshal (31) | 550,000 | Galactic Menace (32) |
| 17 | 700,000 | Grand Star Marshal (33) | 700,000 | Void Reaver (34) |
| 18 | 1,000,000 | Supreme Commander (35) | 1,000,000 | Grim Reaper (36) |
| 19 | 1,500,000 | Galactic Commander (37) | 1,500,000 | Annihilator (38) |
| 20 | 2,000,000 | Galactic Captain (39) | 3,000,000 | Supreme Annihilator (40) |
| 21 | 4,000,000 | Galactic Commodore (41) | 4,000,000 | Chaos Bringer (42) |
| 22 | 5,000,000 | Galactic Admiral (43) | 5,000,000 | Death Lord (44) |

This table mirrors the current seed literally, including the non-monotonic
Grand Admiral threshold and unequal Galactic Captain/Supreme Annihilator
thresholds. Those values are intentionally left as-is by the XP work.

**XP sources and existing semantics:**

| Activity | Existing formula/behavior | Current implementation |
| --- | --- | --- |
| Profitable trade | Positive `credits_delta / trade_xp_ratio`; non-positive delta earns no XP | Engine has a `player.trade.v1` handler, but trade commands currently do not emit that event; source wiring and idempotency are incomplete |
| Successful port robbery | Credit loot uses existing trade ratio; goods loot uses existing fixed amount | Server applies XP directly in the robbery command |
| Failed/real port robbery | Existing loss is 5% of current XP | Server applies XP directly; preserve as a recorded negative adjustment |
| Combat/ship destruction | The destruction procedure already applies its configured XP loss | Record the existing adjustment against its source event; do not invent a victor award amount |
| Other significant actions | Only after an existing action has a stable event and a deterministic formula | No further source approved yet |

Trade and robbery formulas are existing behavior, not new formula decisions.
Any new combat winner, exploration, mission, or encounter award needs an
explicit event payload and deterministic formula before activation.

**Final acceptance criteria:**

1. XP remains persistent in `players.experience` and meaningful approved
   activity changes it deterministically.
2. The current 44 `commission` rows remain byte-for-byte unchanged in meaning:
   rank/title is selected by XP threshold and the existing alignment path.
3. Rank/title refreshes when XP or alignment crosses a commission boundary.
4. Each event-driven adjustment is recorded and applied at most once, even
   after retry, replay, or restart; XP is clamped at zero.
5. XP does not affect damage, ship statistics, or combat values, and no RPG
   attributes, skill trees, or combat multipliers are added.

**Database changes required:** Migration 115 adds
`player_progress_adjustments`, keyed uniquely by source event/request. Keep
`players.experience` and all existing `commission` rows. The ledger records
player, signed delta, reason/source, formula version, and timestamp. Apply
ledger insertion, XP aggregate update, and commission recalculation
atomically. No threshold/rank migration is required.

**API changes required:** Preserve existing XP and title fields. Ensure player
info and rankings expose the current commission title with experience and
alignment; add only backward-compatible fields where a projection is missing.
Event producers must provide stable source IDs and the data needed to apply the
existing trade/robbery formulas. Coordinate producer and response contracts
with Codex #2; do not invent new combat award values in this change.

**Test plan:** Cover persistence and restart, each existing trade/robbery XP
award or loss, source-event duplicate/replay, XP floor at zero, each seeded
threshold boundary, both alignment title paths, and unchanged rank seed data.
Assert no combat or ship statistic changes when XP changes.

### #312 Turnbank and daily login bonuses — rejected scope

**Final acceptance criteria:** No turn banking, login bonuses, streak rewards,
stored turn accumulation, or inactivity accrual are implemented. Turns remain
use-or-lose and are earned through the existing game cadence.

**Database/API changes:** None. Do not add turn-bank columns, ledgers, login
reward events, or login/status fields for accumulated turns.

**Test plan:** Existing cadence/reset and turn-consumption tests continue to
pass; add a regression assertion that no login path or scheduled task grants
stored turns or a login bonus.

## 3. Server/API handoff to Codex #2

Before implementation, agree on these server-owned contracts with Codex #2:

- the global daily tax configuration source and taxable-activity event coverage;
- the NPC negotiation request/result contract and retry behavior;
- XP adjustment event shape and player ranking projection using the existing
  commission table;
- corporate vote/HQ/diplomacy commands and versioned result/event payloads.

All response additions should be backward compatible. These proposals are
design gates, not claims that the protocol already supports them.

## 4. Final implementation preparation

The stakeholder has approved the scope for #288, #270, #277, and #312 as
specified above. Runtime changes remain unimplemented. Before starting server
protocol work, coordinate with Codex #2 on the tax activity/receipt projection,
NPC negotiation request/result contract, and XP source event payloads. Use
isolated deterministic tests before merging any runtime implementation.

The separate #407 corporate epic decomposition and MySQL runtime version/scope
choices remain open topics outside these approved gameplay decisions.
