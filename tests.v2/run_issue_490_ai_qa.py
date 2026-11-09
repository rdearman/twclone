#!/usr/bin/env python3
"""Run the issue #490 scenario catalog against a disposable loopback QA server.

The JSON catalog is the acceptance contract. Existing gameplay suites remain the
executable cases; this wrapper supplies strict endpoint/capture guards and a
single redacted JSONL evidence stream. It never creates, migrates, or resets a
database and refuses non-loopback endpoints.
"""

import argparse
import hashlib
import json
import os
import re
import socket
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests.v2"))

from json_runner import JsonSuiteRunner

CATALOG = ROOT / "tests.v2" / "issue_490_ai_qa_scenarios.json"

SUITES = {
    "PLAN-01-LANDING": "tests.v2/suite_planet_landing_defense.json",
    "SHIP-02-OWNERSHIP": "tests.v2/suite_shipyard_and_ships.json",
    "ECO-01-TRADE": "tests.v2/suite_port_fixed_modes.json",
    "ECO-02-BANK-INTEREST": "tests.v2/suite_economy_and_bank.json",
    "VIS-01-VISIBILITY": "tests.v2/suite_information_fog.json",
    "HAZ-01-ENTRY": "tests.v2/suite_combat_sector_hazards.json",
    "PLAN-02-PLANET-LIFECYCLE": "tests.v2/suite_combat_planet_ops.json",
    "INS-01-INSURANCE": "tests.v2/suite_economy_insurance.json",
}

DISALLOWED_SQL = re.compile(
    r"\b(?:alter|create|drop|grant|revoke|truncate|vacuum|refresh|reindex|cluster|delete|copy)\b",
    re.IGNORECASE,
)


def validate_qa_database(database_name: str) -> None:
    """Require an explicitly named disposable QA database."""
    if not database_name.startswith("twclone_v3qa_"):
        raise SystemExit(
            "REFUSED: issue #490 requires a disposable database named twclone_v3qa_*"
        )


def validate_fixture_sql(test):
    """Permit only admin fixture/evidence SQL, never schema or cluster changes."""
    if test.get("command") != "sys.raw_sql_exec":
        return
    if test.get("user") != "admin":
        raise SystemExit("REFUSED: sys.raw_sql_exec is restricted to the admin fixture user")
    sql = str(test.get("data", {}).get("sql", ""))
    if not sql.strip():
        raise SystemExit("REFUSED: empty fixture SQL")
    if DISALLOWED_SQL.search(sql):
        raise SystemExit("REFUSED: schema/cluster SQL is not allowed by the issue #490 harness")


def validate_evidence_step(test):
    """Evidence-labelled steps must be admin SELECTs, never gameplay mutations."""
    name = str(test.get("name", "")).lower()
    if not any(token in name for token in ("snapshot", "event persistence", "event evidence")):
        return
    if test.get("command") != "sys.raw_sql_exec" or test.get("user") != "admin":
        raise SystemExit("REFUSED: evidence steps must use admin sys.raw_sql_exec")
    sql = str(test.get("data", {}).get("sql", "")).lstrip().lower()
    if not sql.startswith("select "):
        raise SystemExit("REFUSED: evidence steps must be read-only SELECT queries")


def validate_suite_evidence(suite_path: str) -> None:
    """Require response, database and event evidence in the selected suite."""
    with open(suite_path, encoding="utf-8") as stream:
        suite = json.load(stream)
    tests = suite.get("tests", [])
    commands = {step.get("command") for step in tests}
    if not commands.intersection({"planet.land", "ship.list", "trade.buy", "bank.balance", "move.warp"}):
        raise SystemExit("REFUSED: selected suite has no ordinary-player gameplay command")
    snapshots = [step for step in tests if "snapshot" in str(step.get("name", "")).lower()]
    if len(snapshots) < 2:
        raise SystemExit("REFUSED: selected suite must include before/after database snapshots")
    if not any("engine_events" in str(step.get("data", {}).get("sql", "")) for step in tests):
        raise SystemExit("REFUSED: selected suite must query engine_events evidence")
    for step in tests:
        validate_evidence_step(step)


