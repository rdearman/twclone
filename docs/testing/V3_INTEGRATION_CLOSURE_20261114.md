# V3 integration and regression closure — 14 November 2026

## Scope and environment

This was a focused integration check, not a new feature sprint. Gameplay
acceptance used two ordinary Python accounts against a server built from the
current working tree and a fresh disposable Docker PostgreSQL database. Only
test fixtures and scheduler timestamps in that database were changed. The
existing beta and host PostgreSQL were not used.

## Regression results

- `python3 -m pytest -q tests.v2 --tb=short`: **249 passed, 7 skipped, 227
  subtests passed; 0 failed**. Of the seven earlier failures, five were stale
  checks: three SHIP-02 catalog expectations, an open-order expiry expression
  that rejected a legal trailing comma, and a ship-transfer test tied to an
  obsolete source phrase. One exposed six PostgreSQL base tables missing from
  the MySQL base schema; counterparts were added and schema parity now passes.
  One Ferengi fixture relied on host/default database credentials; it now
  requires explicit QA credentials and safely skips when they are absent.
  MySQL runtime remains unsupported; this reconciles base schema coverage and
  does not claim operational MySQL parity.
- `python3 -m pytest -q client/python_client/tests`: **802 passed**. A malformed
  non-object `move.warp` response previously caused an attribute error when
  the client attempted `.get`; the client now fails closed and refreshes
  confirmed player, ship, and sector state.
- Godot 4.5.2 project import, shared asset catalogue (**23 checks**), auth
  transport test, and authoritative refresh test (**14 checks**) ran. The
  refresh test exposed acceptance of a floating-point sector ID despite the
  integer V3 identity contract; that validation is now strict. Its cross-
  account refresh check had a closure counter that did not observe the signal;
  the test now uses a mutable counter array.
- Godot is **not verified**: `command_menu_test.gd` still fails with a large
  set of menu assertions. It also exposed an attempted mutation of a read-only
  command dictionary; the client now deep-copies selected actions before
  retaining them. The follow-up test still fails, so Godot client integration
  remains a blocker and needs a separate focused repair. No live Godot
  multiplayer journey was executed.

## Live player evidence

The same continuous QA world included players 106 and 107:

1. Player 107 deployed one toll fighter in sector 141 through the Python
   deployment menu.
2. Player 106 moved to planet 7, allocated 100 colonists to ORE production,
   and later harvested 12 ORE through the Python surface workflow. The server
   production tick had produced that stock. Planet stock fell by 12 and the
   active ship received 12 ORE; the subsequent harvest correctly returned
   zero, demonstrating no duplicate collection.
3. The starter ship's ineligible photon attempt was refused. In the disposable
   fixture only, its class was changed to the approved photon-capable Missile
   Frigate so the eligible case could be exercised. The Python deployment
   menu fired at adjacent sector 142; the server returned
   `combat.photon_fired_v1`, consumed one of two photons, persisted a 600
   second effect, and returned the same effect on retry after reconnect.
4. Player 106 accepted the sector 141 toll through the Python movement
   workflow. PostgreSQL records the offer as accepted for one credit, ship 39
   in sector 141, player 106 reduced by one credit, and player 107 increased
   by one credit. This confirms atomic payer/payee accounting and passage.
5. Player 106 started a funded class 1 StarPort construction through the
   landed Python surface menu. The successful reply was intentionally lost;
   after server restart the same request was retried through that menu and
   returned the original construction ID with `idempotent_retry=true`.
   PostgreSQL retained one construction row. The scheduled deliveries
   completed and created active port 192 in sector 7.

The test helper transcript and server logs were kept in the ephemeral
directory `/tmp/twclone-verification-20261114` during execution and removed
after cleanup because they contain disposable account/session material.
Authoritative StarPort evidence is summarized in
[`V3_STARPORT_ACCEPTANCE_20261114.md`](V3_STARPORT_ACCEPTANCE_20261114.md).

## Remaining verification blockers

- Godot command-menu regression is red and live Godot gameplay is untested.
- StarPort concurrent upgrade/attack ordering and injected rollback remain
  unverified.
- Toll refusal/combat, simultaneous duplicate toll responses, and restart
  persistence of toll offer resolution were not exercised in this journey.
- Photon expiry and restart persistence after expiration were not exercised.
- Planet production verification covered one class and ORE only. Other class
  ratios, slave stoppages/losses, and production rollback remain unverified.
- Seven tests.v2 cases skipped; in particular, Ferengi PostgreSQL acceptance
  requires explicit disposable QA credentials and was not executed here.
- The full Godot runner stopped at the command-menu failure; the complete set
  of Godot test files was not verified.

These results do not establish public beta readiness or full TWGS 3.13 parity.
