"""Safe evidence harness for ordinary-player V3 acceptance journeys.

The harness is deliberately adapter-based: it never connects to a server or
database by itself and never mutates fixtures.  A live adapter may be supplied
by an operator for an explicitly disposable QA universe; tests use the fake
adapter below the module boundary.  Canonical expectations and AI policy are
kept in separate fields in the emitted manifest.
"""

from __future__ import annotations

import copy
import json
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Protocol


SECRET_FIELDS = {
    "password", "passwd", "session", "session_id", "session_token",
    "access_token", "refresh_token", "authorization", "secret",
}

_FORBIDDEN_ORDINARY_COMMAND_PREFIXES = ("admin.", "sys.")
_FORBIDDEN_ORDINARY_COMMANDS = {
    "migrate",
    "database.reset",
    "fixture.seed",
    "sql.exec",
}


def _validate_ordinary_player_commands(commands: list[dict[str, Any]]) -> None:
    """Reject privileged or fixture-mutating commands before adapter reads."""
    for index, command in enumerate(commands, start=1):
        name = command.get("command") if isinstance(command, dict) else None
        if not isinstance(name, str) or not name:
            raise ValueError(f"command {index} is missing an ordinary-player command name")
        if name in _FORBIDDEN_ORDINARY_COMMANDS or name.startswith(
            _FORBIDDEN_ORDINARY_COMMAND_PREFIXES
        ):
            raise ValueError(f"command {index} is outside the ordinary-player boundary: {name}")


def redact(value: Any) -> Any:
    """Return a JSON-shaped value with credentials and session material removed."""
    if isinstance(value, dict):
        return {
            key: "[REDACTED]" if str(key).lower() in SECRET_FIELDS else redact(item)
            for key, item in value.items()
        }
    if isinstance(value, list):
        return [redact(item) for item in value]
    if isinstance(value, tuple):
        return [redact(item) for item in value]
    return value


class LandingAdapter(Protocol):
    """Read/command boundary supplied by a disposable QA integration."""

    def read_state(self, label: str) -> dict[str, Any]: ...

    def send(self, command: dict[str, Any]) -> dict[str, Any]: ...

    def read_events(self) -> list[dict[str, Any]]: ...

    def read_db(self, label: str) -> dict[str, Any]: ...


class ScenarioAdapter(Protocol):
    """Read/command boundary for a disposable ordinary-player scenario."""

    def read_state(self, label: str) -> dict[str, Any]: ...

    def send(self, command: dict[str, Any]) -> dict[str, Any]: ...

    def read_events(self) -> list[dict[str, Any]]: ...

    def read_db(self, label: str) -> dict[str, Any]: ...


def _matches_expected(actual: Any, expected: Any) -> bool:
    """Match a small JSON-shaped expectation without hiding extra state."""
    if isinstance(expected, dict):
        return isinstance(actual, dict) and all(
            key in actual and _matches_expected(actual[key], value)
            for key, value in expected.items()
        )
    if isinstance(expected, list):
        return actual == expected
    return actual == expected


@dataclass
class EvidenceRecorder:
    """Write one sanitized, replayable evidence bundle."""

    root: Path
    scenario_id: str
    run_id: str
    records: list[dict[str, Any]] = field(default_factory=list)

    def __post_init__(self) -> None:
        self.root.mkdir(parents=True, exist_ok=True)

    def add(self, kind: str, payload: Any, *, label: str | None = None) -> None:
        record = {"kind": kind, "payload": redact(copy.deepcopy(payload))}
        if label is not None:
            record["label"] = label
        self.records.append(record)

    def write(self, *, status: str, canonical: dict[str, Any], ai_policy: dict[str, Any],
              outcome: dict[str, Any]) -> Path:
        manifest = {
            "scenario_id": self.scenario_id,
            "run_id": self.run_id,
            "created_at": datetime.now(timezone.utc).isoformat().replace("+00:00", "Z"),
            "status": status,
            "canonical_requirements": redact(canonical),
            "ai_policy": redact(ai_policy),
            "outcome": redact(outcome),
            "records": self.records,
        }
        path = self.root / "evidence.json"
        path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        return path