def validate_scenario_contract(scenario):
    """Keep the AI QA plan separate from gameplay policy and evidence rules."""
    if not scenario.get("requirement_id"):
        raise ValueError(f"scenario {scenario.get('id')} has no requirement ID")
    commands = scenario.get("gameplay_commands", [])
    if not commands or any(command == "sys.raw_sql_exec" for command in commands):
        raise ValueError(
            f"scenario {scenario.get('id')} must contain ordinary-player commands only"
        )
    cases = scenario.get("required_cases", [])
    if len(cases) < 2:
        raise ValueError(f"scenario {scenario.get('id')} needs positive and negative cases")
    evidence = scenario.get("before_after_queries", [])
    if len(evidence) < 2 or not any("event" in item.lower() for item in evidence):
        raise ValueError(
            f"scenario {scenario.get('id')} must require state and event evidence"
        )
    for field in ("ordinary_player_flow", "refusal_invariants", "success_invariants", "event_filters"):
        if not scenario.get(field):
            raise ValueError(f"scenario {scenario.get('id')} must define {field}")
    if not isinstance(scenario.get("restart_required"), bool):
        raise ValueError(f"scenario {scenario.get('id')} must define restart_required")


class SafeIssue490Runner(JsonSuiteRunner):
    """JSON runner with issue-specific database and SQL safety gates."""

    def _run_test(self, test):
        validate_fixture_sql(test)
        validate_evidence_step(test)
        return super()._run_test(test)

    def _capture_response(self, test_name, command, response):
        super()._capture_response(test_name, command, response)
        if not self.capture_responses or command != "sys.raw_sql_exec":
            return
        lower_name = test_name.lower()
        if not any(token in lower_name for token in ("snapshot", "event persistence", "event evidence")):
            return
        rows = ((response.get("data") or {}).get("rows") or []) if isinstance(response, dict) else []
        evidence_type = "events" if "engine_events" in lower_name else "database"
        record = {
            "kind": "evidence",
            "suite": self.active_suite,
            "test": test_name,
            "evidence_type": evidence_type,
            "status": response.get("status") if isinstance(response, dict) else None,
            "row_count": len(rows),
            "response_sha256": hashlib.sha256(
                json.dumps(self._redact_response(response), sort_keys=True).encode("utf-8")
            ).hexdigest(),
        }
        with open(self.capture_responses, "a", encoding="utf-8") as output:
            output.write(json.dumps(record, separators=(",", ":"), sort_keys=True))
            output.write("\n")


def loopback(host: str) -> bool:
    try:
        return host in {"localhost", "127.0.0.1", "::1"} or all(
            ip.startswith("127.") for ip in socket.gethostbyname_ex(host)[2]
        )
    except OSError:
        return False


def load_catalog():
    with CATALOG.open(encoding="utf-8") as stream:
        catalog = json.load(stream)
    if catalog.get("requirement_id") != "V3-PLAN-01":
        raise ValueError("catalog requirement must remain V3-PLAN-01")
    for scenario in catalog.get("scenarios", []):
        validate_scenario_contract(scenario)
        if scenario["id"] not in SUITES:
            raise ValueError(f"no executable suite mapped for {scenario['id']}")
        if scenario.get("execution_status") not in {"next-unblocked", "deferred", "decision-gated"}:
            raise ValueError(f"invalid execution status for {scenario['id']}")
    return catalog


def next_unblocked_scenarios(catalog):
    """Return only the role-owned scenario currently cleared by the ledger."""
    return [
        scenario["id"] for scenario in catalog.get("scenarios", [])
        if scenario.get("execution_status") == "next-unblocked"
    ]


