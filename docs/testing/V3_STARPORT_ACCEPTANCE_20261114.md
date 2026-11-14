# V3 StarPort acceptance — 14 November 2026

Status: **implementation and live acceptance evidence recorded; not marked VERIFIED**.

## Environment and rules

- Server was built from the V3 working tree and run locally against disposable Docker PostgreSQL container `twclone-qa-prod182-20261114`, database `twclone_rates_game` on loopback port `55489`.
- The existing beta and host PostgreSQL were not used or changed.
- Players 107 and 108 were ordinary registered accounts. Initial resource/sector state was adjusted directly in this disposable database only to make the journey deterministic. Construction and upgrades were initiated through the Python client workflow code; the successful port attack journey also dispatched `combat_attack_port_flow` from the Python client against the running server.
- Scheduler task due times were advanced in the disposable database to execute all three scheduled construction deliveries promptly. Delivery amounts and gameplay settlement were server-handled.
- Numeric construction, delivery, upgrade, durability, defence and attack-turn defaults are configurable V3 values, explicitly labelled noncanonical in `docs/V3_GAMEPLAY_DECISIONS.md`. No TWGS 3.13 parity is claimed for those values.

## Evidence

| Journey | Result | Observed evidence |
|---|---|---|
| Fund a port from a planet | PASS | Python surface workflow debited 100,000 credits and 1,000 each ORE/ORG/EQU. Insufficient initial EQU refused construction; credits and all planet resources remained unchanged and no extra construction row appeared. |
| Scheduled construction | PASS | Three scheduled deliveries completed; the selected class produced an active port and its expected fixed commodity buy/sell directions. Construction state and completed port ID persisted in PostgreSQL. |
| Retry construction request | PASS with a caveat | Reusing the same player/request key was reconciled after correcting an advisory-lock SQL type mismatch. The construction row remained unique. The final replay response was not captured in a concise transcript, so exact response replay remains a verification follow-up. |
| Upgrade port capacity | PASS | Python dock workflow upgraded ORE capacity level 1: player paid 10,000 credits, ship supplied 100 ORE, port petty cash increased by 10,000, and capacity increased by 1,000. The same idempotency key replay did not debit again. `port.info` reported `max_quantity=6000`. |
| Upgrade rollback | PASS | A new level-2 request with no ORE was refused; credits, cargo, port cash and capacity level stayed unchanged. |
| Protected-sector attack | PASS | Ordinary Python combat workflow in sector 1 received Captain Z refusal. The port and turns were unchanged; ship destruction/recovery occurred once. Retrying the same action key did not destroy another ship. |
| Attack retaliation | PASS | In sector 20, the first ordinary Python attack reduced durability from 10,000 to 1,000, used one turn, and removed 1,000 fighters before hull damage. The follow-up attack reduced durability to zero, destroyed the port, and applied remaining retaliation to hull. |
| Destruction and recovery | PASS | The second strike destroyed the attacking 100-hull ship; the player recovered in sector 1 in a replacement escape pod. No salvage was awarded. The port became inactive with durability zero and its commodity stock was set to zero. |
| Concurrent destruction retry | PASS | Two concurrent requests carrying the same player/request key returned the same destruction receipt. Only one action/turn/retaliation settlement occurred. |
| Restart persistence | PASS | After server restart, `sector.info` omitted the destroyed port and `port.info` refused access. PostgreSQL retained the inactive historical port row and zeroed stock. |

## Regression results

- Focused tests: `python3 -m pytest -q tests.v2/test_starport_sector_and_pta_contract.py tests.v2/test_v3_planet_production_rates.py tests.v2/test_v3_photon_contract.py tests.v2/test_v3_toll_fighter_contract.py` — **14 passed**.
- Python client syntax and menu JSON validation passed with `py_compile` and `json.tool`.
- Full `python3 -m pytest -q tests.v2`: **243 passed, 7 failed, 6 skipped, 69 subtests passed** (18.7 seconds). Failures: one Ferengi fixture attempted localhost:5432 with stale credentials; three SHIP-02 catalog expectations disagree with the current catalog artifact; MySQL/PostgreSQL base-schema table parity reports six PG-only tables added by recent V3 work; an open-order expiry static test expects a comma-free PostgreSQL column line although the nullable `timestamptz` declaration has a trailing comma; and a ship-transfer source test expects an older source-text phrase. No assertion was weakened. The Ferengi test failed authentication before SQL mutation. These are not counted as StarPort acceptance failures.
- Migration 187's partial unique index was applied twice to disposable PostgreSQL: the first application created it and replay succeeded with `IF NOT EXISTS`.

## Implementation notes and remaining verification

- Construction and capacity upgrade are now ordinary-player workflows. Port class is fixed at completion; only one active port may occupy a sector. Upgrade cost/material/capacity progression comes from versioned rules. Migration 187 adds database uniqueness for active `ports.sector_id`, closing the concurrent active-port race.
- Port combat uses separately persisted durability and defence; attack, turn debit, shield/fighter/hull retaliation, destruction, stock clearing, event and retry receipt are transactional. FedSpace Captain Z enforcement is applied before port damage.
- The first attempt to force a rebuild invoked concurrent Autotools regeneration and failed in configure; the generated build-file changes were reverted. The regular build path was restored and the rebuilt server was then used for the live combat test. A disposable fixture initially attempted hull above the schema's 100-point bound and was rejected before gameplay; it was corrected to the valid 100-point maximum.
- This evidence does not establish all possible concurrent upgrade/attack orderings, injected database rollback at every statement boundary, every port class mapping, or restart replay of construction receipts. Those remain follow-ups; therefore the tracker remains `IMPLEMENTED_UNVERIFIED`.

## Integration closure follow-up — 14 November 2026

Using a separate disposable PostgreSQL database and the ordinary Python player
workflow, the construction receipt gap above was closed. Player 106 initiated
class 1 construction from the landed surface. The test client deliberately
dropped the successful reply, the disposable game server was restarted, and
the player retried with the same pending request key through the surface menu.
The client printed construction ID 1 and PostgreSQL returned the same
`construction_id=1`, `idempotent_retry=true`, with exactly one request row.
The three scheduled deliveries then completed; PostgreSQL recorded
`status=completed`, `deliveries_completed=3`, and `port_id=192`. Port 192 was
active in sector 7 with `porttype_id=1`, durability 10,000, and defence 1,000.
Delivery timestamps and the scheduler due time were advanced only in the
disposable QA database to avoid waiting a day between deliveries.

Concurrent upgrade retries, concurrent attack refusals, and rollback under
injected database failure remain unverified. This follow-up therefore closes
restart replay and delivery completion only; it does not promote StarPort to
fully verified.