def _state_delta(before: dict[str, Any], after: dict[str, Any]) -> dict[str, Any]:
    keys = ("sector_id", "turns_remaining", "landed_planet", "onplanet", "ship_id")
    return {key: (before.get(key), after.get(key)) for key in keys if before.get(key) != after.get(key)}


def _landing_state_delta(before: dict[str, Any], after: dict[str, Any]) -> dict[str, Any]:
    """Return only state that a refused landing must never change.

    Planetary defenses may legitimately change hull, shields, fighters, or the
    event journal while refusing the landing. Those effects are evidence to
    inspect, but they are not a turn/landing-state mutation.
    """
    keys = ("sector_id", "turns_remaining", "landed_planet", "onplanet", "ship_id")
    return {key: (before.get(key), after.get(key)) for key in keys if before.get(key) != after.get(key)}


def _evidence_shape_errors(
    *,
    state_before: Any,
    state_after: Any,
    db_before: Any,
    db_after: Any,
    response: Any,
    events: Any,
    prefix: str = "",
) -> list[str]:
    """Fail closed when an adapter did not provide an evidence boundary.

    A live adapter is intentionally outside this module, so protocol success
    cannot make absent snapshots or event batches look like acceptance proof.
    The raw values are still recorded by the caller (and redacted by
    ``EvidenceRecorder``), allowing an operator to diagnose the adapter.
    """
    errors: list[str] = []
    label = f"{prefix} " if prefix else ""
    for name, value in (
        ("state before", state_before),
        ("state after", state_after),
        ("database snapshot before", db_before),
        ("database snapshot after", db_after),
        ("response", response),
    ):
        if not isinstance(value, dict):
            errors.append(f"{label}{name} evidence is not an object")
    if not isinstance(events, list):
        errors.append(f"{label}event batch evidence is not a list")
    return errors


