# V3 regression recovery checkpoint

Updated 2026-11-07.

- Historical baseline: 2,076 executed; 1,835 passed; 241 failed.
- Latest complete run: 2,077 executed; 1,910 passed; 167 failed; 1 blocked; 2 unsupported.
- Server crashes and transport cascades: 0.
- Resolved: XP persistence now uses the approved idempotent progress ledger path; cases 0006, 0009 and 0013 in `suite_v3_xp_persistence.json` pass.
- Current batch: corrected Ferengi fixture SQL alias and made player/ship positioning atomic; fixed Ferengi population ranking so Gaila, Moogie and Vek remain active under density scaling; changed command dispatch to authenticate before schema validation, so unauthenticated malformed requests receive the contract's 401 response. The latest full run removed eleven failures from the previous baseline; Gaila encounter and offer lifecycle now pass.
- Resolved in the latest run: literal numeric strings such as MFA codes now retain their JSON string type; the MFA suite passes. The corporation invite SQL now uses the schema's `corp_id` column; the remaining corporation failures are fixture/state sequencing issues rather than the prior missing-column error.
- Current batch: corporation invite expiry now converts PostgreSQL `timestamptz` to epoch seconds before comparison, and the advanced corporation roster fixture supplies its required `corp_id`. Targeted validation passed; a full regression rerun is pending.
- Resolved in the latest full run: the `sector.search` schema now matches the documented `q` field and permits the existing empty-query behavior; seven schema-driven failures disappeared. One remaining search failure is a fixture/result assertion for the requested `Sol` row.
- Current batch: updated the advanced corporation suite to the documented `corp.leave` last-member behavior (`corp.leave.dissolved`) and to expect the subsequent `corp.dissolve` refusal after the corporation has already been removed. Corrected corporation-permissions fixtures to isolate usernames, fund the CEO, invite/join the tested members, and expect the standard permission-denied error envelope. Corrected planet stock seeding to use the canonical conflict-safe upsert and withdrew the remaining 90 units after the 10-unit market sale.
- Latest full regression: 2082 cases executed, 1920 passed, 162 failed, 1 blocked, 2 unsupported; no server crash or transport cascade. The corporation-permissions and planet duplicate-stock fixture cascades are resolved; one remaining corporation assertion was the legacy `refused` status and is now aligned to error code 403.
- Current batch: aligned the move.describe_sector positive fixture with the V3 contract's required sector_id argument. The remaining ship-cargo dynamic failures use obsolete port.trade.* and planet.deposit request schemas and require a separate protocol reconciliation.
- Commits created: `b90d1aa6` (authentication dispatch, corporation invite expiry, corporation roster fixture, checkpoint) and `9347c2e8` (sector search schema).
- Known blockers: `events.resume` is compile-gated and unregistered; MSL tests request an unregistered cron command; quasar/Captain Z expectations require contract decisions.
- Prior commits: `60967ed5`, `317d0576`, `caf3b9e3`.
- Remaining Ferengi issue: the temporary `codex_fixture_roster_only` case expects a new active definition to bypass the configured population target; this is a fixture/specification question, not a provisioning error.
- Next group: tavern fixtures and other high-count shared fixture groups.
