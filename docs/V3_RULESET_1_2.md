# TWClone V3 ruleset 1.2 — approved mechanics addendum

Date: 18 November 2026  
Status: versioned TWClone V3 rules. These project decisions do not assert
historical TradeWars 2002 or TWGS 3.13 parity unless separately verified.

Ruleset 1.0 remains in force except where this addendum gives an explicit,
compatible refinement. The daily POP-05 slave-unrest contract is preserved.

## Slave unrest

These values govern the separate production-cycle unrest mechanic:

| Parameter | Approved V3 value |
|---|---:|
| Range | 0–100 |
| Eligible cycle using slave labor | +2 unrest |
| Eligible cycle without slave labor | −1 unrest |
| Disruption chance | `min(floor(unrest / 10), 10)%` |
| Disruption effect | Skip that cycle's production; no population loss from the disruption |

Unrest is clamped after its cycle transition and the probability is evaluated
from the resulting unrest level. The roll is deterministic for the planet and
production tick so retrying that tick cannot draw a new result.

This does not replace or weaken POP-05. POP-05 remains the separate daily,
persisted stoppage, recovery and configured population-loss mechanism. A
persistent POP-05 stoppage suppresses slave production; it is not converted
into a per-cycle disruption and does not change the cycle unrest level while no
eligible slave-production cycle occurs. The cycle disruption itself neither
changes population nor consumes production remainders.

Compatibility finding: **no direct conflict** between the numeric cycle rules
and POP-05 when the two consequences remain separate as above. Existing POP-05
stoppage recovery and possible population loss remain authoritative.

Implementation: migration `190_v3_slave_unrest_production_cycles.sql`,
`src/slave_unrest_cycle.h`, and `db_cron_planet_update_production_stock`.
Disposable PostgreSQL acceptance passed 20/20 cases across
`slave_unrest_production_prepare.json` and
`slave_unrest_production_verify.json`, including cycle transitions,
disruption accounting, no disruption population loss and duplicate-tick
idempotency. The separate POP-05 daily path passed 30/30 disposable PostgreSQL cases across
`slave_unrest_daily_prepare.json` and
`slave_unrest_daily_restart_verify.json`, including configured evaluation,
stoppage/loss/recovery, proportional assignment accounting, unchanged owner and
population class, restart persistence, retry idempotency and injected rollback.
Concurrent POP-05 scheduler invocation remains untested. The daily numeric policy
values remain configurable and unset; this addendum supplies none of them, so it
does not unblock or promote the full POP-05 requirement.

## Ferengi destruction reward

An eligible player receives exactly 5,000 gameplay credits for destroying a
genuine Ferengi NPC ship. This is a **system-generated V3 economic source**.
The reward is recorded with its recipient and source in
`ferengi_destruction_rewards` and is settled atomically with ship destruction.
Its once-only key is the NPC ship's destruction sequence, so a retry cannot pay
twice and a later respawn followed by a new destruction has a distinct event.
Player-owned ships in a Ferengi corporation do not qualify.

This reward is separate from the existing Ferengi bounty penalty/wanted-list
mechanism. It does not replace that penalty or fund it. The reward credits the
player's gameplay balance; no treasury debit is implied.

Implementation: migration `191_ferengi_destruction_rewards.sql` and the
`combat.attack` settlement path. Disposable PostgreSQL acceptance passed 35/35
cases in `ferengi_destruction_reward.json` and 6/6 after a serving-server
restart in `ferengi_destruction_reward_restart.json`. It verified the exact
authenticated 5,000-credit reward, source/recipient ledger, one reward per
destruction sequence, player-owned Ferengi-corporation exclusion, retry
rejection and restart persistence. The separate wanted-list penalty and
Ferengi pursuit clauses remain unverified, so the parent NPC-FER-01 is not
promoted.

## Orion targeting

Orion attacks remain subject to safe-zone protection and established
combat/ownership rules. Sectors 1–10 are excluded before an attack action is
claimed. Ships owned by Orion corporation members are treated as allied and are
excluded for every Orion combat personality. The scheduler processes each
eligible player in a sector rather than stopping after the first target.

These protections do not create a general player immunity or a new alliance
system. The current project contract only supplies the existing Orion
corporation relationship and FedSpace safe-zone protection.

Disposable PostgreSQL acceptance passed 60/60 cases across
`orion_entry_attack_prepare.json` and `orion_entry_attack_verify.json`. It
verified ordinary warp and transwarp, allied exclusion, one hostile damage
settlement, same-window idempotency and a serving-server restart. This run did
not exercise a safe-zone arrival, a protected-player exclusion beyond the
existing Orion alliance, removal of an NPC during targeting, or genuinely
concurrent arrivals; those remain open and NPC-ORI-01 stays PARTIAL.

## Economy acceptance horizon

The approved horizon is 30 simulated game days. The hidden SysOp acceptance
command `sys.cron.advance_to` dispatches the actual registered cron handlers and
their existing rescheduling logic at a deterministic QA epoch. It is gated by
`test.deterministic_scheduler_enabled=1`; the engine's wall-clock cron dispatch
is paused while this isolated test switch is on. It is not a gameplay feature
or a balance rule.

Simulation telemetry can demonstrate accounting/state invariants and reveal
sources or sinks. The 30-day PostgreSQL simulation passed 64/64 cases using the
actual scheduler dispatcher. It captured daily player/corporation/bank credits,
planet and port stock, population, production remainders, reward counts,
bank/corporation flows by mechanism and planetary production totals. Daily
checks found no negative balances, stock or population and no hardware stock
above capacity. This establishes the tested invariants for this fixture, not
economy-wide conservation or balance. There is still no approved quantitative
balance criterion; no rates or prices were changed.

The black-market hardware regression passed 34/34 against disposable
PostgreSQL. Its restock deadline is advanced through the same registered
scheduler dispatcher, preserving the exact stock assertion without a
wall-clock wait.