def run_planet_landing(
    adapter: LandingAdapter,
    output_dir: Path,
    *,
    planet_id: int,
    case_id: str,
    expected: str,
    canonical_requirements: dict[str, Any],
    ai_policy: dict[str, Any],
    run_id: str = "local-qa",
    command_id: str = "planet-land-1",
) -> dict[str, Any]:
    """Run one landing attempt and persist responses, events and DB snapshots.

    ``expected`` is an operator-selected case classification (``success`` or
    ``refusal``), not a substitute for a version-pinned canonical decision.
    Refusal cases are checked for unchanged gameplay state; success cases only
    require the protocol's landing identity and state transition.  Turn cost
    remains an explicit canonical input rather than a hard-coded AI rule.

    A live acceptance adapter may expose ``read_restart_state`` and
    ``read_restart_db`` after the operator has restarted/reconnected to the
    disposable QA server.  When ``restart_required`` is declared, those
    readbacks are mandatory; the harness never restarts a service itself.
    """
    recorder = EvidenceRecorder(output_dir, case_id, run_id)
    before = adapter.read_state("before")
    db_before = adapter.read_db("before")
    recorder.add("state", before, label="before")
    recorder.add("db_snapshot", db_before, label="before")

    command = {"id": command_id, "command": "planet.land", "data": {"planet_id": planet_id}}
    recorder.add("request", command)
    response = adapter.send(command)
    recorder.add("response", response)
    events = adapter.read_events()
    recorder.add("events", events)

    after = adapter.read_state("after")
    db_after = adapter.read_db("after")
    recorder.add("state", after, label="after")
    recorder.add("db_snapshot", db_after, label="after")

    restart_state = None
    restart_db = None
    restart_required = bool(canonical_requirements.get("restart_required", False))
    restart_reader = getattr(adapter, "read_restart_state", None)
    restart_db_reader = getattr(adapter, "read_restart_db", None)
    if restart_reader is not None and restart_db_reader is not None:
        restart_state = restart_reader()
        restart_db = restart_db_reader()
        recorder.add("state", restart_state, label="after_restart")
        recorder.add("db_snapshot", restart_db, label="after_restart")

    status = response.get("status") if isinstance(response, dict) else None
    response_type = response.get("type") if isinstance(response, dict) else None
    errors = _evidence_shape_errors(
        state_before=before,
        state_after=after,
        db_before=db_before,
        db_after=db_after,
        response=response,
        events=events,
    )
    if restart_required and (restart_state is None or restart_db is None):
        errors.append("restart-required case lacks post-restart state/database readback")
    if restart_state is not None and restart_db is not None:
        if restart_state != after:
            errors.append("post-restart gameplay state differs from immediate readback")
        if restart_db != db_after:
            errors.append("post-restart database snapshot differs from immediate readback")
    if expected == "success":
        if status != "ok" or response_type != "planet.land.success":
            errors.append("successful case did not return planet.land.success")
        response_planet_id = (
            response.get("data", {}).get("planet_id")
            if isinstance(response, dict) and isinstance(response.get("data", {}), dict)
            else None
        )
        if response_planet_id is not None and response_planet_id != planet_id:
            errors.append("successful response identifies a different planet")
        if after.get("landed_planet") != planet_id or after.get("onplanet") is not True:
            errors.append("successful case did not land the ordinary player's ship")
        if canonical_requirements.get("event_required", False) and not events:
            errors.append("successful case did not append a landing event")
        if canonical_requirements.get("database_must_change", False) and db_before == db_after:
            errors.append("successful case did not change the authoritative database snapshot")
        turn_cost = canonical_requirements.get("turn_cost")
        if isinstance(turn_cost, int) and turn_cost >= 0:
            before_turns = before.get("turns_remaining")
            after_turns = after.get("turns_remaining")
            if not isinstance(before_turns, int) or not isinstance(after_turns, int):
                errors.append("numeric canonical turn cost requires integer turn snapshots")
            elif before_turns - after_turns != turn_cost:
                errors.append(
                    f"successful case consumed {before_turns - after_turns} turns; "
                    f"expected {turn_cost}"
                )
    elif expected == "refusal":
        if status not in {"refused", "error"}:
            errors.append("refusal case returned success")
        if _landing_state_delta(before, after):
            errors.append("refusal mutated landing state or consumed a turn")
        if canonical_requirements.get("database_must_be_unchanged", True) and db_before != db_after:
            errors.append("refusal mutated the authoritative database snapshot")
    else:
        errors.append(f"unknown expected case classification: {expected}")

    outcome = {
        "case_id": case_id,
        "expected": expected,
        "response_status": status,
        "response_type": response_type,
        "state_delta": _state_delta(
            before if isinstance(before, dict) else {},
            after if isinstance(after, dict) else {},
        ),
        "database_changed": db_before != db_after,
        "restart_checked": restart_state is not None and restart_db is not None,
        "event_types": [event.get("type") if isinstance(event, dict) else None for event in events]
        if isinstance(events, list) else [],
        "event_count": len(events) if isinstance(events, list) else 0,
        "passed": not errors,
        "errors": errors,
    }
    evidence_status = "qualified_fixture" if not errors else "confirmed_fixture_failure"
    recorder.write(
        status=evidence_status,
        canonical=canonical_requirements,
        ai_policy=ai_policy,
        outcome=outcome,
    )
    return outcome


