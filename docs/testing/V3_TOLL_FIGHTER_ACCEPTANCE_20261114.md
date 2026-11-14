# V3 Toll Fighter Acceptance — 2026-11-14

## Scope and environment

Migration 184 and the current V3 server ran against the disposable PostgreSQL
16 container `twclone-qa-prod182-20261114`, database `twclone_rates_game`, on
loopback port 55489. Ordinary players 107, 108, 109 and 110 were registered
through `auth.register`. SQL was used only to provision sector 2 with a
deterministic three-fighter toll asset and to set player 110's credits to zero.
The toll action itself used the Python client's `_perform_warp` workflow.

The noncanonical default rate from `V3-TOLL-RATE-01` was 1 credit per fighter.
Each generated offer quoted 3 credits for 3 fighters.

## Executed journeys

- **Accept:** player 107 accepted the Python prompt, moved from sector 1 to 2,
  and paid 3 credits. Player credits changed 5000→4997; toll-owner credits
  changed 5000→5003; the offer became `accepted`; the toll fighters did not
  engage.
- **Refuse:** player 107 returned to sector 1 and refused a second offer.
  Movement proceeded as a hostile encounter. The three toll fighters were
  consumed in combat and the player's shields, fighters and hull were damaged.
  No credits changed; the receipt became `refused`.
- **Concurrent offer retry:** two concurrent copies of the same `move.warp`
  request for player 109 returned the same offer ID. Accepting that offer
  transferred exactly 3 credits to its owner and moved once.
- **Completed retries:** replaying both the original movement request ID and
  the accepted offer after reaching sector 2 returned `move.result` with
  `idempotent_retry=true`; neither replay charged credits or moved again.
- **Insufficient funds:** player 110 had zero credits. Accepting the 3-credit
  offer was refused; the player stayed in sector 1 with zero credits and
  999999 turns, the owner remained at 5006 credits, and the toll asset retained
  all three fighters. The quote remained pending for a retry/decline.

## Automated verification

- ASan/UBSan build passed after the toll handler changes.
- Focused regression: `python3 -m pytest -q tests.v2/test_v3_toll_fighter_contract.py tests.v2/test_v3_photon_contract.py tests.v2/test_v3_planet_production_rates.py tests.v2/test_planetary_server_contract.py` — **63 passed** before the final completed-retry preflight adjustment; rerun after that adjustment is required.
- Migration 184 applied to the disposable database. Replay test is pending.

## Status and remaining work

Normal-warp toll behavior is implemented and partially runtime accepted. It is
not marked verified. Transwarp arrivals currently do not present toll offers;
also pending are mixed-owner edge cases, destruction during refusal, pending
offer restart/reconnect workflow, injected settlement rollback, and the final
regression rerun. The 1-credit rate is configurable and explicitly
noncanonical; no TWGS 3.13 numeric-parity claim is made.
