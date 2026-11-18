# TWClone V3 ruleset 1.1 — approved decision addendum

Date: 18 November 2026
Base: the existing approved V3 contracts in `V3_GAMEPLAY_DECISIONS.md` and
`V3_GAMEPLAY_PARITY.md`.

This addendum records decisions approved for TWClone V3. It is a project ruleset,
not a claim that every rule reproduces historical TradeWars 2002 or TWGS 3.13.
Existing version 1.0 contracts remain in force except where a compatible,
explicitly approved change is implemented. A listed rule can remain blocked if
it conflicts with an existing contract or lacks a necessary accounting rule.

## Approved decisions and implementation state

| Rule | Approved V3 decision | Classification | Implementation state |
|---|---|---|---|
| Slave unrest | Range 0–100; each production cycle with slave labor adds 2; each production cycle without slave labor subtracts 1; disruption chance is `min(floor(unrest / 10), 10)%`; disruption skips the affected production cycle and never destroys population. | TWClone-specific numeric rule; historical parity unverified. | **Blocked by contract conflict.** Existing POP-05 and protocol 24 approve a persisted daily stoppage policy that can reduce slave population. Do not replace it until the owner reconciles which contract/version controls. No database migration or production change made. |
| Ferengi destruction reward | An eligible player who destroys an eligible Ferengi ship receives 5,000 credits at most once for that destruction. | TWClone-specific fixed reward; historical parity unverified. | **Blocked by missing funding/accounting source.** Existing `bounties` rows represent a liability against an offender and do not fund a killer reward. This does not replace the existing Ferengi retaliation/wanted flow. No reward path added. |
| Orion entry targeting | Orion NPCs attack eligible non-allied players entering an Orion-controlled sector through supported travel, subject to existing safe-zone and combat protections. | TWClone-specific targeting rule; historical parity unverified. | **Implemented and partially verified.** Warp, transwarp, allied exclusion, combat settlement, restart and duplicate scheduler retry passed on disposable PostgreSQL. Safe-zone/protection edges, NPC removal races, defense reinforcement and other personalities remain unverified. |
| Economy observation horizon | Evaluate invariant and balance telemetry over 30 simulated game days, without tuning approved rates, prices, interest or rewards. | Acceptance policy, not a gameplay constant or historical claim. | **Harness/acceptance incomplete.** The horizon alone does not define an acceptable balance threshold. Record invariant violations separately from balance judgments requiring policy. |

## Evidence and compatibility

Orion evidence is in `tests.v2/suite_orion_entry_attack_v3.json` and
`tests.v2/suite_orion_entry_attack_v3_verify.json`, executed against a serving
V3 process and an isolated disposable PostgreSQL database. The game process
restarted between setup and gameplay. Both suites passed (27 and 35 expanded
cases; 62 passed, 0 failed). See the 18 November acceptance update in
`testing/V3_CLAUSE_ACCEPTANCE_MATRIX_20261116.md`.

The Orion repository now counts and controls only ships whose ownership player
has `is_npc=TRUE`. No schema migration is needed. Existing human corporation
assets are preserved; reconciliation may provision additional NPC ships if the
actual NPC fleet is below its configured target.

No claim in this addendum establishes complete V3 parity. Parent requirements
remain conservatively classified until every mandatory clause has executable
evidence.
