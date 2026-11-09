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
- explicit separation of V3 scope from MySQL, federation, and experimental AI work.

Canonical requirements are described in the TradeWars references cited by the
parity work, the V3 protocol in [`docs/PROTOCOL.v3/00_index.md`](PROTOCOL.v3/00_index.md),
and the gameplay/design contracts in [`docs/V2_GAMEPLAY_SYSTEMS_CONTRACTS.md`](V2_GAMEPLAY_SYSTEMS_CONTRACTS.md),
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

- `ship.sell` and `shipyard.sell` explicitly refuse as not implemented;
  `ship.transfer` has source-level transactional code but lacks accepted live
  two-player settlement and lifecycle evidence. See `src/server_ships.c`,
  `src/server_stardock.c`, and `tests.v2/suite_ship_tow_live.json` plus ship
  lifecycle tests.
- `planet.harvest` explicitly refuses as not implemented. Planet production,
  positive treasury settlement, citadel lifecycle, landing/launch rollback, and
  destruction/capture remain incomplete or unverified. See `src/server_planets.c`
  and `tests.v2/suite_planet_landing_defense.json`.
- Transwarp canonical fuel, interdiction/lock rules, and full entry-hazard
  aftermath remain unresolved. Warp/transwarp/launch/flee/tow need one accepted
  hazard transaction and destruction/pod/respawn matrix.
- Full trade settlement, retries, restart conservation, market-order matching,
  haggling, and the trade-account preference contract remain unresolved.
- Bank-interest runtime wiring is unresolved: isolated SQL exists, but one
  authoritative scheduler path, ledger classification, idempotency, rollback,
  and restart evidence are still required.
- Ship lifecycle invariants, selected-ship inspection, public hull state, and
  ordinary-player Python/Godot end-to-end parity remain incomplete.
- Private chat contract alignment, nonempty rankings, mail contract alignment,
  Ferengi/NPC autonomous behavior, and multiplayer event replay/ACK/resume need
  end-to-end evidence.

## Known blockers and issue references

- `V3-CAN-00`: pin the TW2002 edition and SysOp policy before accepting haggling,
  insurance, harvest, market orders, combat odds, capture, or planetary-destruction
  behavior as canonical.
- Issue `#491`: original player-facing parity authority; federation and other
  noncanonical expansions are outside that release authority.
- Issue `#423`: computer/intelligence, scans, rankings, and information scope.
- Issue `#396`: combat feature scope and aftermath.
- Issue `#416`: planet/colonisation scope.
- Issue `#437`: NPC and Ferengi behavior.
- Issue `#490`: bounded ordinary-player/AI QA capability, not proof of canonical
  parity.
- Issue `#493`: MySQL backend work; not a V3 PostgreSQL gameplay release gate.
- Issues `#472–#483`: federation; defer from V3 parity certification.

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
