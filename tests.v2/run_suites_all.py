#!/usr/bin/env python3
import argparse
import glob
import json
import os
import ssl
import socket
import subprocess
import sys
import tempfile
from pathlib import Path
from datetime import datetime, timezone
from json_runner import JsonSuiteRunner

# Defaults
HOST = "localhost"
PORT = 1234

def discover_json_suites(base_dir):
    candidates = set(glob.glob(os.path.join(base_dir, "suite_*.json")))
    candidates.update(glob.glob(os.path.join(base_dir, "test_*.json")))
    return sorted(
        path for path in candidates
        if os.path.basename(path) not in {
            "suite_regression_full.json",  # manifest, not a suite
            "test_rig.json", "test_setup.json",
        }
    )


def load_suite(path):
    with open(path, encoding="utf-8") as source:
        return json.load(source)


def tls_listener_available(host, port, timeout=2):
    """Probe TLS without sending application data or changing server state."""
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    context.check_hostname = False
    context.verify_mode = ssl.CERT_NONE
    try:
        with socket.create_connection((host, port), timeout=timeout) as raw:
            with context.wrap_socket(raw, server_hostname=host):
                return True
    except (OSError, ssl.SSLError):
        return False


def nested_case_count(data):
    """Count cases in the documented legacy nested-suite shape for reports."""
    return sum(
        len(group.get("tests", []))
        for group in data.get("suites", [])
        if isinstance(group, dict) and isinstance(group.get("tests", []), list)
    )


def _case_statuses(report):
    """Return deterministic case statuses, including compatibility with old reports."""
    statuses = {}
    for suite in report.get("suites", []):
        suite_name = os.path.basename(suite.get("suite", "suite"))
        for index, case in enumerate(suite.get("cases", []), 1):
            case_id = case.get("id") or f"{suite_name}::case-{index:04d}"
            statuses[case_id] = case.get("status", "unknown")
    return statuses


def _baseline_diff(previous, current):
    before = _case_statuses(previous)
    after = _case_statuses(current)
    changed = []
    for case_id in sorted(set(before) | set(after)):
        old = before.get(case_id, "not_present")
        new = after.get(case_id, "not_present")
        if old != new:
            changed.append({"id": case_id, "before": old, "after": new})
    return {
        "cases_compared": len(set(before) | set(after)),
        "changed": changed,
        "newly_passed": [item["id"] for item in changed
                          if item["after"] == "passed" and item["before"] != "passed"],
        "newly_failed": [item["id"] for item in changed
                          if item["after"] == "failed" and item["before"] != "failed"],
    }


def _atomic_write_json(path, payload):
    """Write JSON beside its destination, then atomically replace it."""
    destination = Path(path)
    destination.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(
        prefix=destination.name + ".", suffix=".tmp", dir=destination.parent
    )
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as output:
            json.dump(payload, output, indent=2)
            output.write("\n")
            output.flush()
            os.fsync(output.fileno())
        os.replace(temporary, destination)
    except Exception:
        try:
            os.unlink(temporary)
        except FileNotFoundError:
            pass
        raise


def _is_completed_report(report):
    if not isinstance(report, dict):
        return False
    if report.get("status") == "completed":
        return True
    suites = report.get("suites", [])
    return bool(suites) and all(
        suite.get("status") in ("passed", "failed") for suite in suites
    ) and not report.get("not_executed")


