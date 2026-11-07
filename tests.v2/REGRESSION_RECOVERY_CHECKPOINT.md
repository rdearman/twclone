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
- Known blockers: `events.resume` is compile-gated and unregistered; MSL tests request an unregistered cron command; quasar/Captain Z expectations require contract decisions.
- Prior commits: `60967ed5`, `317d0576`, `caf3b9e3`.
- Remaining Ferengi issue: the temporary `codex_fixture_roster_only` case expects a new active definition to bypass the configured population target; this is a fixture/specification question, not a provisioning error.
- Next group: tavern fixtures and other high-count shared fixture groups.
