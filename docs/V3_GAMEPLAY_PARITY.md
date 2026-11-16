# V3 gameplay parity

This page summarizes the scope and verification limits for original
TradeWars 2002 gameplay. It is not a second protocol or an append-only test
log.

## Authority and scope

1. [`PROTOCOL.v3/00_index.md`](PROTOCOL.v3/00_index.md) defines the published
   client/server and server/engine contracts.
2. [`V3_GAMEPLAY_DECISIONS.md`](V3_GAMEPLAY_DECISIONS.md) records approved
   gameplay rules, configured noncanonical defaults and unresolved decisions.
3. Focused reports linked from the
   [`completion tracker`](V3_GAMEPLAY_COMPLETION_TRACKER.md) record executed
   evidence. A source handler or passing unit test alone does not establish
   playable parity.

V3 targets the approved original-game player loop. V4 StarPort ownership,
freight exchanges and other expressly deferred expansions are not V3 parity
requirements. The exact original executable/configuration has not been pinned;
the published TradeWars manual is a discovery reference and cannot override
approved V3 decisions.

## Gameplay areas

| Area | V3 scope | Verification approach |
|---|---|---|
| Player and navigation | Registration, persistence, movement, scans, routes, avoids and turns | Ordinary-player client journey plus authoritative state checks |
| Trading and economy | Port modes, cargo/credit/stock settlement, equipment and scheduled economy | Client workflow, transaction accounting, retries and restart checks |
| Ships and combat | Acquisition, repair, transfers, weapons, sector defenses, destruction and recovery | Two-player client journey with damage, resource and recovery assertions |
| Planets | Landing, colonists, production, harvest, citadels and planetary combat | Ownership/access checks, production accounting and restart checks |
| Corporations | Membership, permissions, treasury and authorized shared transfers | Multiple ordinary accounts and conservation assertions |
| StarPorts | Construction, deliveries, capacity upgrades, attack and destruction | Concurrency, idempotent replay, rollback and history/stock assertions |
| Feedback | Authenticated reports, safe diagnostics and durable local queue | Redaction, ownership, idempotency and mock publisher tests |

The latest acceptance summary and bounded claims are in
[`V3_GAMEPLAY_COMPLETION_TRACKER.md`](V3_GAMEPLAY_COMPLETION_TRACKER.md) and
[`testing/V3_RELEASE_CANDIDATE_AUDIT_20261115.md`](testing/V3_RELEASE_CANDIDATE_AUDIT_20261115.md).

## Known scope boundaries

- StarPort class, construction, upgrade, destruction and noncanonical defaults
  follow the approved V3 decisions; do not infer V4 ownership or salvage.
- Photon and toll-fighter numeric defaults are configurable and explicitly
  noncanonical unless a cited source establishes otherwise.
- Planet production ratios are versioned configuration. Preserve the approved
  worker allocation and citadel-gated fighter-production rules.
- Full MySQL game-server runtime is not supported by the current evidence.
- Passing client and server regression suites does not clear external
  deployment; consult the release-candidate audit.