def _publish_completed_report(report_path, temporary_path, report):
    """Rotate only a completed report; blocked runs never touch retained files."""
    report_path = Path(report_path)
    previous_path = report_path.with_name("regression_previous.json")
    failures_path = report_path.with_name("regression_failures.json")

    if report_path.exists():
        try:
            with report_path.open(encoding="utf-8") as source:
                prior = json.load(source)
            if _is_completed_report(prior):
                _atomic_write_json(previous_path, prior)
        except (OSError, json.JSONDecodeError):
            pass

    os.replace(temporary_path, report_path)
    failed_cases = []
    for suite in report.get("suites", []):
        for case in suite.get("cases", []):
            if case.get("status") != "failed":
                continue
            failed_cases.append({
                "suite": os.path.basename(suite.get("suite", "")),
                "id": case.get("id"),
                "name": case.get("name"),
                "command": case.get("command"),
                "status": case.get("status"),
                "runner_classification": case.get("classification"),
                "primary_classification": "UNCLASSIFIED",
                "root_cause_group": None,
                "evidence": {
                    "expected": case.get("expected"),
                    "assertions": case.get("assertions", []),
                    "actual": case.get("actual"),
                    "failure_reason": case.get("failure_reason"),
                    "runner_error": case.get("runner_error"),
                },
                "proposed_action": "Review this case's request, V3 contract, fixture and implementation evidence before classifying.",
                "confidence": "unknown",
            })
    failure_inventory = {
        "generated_at": report.get("finished_at"),
        "run_status": report.get("status"),
        "summary": report.get("summary", {}),
        "classification_counts": {
            "IMPLEMENTATION_DEFECT": 0,
            "FIXTURE_DEFECT": 0,
            "OBSOLETE_V2_TEST": 0,
            "PROTOCOL_SCHEMA_MISMATCH": 0,
            "MISSING_IMPLEMENTATION": 0,
            "SPECIFICATION_BLOCKER": 0,
            "INFRASTRUCTURE": 0,
            "UNCLASSIFIED": len(failed_cases),
        },
        "blocked_cases": report.get("summary", {}).get("blocked", 0),
        "unsupported_suites": report.get("unsupported", []),
        "failed_cases": failed_cases,
    }
    _atomic_write_json(failures_path, failure_inventory)


def server_listener_available(host, port):
    """Distinguish a request timeout from an actually dead test server."""
    try:
        with socket.create_connection((host, port), timeout=1):
            return True
    except OSError:
        return False


def regression_exit_code(report, failed):
    """A skipped-only run is not a successful test execution."""
    if failed:
        return 1
    if report.get("summary", {}).get("total_suites_executed", 0) == 0:
        return 2
    return 0