def record_runner_failure(capture: Path, *, scenario_id: str, host: str,
                          port: int, error: OSError) -> None:
    """Persist an infrastructure failure without leaking connection details."""
    record = {
        "kind": "runner_failure",
        "scenario": scenario_id,
        "failure_class": "infrastructure",
        "error_type": type(error).__name__,
        "error": str(error),
        "endpoint": {"host": host, "port": port},
    }
    with capture.open("a", encoding="utf-8") as output:
        output.write(json.dumps(record, separators=(",", ":"), sort_keys=True))
        output.write("\n")


def record_run_metadata(capture: Path, *, requirement_id: str, scenarios, host: str,
                        port: int, qa_database: str) -> None:
    record = {
        "kind": "run_metadata",
        "requirement_id": requirement_id,
        "scenarios": scenarios,
        "endpoint": {"host": host, "port": port},
        "qa_database": qa_database,
        "ordinary_player_policy": True,
        "admin_sql_scope": "fixture_and_read_only_evidence",
    }
    with capture.open("a", encoding="utf-8") as output:
        output.write(json.dumps(record, separators=(",", ":"), sort_keys=True))
        output.write("\n")


def main(argv=None):
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default=os.environ.get("TWCLONE_TEST_HOST", "127.0.0.1"))
    parser.add_argument("--port", type=int, default=int(os.environ.get("TWCLONE_TEST_PORT", "12483")))
    parser.add_argument("--capture", required=True, help="redacted JSONL output under the disposable QA directory")
    parser.add_argument(
        "--qa-database",
        default=os.environ.get("TWCLONE_QA_DATABASE"),
        help="explicit disposable database name; must start with twclone_v3qa_",
    )
    parser.add_argument("--scenario", action="append", choices=sorted(SUITES), help="run selected scenario IDs")
    args = parser.parse_args(argv)

    load_catalog()
    if not args.qa_database:
        raise SystemExit(
            "REFUSED: pass --qa-database twclone_v3qa_<name> (or set TWCLONE_QA_DATABASE)"
        )
    validate_qa_database(args.qa_database)
    if not loopback(args.host):
        raise SystemExit("REFUSED: issue #490 runner only accepts a loopback QA endpoint")
    capture = Path(args.capture).resolve()
    if not (str(capture).startswith("/tmp/") or str(capture).startswith(str(ROOT) + os.sep)):
        raise SystemExit("REFUSED: response capture must be in /tmp or this repository")
    capture.parent.mkdir(parents=True, exist_ok=True)

    catalog = load_catalog()
    selected = args.scenario or next_unblocked_scenarios(catalog)
    deferred = {
        scenario["id"]: scenario.get("execution_status")
        for scenario in catalog.get("scenarios", [])
        if scenario.get("execution_status") != "next-unblocked"
    }
    refused = [scenario_id for scenario_id in selected if scenario_id in deferred]
    if refused:
        details = ", ".join(f"{scenario_id}={deferred[scenario_id]}" for scenario_id in refused)
        raise SystemExit(
            "REFUSED: selected issue #490 scenarios are not next-unblocked: " + details
        )
    for scenario_id in selected:
        validate_suite_evidence(SUITES[scenario_id])
    record_run_metadata(
        capture,
        requirement_id=catalog["requirement_id"],
        scenarios=selected,
        host=args.host,
        port=args.port,
        qa_database=args.qa_database,
    )
    runner = SafeIssue490Runner(args.host, args.port, capture_responses=str(capture))
    failed = []
    for scenario_id in selected:
        suite = SUITES[scenario_id]
        print(f"\n>>> SCENARIO {scenario_id}: {suite}")
        try:
            passed = runner.run_suite(suite)
        except OSError as error:
            record_runner_failure(
                capture, scenario_id=scenario_id, host=args.host,
                port=args.port, error=error,
            )
            print(f"BLOCKED (infrastructure error: {type(error).__name__}: {error})")
            passed = False
        if not passed:
            failed.append(scenario_id)
    if failed:
        print("FAILED SCENARIOS: " + ", ".join(failed))
        return 1
    print("ALL SELECTED SCENARIOS PASSED")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
