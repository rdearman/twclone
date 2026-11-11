# twclone V3 status

Status date: 2026-11-01. This is the maintained V3 release-status document. It
records evidence and blockers; it is not a replacement for the protocol,
architecture, gameplay, or canonical-reference documents.

## Release objectives

V3 must provide:

- the selected, version-pinned TradeWars 2002 player-facing gameplay scope;
- all documented twclone enhancements, without silently dropping incomplete scope;
- authoritative server rules with atomic PostgreSQL persistence, restart safety,
  and idempotent retries;
- usable ordinary-player Python and Godot workflows with aligned schemas,
  refusals, refreshes, and state recovery;
- explicit separation of V3 scope from MySQL, V5 server-to-server federation,
  and experimental AI work. This does not exclude the in-game Federation
  faction, jurisdiction, or enforcement mechanics from V3 where specified.

Canonical requirements are described in the TradeWars references cited by the
parity work, the V3 protocol in [`docs/PROTOCOL.v3/00_index.md`](PROTOCOL.v3/00_index.md),
and the gameplay/design contracts in [`docs/V3_GAMEPLAY_DECISIONS.md`](V3_GAMEPLAY_DECISIONS.md),
[`docs/ENGINE.md`](ENGINE.md), [`docs/ARCHITECTURE_OWNERSHIP.md`](ARCHITECTURE_OWNERSHIP.md),
and `docs/design/`. Ship, movement, trade, combat, planets, corporations, banking,
NPC, community, and SysOp command requirements are in `docs/PROTOCOL.v3/19_*` through
`29_*`.

## Verified completed functionality

These are deliberately narrow completions, supported by real socket evidence
recorded in the parity audit's section 21 and its artifacts, not by command
registration or unit tests alone:

- Adjacent movement, movement refusal, and unchanged turns on refusal:
  `tests.v2/suite_movement_nav.json` (`movement_nav` live run).
- Exercised information-boundary behavior, including port-class redaction and
  remote visibility refusal: `tests.v2/suite_information_fog.json`
  (`information_fog` live run).
- Fixed port buy/sell direction and stock refusal, with the exercised credit,
  turn, cargo, cloak, knowledge, and mode invariants:
  `tests.v2/suite_port_fixed_modes.json` (`port_fixed_modes` live run).
- Basic Genesis provisioning, planet creation, schema-correct planet rename, and
  ownership transfer: `tests.v2/suite_planets.json` and `suite_planet_ops.json`
  (`planets` and `planet_ops` live runs).

These completions do not certify the surrounding subsystems or all canonical
variants.

## Confirmed outstanding functionality

- `ship.sell` and `shipyard.sell` now share an atomic sale settlement and
  persist identifiable used hulls; used-hull acquisition remains undefined
  because V3 has no purchase request or pricing/settlement contract.
  `ship.transfer` has source-level transactional code but lacks accepted live
  two-player settlement and lifecycle evidence. See `src/server_ships.c`,
  `src/server_stardock.c`, and `tests.v2/suite_ship_sale_v3.json` plus ship
  lifecycle tests.
- `planet.harvest` remains unimplemented. Its resource, landed-ship destination,
  zero-turn, no-generation, atomicity and partial-capacity rules are approved;
  authorization for a landed non-owner remains unresolved. `planet.view` and
  `planet.emancipate` now have source handlers, but runtime verification is
  pending. Population-type and slave-production groundwork exists; conquest
  conversion, slave acquisition and deterministic unrest remain incomplete.
- Mandatory escape-pod respawn, cargo/equipment loss, synchronous atomic
  transwarp and immediate entry-hazard policies are approved in protocol 21/23;
  implementation across every destruction/movement path remains outstanding.
- Full trade settlement, retries, restart conservation, market-order matching,
  and the trade-account preference contract remain unresolved. V3 excludes
  haggling and retains authoritative dynamic port prices.
- The Galactic Exchange source audit found that immediate planetary sales
  remove stock and credit the seller without recording Exchange inventory or
  an Exchange-funded buyer; scheduled order matching instead transfers goods
  directly between seller and buyer locations. Exchange ownership/funding
  integration is required. See `V3_GALACTIC_EXCHANGE_CONTRACT.md`.
- Bank-interest runtime wiring is unresolved: isolated SQL exists, but one
  authoritative scheduler path, ledger classification, idempotency, rollback,
  and restart evidence are still required.
- Ship lifecycle invariants, selected-ship inspection, public hull state, and
  ordinary-player Python/Godot end-to-end parity remain incomplete.
- Private chat contract alignment, nonempty rankings, mail contract alignment,
  Ferengi/NPC autonomous behavior, and multiplayer event replay/ACK/resume need
  end-to-end evidence.

## Known blockers and issue references

- `V3-CAN-00`: pin the TW2002 edition for parity questions not settled by
  approved V3 policies. V3 has explicit rules for fixed-price trade, hull-only
  insurance, harvest, destruction/respawn, capture and synchronous transwarp;
  these do not assert edition-specific parity.
- Issue `#491`: V3 release contract and original player-facing parity
  authority; server-to-server federation and other noncanonical expansions
  are outside that release authority.
- Issue `#423`: computer/intelligence, scans, rankings, and information scope.
- Issue `#396`: combat feature scope and aftermath.
- Issue `#416`: planet/colonisation scope.
- Issue `#437`: NPC and Ferengi behavior.
- Issue `#490`: bounded ordinary-player/AI QA capability, not proof of canonical
  parity.
- Issue `#493`: MySQL backend work; not a V3 PostgreSQL gameplay release gate.
- Issues `#472–#483`: server-to-server federation programme, assigned to V5;
  preserve these issue numbers and exclude this programme from V3 parity
  certification. This does not refer to in-game Federation gameplay.

## Reproducible acceptance evidence

The live-suite run names and artifact locations are recorded in
`docs/reports/V3_TW2002_CANONICAL_PARITY_AUDIT.md`, section 21. Reproducible
fixtures and contracts include:

- `tests.v2/suite_movement_nav.json`
- `tests.v2/suite_information_fog.json`
- `tests.v2/suite_port_fixed_modes.json`
- `tests.v2/suite_planets.json`
- `tests.v2/suite_planet_ops.json`
- `tests.v2/suite_ship_negative.json`
- `tests.v2/test_transwarp_server_contract.py`
- `tests.v2/test_sector_entry_server_contract.py`
- `tests.v2/test_insurance_server_contract.py`
- `tests/pg/` repository and server integration runners

Static tests and isolated SQL tests are useful regression evidence but do not
promote a gameplay requirement to complete. The next acceptance run must use a
disposable PostgreSQL database, record the exact binary/migration/seed, and cover
positive, negative, persistence, rollback, retry, and two-player journeys.

## Scope preservation

Dynamic pricing, data-driven commodities and port behavior, trade offers, market
settlement, insurance, planet taxation/production, environmental hazards, NPC
personalities, event journals, gateway delivery, client asset systems, and SysOp
operations remain documented V3 enhancements. Their incomplete implementation is
reported as a status or blocker; it is not removed from scope.