def run_all():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default=os.environ.get("TWCLONE_TEST_HOST", HOST))
    parser.add_argument("--port", type=int, default=int(os.environ.get("TWCLONE_TEST_PORT", PORT)))
    parser.add_argument("--client-idle-timeout-ms", type=int, default=8000,
                        help="Reconnect cached clients before the server's idle request deadline")
    parser.add_argument("--suite", action="append", help="Run only this suite; repeatable")
    parser.add_argument("--capture-responses")
    parser.add_argument("--report", default="tests.v2/regression_report.json")
    parser.add_argument("--baseline", help="Previous report to compare by stable case ID")
    parser.add_argument("--rig", action="store_true",
                        help="Reset and apply the canonical fixture to an explicitly isolated QA database")
    parser.add_argument("--dbname", default=os.environ.get("QA_PGDATABASE"))
    parser.add_argument("--pg-host", default=os.environ.get("QA_PGHOST", "127.0.0.1"))
    parser.add_argument("--pg-port", type=int, default=int(os.environ.get("QA_PGPORT", "55438")))
    parser.add_argument("--pg-user", default=os.environ.get("QA_PGUSER", "postgres"))
    args = parser.parse_args()

    # Get the directory where this script is located
    base_dir = os.path.dirname(os.path.abspath(__file__))
    
    report_path = Path(args.report).resolve()
    temporary_report_path = report_path.with_name(report_path.name + ".tmp")
    previous_path = report_path.with_name("regression_previous.json")
    baseline_path = args.baseline
    if baseline_path is None:
        for candidate in (report_path, previous_path):
            try:
                with candidate.open(encoding="utf-8") as source:
                    candidate_report = json.load(source)
                if _is_completed_report(candidate_report):
                    baseline_path = str(candidate)
                    break
            except (OSError, json.JSONDecodeError):
                continue
    previous_report = None
    if baseline_path and os.path.exists(baseline_path):
        try:
            with open(baseline_path, encoding="utf-8") as source:
                previous_report = json.load(source)
        except (OSError, json.JSONDecodeError):
            previous_report = None

    report = {
        "started_at": datetime.now(timezone.utc).isoformat(),
        "host": args.host,
        "port": args.port,
        "database_rigged": False,
        "suites": [],
        "skipped": [],
        "unsupported": [],
        "blocked": [],
    }

    all_json_files = sorted(glob.glob(os.path.join(base_dir, "*.json")))
    all_json_suites = discover_json_suites(base_dir)
    manifest_path = os.path.join(base_dir, "suite_regression_full.json")
    report["manifest"] = manifest_path
    report["manifest_is_metadata"] = True

    if args.rig:
        if not args.dbname:
            parser.error("--rig requires --dbname or QA_PGDATABASE")
        rig_script = os.path.join(base_dir, "rig_db.py")
        rig_config = os.path.join(base_dir, "test_rig.json")
        cmd = [sys.executable, rig_script, "--config", rig_config, "--reset",
               "--dbname", args.dbname, "--host", args.pg_host,
               "--port", str(args.pg_port), "--user", args.pg_user]
        result = subprocess.run(cmd)
        if result.returncode != 0:
            report["blocked"].append({"reason": "database rigging failed", "returncode": result.returncode})
            report["status"] = "blocked"
            _atomic_write_json(temporary_report_path, report)
            return 2
        report["database_rigged"] = True

    runner = JsonSuiteRunner(
        args.host,
        args.port,
        macros_path=os.path.join(base_dir, "macros.json"),
        capture_responses=args.capture_responses,
        client_idle_timeout_ms=args.client_idle_timeout_ms,
    )

    candidate_suites = discover_json_suites(base_dir)
    if args.suite:
        base_path = Path(base_dir).resolve()
        requested = []
        for item in args.suite:
            path = Path(item).resolve()
            if base_path not in path.parents or not path.is_file():
                parser.error(f"--suite must name an existing JSON file under {base_path}")
            requested.append(str(path))
        candidate_suites = sorted(set(requested))

    suites_to_run = []
    for path in candidate_suites:
        data = load_suite(path)
        if not isinstance(data.get("tests"), list):
            report["skipped"].append({
                "suite": path,
                "reason": "legacy nested step DSL is not a direct JSON-RPC suite",
                "cases_declared": nested_case_count(data),
            })
            continue
        legacy_requests = any(
            isinstance(test, dict)
            and ("request" in test or "requests" in test)
            and "command" not in test
            for test in data.get("tests", [])
        )
        if data.get("config", {}).get("tls"):
            tls_available = tls_listener_available(args.host, args.port)
            if legacy_requests or not tls_available:
                if not tls_available:
                    reason = "suite requires TLS but the active test-server listener did not complete a TLS handshake"
                else:
                    reason = "TLS listener is available, but suite cases use the unnormalized legacy request/sequence DSL"
                report["skipped"].append({
                    "suite": path,
                    "reason": reason,
                    "cases_declared": len(data.get("tests", [])),
                })
                continue
        suites_to_run.append(path)

    try:
        with socket.create_connection((args.host, args.port), timeout=2):
            server_available = True
    except OSError as error:
        server_available = False
        report["blocked"].append({
            "reason": "game server unavailable",
            "endpoint": f"{args.host}:{args.port}",
            "error": str(error),
        })

    if not suites_to_run and not report["skipped"]:
        print("ERROR: No test suites found!")
        sys.exit(1)

    _atomic_write_json(temporary_report_path, report)

    compatible_suites = list(suites_to_run)
    if not server_available:
        # A missing listener blocks the run; it does not execute every suite
        # and manufacture one failure per case.  Preserve the discovered
        # suite list as explicitly not executed in the report.
        for remaining in compatible_suites:
            report["suites"].append({
                "suite": remaining,
                "status": "not_executed",
                "reason": "game server unavailable before run",
                "cases": [],
                "total_cases": len(load_suite(remaining).get("tests", [])),
            })
        suites_to_run = []

    failed = []
    for index, suite in enumerate(suites_to_run):
        print(f"\n>>> EXECUTING SUITE: {suite}")
        
        if server_available:
            success = runner.run_suite(suite)
            suite_report = runner.last_suite_report or {
                "suite": suite, "status": "blocked", "cases": []
            }
        else:
            success = False
            suite_report = {
                "suite": suite,
                "status": "blocked",
                "reason": "game server unavailable",
                "cases": [],
                "total_cases": 0,
            }
        report["suites"].append(suite_report)
        _atomic_write_json(temporary_report_path, report)

        # A lost listener is an infrastructure boundary, not a gameplay
        # assertion.  Stop here and explicitly account for the remaining
        # suites as not executed; continuing would manufacture hundreds of
        # misleading connection-refused failures.
        if server_available and any(
            case.get("classification") == "infrastructure_error"
            and any(token in case.get("runner_error", "") for token in (
                "ConnectionRefusedError", "ConnectionResetError",
                "BrokenPipeError", "TimeoutError", "ConnectionAbortedError",
            ))
            for case in suite_report.get("cases", [])
        ) and not server_listener_available(args.host, args.port):
            server_available = False
            report["blocked"].append({
                "reason": "game server became unavailable during suite",
                "suite": suite,
                "endpoint": f"{args.host}:{args.port}",
            })
            for remaining in suites_to_run[index + 1:]:
                report["suites"].append({
                    "suite": remaining,
                    "status": "not_executed",
                    "reason": "stopped after game server became unavailable",
                    "cases": [],
                    "total_cases": len(load_suite(remaining).get("tests", [])),
                })
            failed.append(suite)
            print(f">>> SUITE BLOCKED AFTER SERVER LOSS: {suite}")
            break
        
        if not success:
            failed.append(suite)
            print(f">>> SUITE FAILED: {suite}")
        else:
            print(f">>> SUITE PASSED: {suite}")

    report["finished_at"] = datetime.now(timezone.utc).isoformat()
    report["summary"] = {
        "total_json_files_discovered": len(all_json_files),
        "total_suites_discovered": len(all_json_suites),
        "total_suites_executed": sum(
            1 for suite in report["suites"]
            if suite.get("status") in ("passed", "failed")
        ),
        "total_cases_discovered": sum(
            len(load_suite(path).get("tests", [])) for path in compatible_suites
        ),
        "total_cases_executed": sum(len(s.get("cases", [])) for s in report["suites"]),
        "passed": sum(1 for s in report["suites"] for c in s.get("cases", []) if c.get("status") == "passed"),
        "failed": sum(1 for s in report["suites"] for c in s.get("cases", []) if c.get("status") == "failed"),
        "skipped": sum(item.get("cases_declared", 0) for item in report["skipped"]),
        "skipped_suites": len(report["skipped"]),
        "blocked": len(report["blocked"]) + sum(
            1 for suite in report["suites"] if suite.get("status") == "blocked"
        ),
        "unsupported": len(report["unsupported"]),
        "not_executed": sum(1 for s in report["suites"] if s.get("status") == "not_executed"),
    }
    completed_suite_count = sum(
        1 for suite in report["suites"]
        if suite.get("status") in ("passed", "failed")
    )
    report["status"] = (
        "completed" if completed_suite_count == len(compatible_suites)
        and completed_suite_count > 0 else "incomplete"
    )
    if previous_report is not None:
        report["baseline"] = os.path.abspath(baseline_path)
        report["baseline_diff"] = _baseline_diff(previous_report, report)
    _atomic_write_json(temporary_report_path, report)
    if report["status"] == "completed":
        _publish_completed_report(report_path, temporary_report_path, report)

    print("\n" + "="*40)
    if failed:
        print(f"FAILURES: {len(failed)} suites failed.")
        for f in failed:
            print(f" - {f}")
        sys.exit(regression_exit_code(report, failed))

    executed_suites = report["summary"]["total_suites_executed"]
    if executed_suites == 0:
        print("NO SUITES EXECUTED; requested suites were skipped or blocked.")
        sys.exit(regression_exit_code(report, failed))

    skipped_suites = report["summary"]["skipped_suites"]
    skipped_cases = report["summary"]["skipped"]
    print(f"ALL EXECUTED SUITES PASSED ({executed_suites}); "
          f"{skipped_suites} suite(s) skipped ({skipped_cases} declared cases).")
    sys.exit(regression_exit_code(report, failed))

if __name__ == "__main__":
    run_all()