def run_ordinary_player_scenario(
    adapter: ScenarioAdapter,
    output_dir: Path,
    *,
    scenario_id: str,
    case_id: str,
    commands: list[dict[str, Any]],
    expected: str,
    canonical_requirements: dict[str, Any],
    ai_policy: dict[str, Any],
    run_id: str = "local-qa",
) -> dict[str, Any]:
    """Capture a multi-command journey without provisioning or direct DB writes.

    The adapter is intentionally injected: this function cannot connect to, seed,
    migrate, or reset a database.  Every command gets a sanitized request,
    response, event batch, and authoritative state/database before/after pair.
    ``expected`` is a scenario classification only; canonical requirements remain
    data in the evidence manifest and are never inferred from AI policy.
    """
    _validate_ordinary_player_commands(commands)
    recorder = EvidenceRecorder(output_dir, scenario_id, run_id)
    before = adapter.read_state("before")
    db_before = adapter.read_db("before")
    recorder.add("state", before, label="before")
    recorder.add("db_snapshot", db_before, label="before")

    responses: list[Any] = []
    event_batches: list[list[dict[str, Any]]] = []
    command_boundaries: list[dict[str, Any]] = []
    evidence_errors: list[str] = []
    for index, command in enumerate(commands, start=1):
        # Keep a read-only boundary around every mutation.  This is especially
        # important for a multi-command landing/ownership/trade journey: an
        # aggregate journey snapshot cannot show which command first changed
        # turns, ownership, cargo, or the ledger.  The adapter owns the reads;
        # this runner never writes rows or retries a command.
        before_label = f"command-{index}-before"
        after_label = f"command-{index}-after"
        command_state_before = adapter.read_state(before_label)
        command_db_before = adapter.read_db(before_label)
        recorder.add("state", command_state_before, label=before_label)
        recorder.add("db_snapshot", command_db_before, label=before_label)
        # A scenario definition may omit an id for readability, but the live
        # adapter and evidence bundle still need a stable replay key.  Derive
        # one from immutable scenario coordinates; never generate a random id
        # and never retry a mutation here.
        replay_command = copy.deepcopy(command)
        replay_command.setdefault(
            "id", f"{scenario_id}:{case_id}:command-{index}"
        )
        recorder.add("request", replay_command, label=f"command-{index}")
        response = adapter.send(replay_command)
        responses.append(response)
        recorder.add("response", response, label=f"command-{index}")
        events = adapter.read_events()
        event_batches.append(events)
        recorder.add("events", events, label=f"command-{index}")
        command_state_after = adapter.read_state(after_label)
        command_db_after = adapter.read_db(after_label)
        recorder.add("state", command_state_after, label=after_label)
        recorder.add("db_snapshot", command_db_after, label=after_label)
        evidence_errors.extend(_evidence_shape_errors(
            state_before=command_state_before,
            state_after=command_state_after,
            db_before=command_db_before,
            db_after=command_db_after,
            response=response,
            events=events,
            prefix=f"command-{index}",
        ))
        command_boundaries.append({
            "command": index,
            "state_changed": command_state_before != command_state_after,
            "database_changed": command_db_before != command_db_after,
        })

    # A mixed journey (for example, a successful mutation followed by a
    # duplicate/refused retry) must not hide a bad individual response behind
    # an otherwise valid aggregate result.  Expectations are supplied by the
    # scenario contract; they are never inferred from the AI policy.
    command_expectations = canonical_requirements.get("command_expectations", [])
    command_expectation_errors: list[str] = []
    if command_expectations and len(command_expectations) != len(commands):
        command_expectation_errors = [
            "command expectations must provide one entry per ordinary-player command"
        ]
    else:
        for index, expectation in enumerate(command_expectations):
            if not isinstance(expectation, dict):
                command_expectation_errors.append(
                    f"command-{index + 1} expectation is not an object"
                )
                continue
            response = responses[index]
            status = response.get("status") if isinstance(response, dict) else None
            allowed_statuses = expectation.get("statuses")
            if allowed_statuses is not None and status not in allowed_statuses:
                command_expectation_errors.append(
                    f"command-{index + 1} status {status!r} is not one of {allowed_statuses!r}"
                )
            expected_type = expectation.get("type")
            if expected_type is not None:
                response_type = response.get("type") if isinstance(response, dict) else None
                if response_type != expected_type:
                    command_expectation_errors.append(
                        f"command-{index + 1} response type {response_type!r} "
                        f"does not match {expected_type!r}"
                    )
            before_boundary = command_boundaries[index]
            if expectation.get("state_unchanged") and before_boundary["state_changed"]:
                command_expectation_errors.append(
                    f"command-{index + 1} changed authoritative gameplay state"
                )
            if expectation.get("database_unchanged") and before_boundary["database_changed"]:
                command_expectation_errors.append(
                    f"command-{index + 1} changed authoritative database state"
                )

    after = adapter.read_state("after")
    db_after = adapter.read_db("after")
    recorder.add("state", after, label="after")
    recorder.add("db_snapshot", db_after, label="after")

    errors = _evidence_shape_errors(
        state_before=before,
        state_after=after,
        db_before=db_before,
        db_after=db_after,
        response=responses[-1] if responses else None,
        events=event_batches[-1] if event_batches else None,
    )
    errors.extend(evidence_errors)
    errors.extend(command_expectation_errors)
    statuses = [response.get("status") if isinstance(response, dict) else None for response in responses]
    response_types = [response.get("type") if isinstance(response, dict) else None for response in responses]
    restart_state = None
    restart_db = None
    restart_required = bool(canonical_requirements.get("restart_required", False))
    restart_reader = getattr(adapter, "read_restart_state", None)
    restart_db_reader = getattr(adapter, "read_restart_db", None)
    if restart_reader is not None and restart_db_reader is not None:
        restart_state = restart_reader()
        restart_db = restart_db_reader()
        recorder.add("state", restart_state, label="after_restart")
        recorder.add("db_snapshot", restart_db, label="after_restart")
    if restart_required and (restart_state is None or restart_db is None):
        errors.append("restart-required scenario lacks post-restart state/database readback")
    if restart_state is not None and restart_db is not None:
        if restart_state != after:
            errors.append("post-restart gameplay state differs from immediate readback")
        if restart_db != db_after:
            errors.append("post-restart database snapshot differs from immediate readback")
    if expected == "success" and not responses:
        errors.append("successful scenario sent no commands")
    elif expected == "success" and any(status != "ok" for status in statuses):
        errors.append("successful scenario contained a non-success response")
    elif expected == "refusal" and any(status not in {"refused", "error"} for status in statuses):
        errors.append("refusal scenario contained a successful response")
    if expected == "refusal":
        if before != after:
            errors.append("refusal journey mutated authoritative gameplay state")
        if db_before != db_after:
            errors.append("refusal journey mutated the authoritative database snapshot")
    elif expected not in {"success", "refusal", "mixed", "deferred"}:
        errors.append(f"unknown expected scenario classification: {expected}")

    expected_types = canonical_requirements.get("response_types")
    if expected_types is not None and response_types != expected_types:
        errors.append(
            f"response type sequence {response_types!r} does not match "
            f"expected {expected_types!r}"
        )
    expected_final_state = canonical_requirements.get("final_state")
    if expected_final_state is not None and not _matches_expected(after, expected_final_state):
        errors.append("final authoritative state does not match the declared expectation")
    if canonical_requirements.get("database_must_change", False) and db_before == db_after:
        errors.append("scenario did not change the authoritative database snapshot")
    if canonical_requirements.get("database_must_be_unchanged", False) and db_before != db_after:
        errors.append("scenario changed the authoritative database snapshot")
    if canonical_requirements.get("event_required", False) and not any(event_batches):
        errors.append("scenario did not record an event batch")

    outcome = {
        "case_id": case_id,
        "expected": expected,
        "response_statuses": statuses,
        "response_types": response_types,
        "response_count": len(responses),
        "event_count": sum(len(events) if isinstance(events, list) else 0 for events in event_batches),
        "command_boundaries": command_boundaries,
        "state_changed": before != after,
        "db_changed": db_before != db_after,
        "restart_checked": restart_state is not None and restart_db is not None,
        "passed": not errors,
        "errors": errors,
    }
    evidence_status = "qualified_fixture" if not errors else "confirmed_fixture_failure"
    recorder.write(
        status=evidence_status,
        canonical=canonical_requirements,
        ai_policy=ai_policy,
        outcome=outcome,
    )
    return outcome

