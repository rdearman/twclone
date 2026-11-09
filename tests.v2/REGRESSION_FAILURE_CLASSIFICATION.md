# Regression failure classification

## Last verified completed run

The latest completed full run recorded 2,096 executed cases: 1,985 passed, 111 failed, 1 blocked, 2 unsupported, and zero server crashes. A subsequent full run did not complete: PostgreSQL was unavailable before database setup. Its attempt overwrote `tests.v2/regression_report.json` with a blocked-run summary.

| Primary classification | Cases |
| --- | ---: |
| IMPLEMENTATION_DEFECT | 0 |
| FIXTURE_DEFECT | 0 |
| OBSOLETE_V2_TEST | 0 |
| PROTOCOL_SCHEMA_MISMATCH | 0 |
| MISSING_IMPLEMENTATION | 0 |
| SPECIFICATION_BLOCKER | 0 |
| INFRASTRUCTURE | 0 |
| UNCLASSIFIED | 111 |
| **Failed total** | **111** |

Blocked (1) and unsupported (2) are tracked separately and are not included in the failed-case classification total.

## Evidence limitation

The preserved artifact `/tmp/twclone-v3-regression-8rk9qywp/responses.jsonl` has 1,986 rows with suite, stable case ID, test name, command, and actual response. It does not contain the runner's pass/fail verdict, expected response, or assertion results. The full report containing those verdicts is no longer present; the current report was replaced by the later PostgreSQL-blocked attempt. Suite files have also been edited since the completed run, so comparing the old responses against their current assertions would produce unreliable classifications.

Accordingly, the 111 are explicitly UNCLASSIFIED. The case-level `cases` list is empty because the retained evidence does not establish which of the 1,986 captured request IDs were among the 111 failures. No IDs or causes are inferred. This is an evidence blocker, not a claim that the failures share one root cause. The machine-readable record is [REGRESSION_FAILURE_CLASSIFICATION.json](REGRESSION_FAILURE_CLASSIFICATION.json).

The checkpoint contains aggregate-only clusters (planet landing/defense 12; tavern/trade 11; MSL enforcement 10; police D1 8; ship cargo 8; phase-3 port trading 6; combat advanced 6; event journal 6). These counts are preserved as probable leads, not as verified memberships or a partition of the 111.

## Next classification pass

When PostgreSQL is safely available, run the existing full regression against its disposable database and preserve a completed report to a separate durable path before the next attempt can overwrite it. The report must include each stable case ID, expected and actual values, and runner classification. Then classify every failed record individually; keep truly ambiguous cases UNCLASSIFIED. Do not count blocked or unsupported cases as passes.
