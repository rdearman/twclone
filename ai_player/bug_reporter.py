import os
import json
import logging
from datetime import datetime

logger = logging.getLogger(__name__)

_SECRET_FIELDS = {
    "password", "passwd", "session", "session_id", "session_token",
    "access_token", "refresh_token", "authorization", "secret",
}


def redact_secrets(value):
    """Return a JSON-shaped copy with credentials and session tokens removed."""
    if isinstance(value, dict):
        return {
            key: "[REDACTED]" if str(key).lower() in _SECRET_FIELDS else redact_secrets(item)
            for key, item in value.items()
        }
    if isinstance(value, list):
        return [redact_secrets(item) for item in value]
    if isinstance(value, tuple):
        return [redact_secrets(item) for item in value]
    return value

class BugReporter:
    def __init__(self, report_path=None):
        """
        Initializes the Bug Reporter.
        :param report_path: The directory to save bug reports to.
        """
        self.report_path = report_path or "bugs"
        if not os.path.exists(self.report_path):
            try:
                os.makedirs(self.report_path)
            except OSError as e:
                logger.error(f"Failed to create bug report directory: {e}")
                self.report_path = "." # Fallback to current dir
        
        # Define the path for the central QA log
        self.qa_log_file = os.path.join(self.report_path, "qa_log.jsonl")

    def _get_timestamp(self):
        """Returns a sortable timestamp string."""
        return datetime.utcnow().strftime("%Y%m%d_%H%M%S_%f")[:-3]

    def log_command(self, command_dict, state_before=None):
        """
        Logs a sent command to the central QA log.
        --- NEW FUNCTION ---
        """
        try:
            log_entry = {
                "timestamp": datetime.utcnow().isoformat() + "Z",
                "type": "command",
                "data": redact_secrets(command_dict),
                "state_before": redact_secrets(state_before) if state_before is not None else None,
            }
            with open(self.qa_log_file, 'a') as f:
                json.dump(log_entry, f)
                f.write('\n')
        except Exception as e:
            logger.error(f"Failed to log command: {e}")

    def log_response(self, response_dict, command=None, state_before=None):
        """
        Logs a received response to the central QA log.
        --- NEW FUNCTION ---
        """
        try:
            log_entry = {
                "timestamp": datetime.utcnow().isoformat() + "Z",
                "type": "response",
                "data": redact_secrets(response_dict),
                "command": redact_secrets(command) if command is not None else None,
                "state_before": redact_secrets(state_before) if state_before is not None else None,
            }
            with open(self.qa_log_file, 'a') as f:
                json.dump(log_entry, f)
                f.write('\n')
        except Exception as e:
            logger.error(f"Failed to log response: {e}")

    @staticmethod
    def _gameplay_snapshot(state):
        player = (state.get("player_info") or {}).get("player") or {}
        ship = state.get("ship_info") or {}
        return {
            "sector": state.get("player_location_sector", player.get("sector")),
            "credits": player.get("credits"),
            "turns_remaining": player.get("turns_remaining"),
            "ship_id": ship.get("id", player.get("ship_id")),
            "cargo": [
                {"commodity": item.get("commodity"), "quantity": item.get("quantity", 0)}
                for item in ship.get("cargo", []) if isinstance(item, dict)
            ],
        }

    def log_gameplay_outcome(self, *, identity, seed, command, response,
                             state_before, state_after, events=None,
                             state_after_source="player.my_info + ship.info readback",
                             verification_complete=True):
        """Compare an approved action with authoritative refreshed player state."""
        command_name = command.get("command")
        data = command.get("data") or {}
        response_data = response.get("data") or {}
        before = self._gameplay_snapshot(state_before)
        after = self._gameplay_snapshot(state_after)
        expected = {}
        actual = {}
        consistent = None
        succeeded = response.get("status") == "ok"

        if succeeded and verification_complete and command_name == "move.warp":
            expected["sector"] = data.get("to_sector_id")
            actual["sector"] = after.get("sector")
            consistent = str(expected["sector"]) == str(actual["sector"])
        elif succeeded and verification_complete and command_name in ("trade.buy", "trade.sell"):
            sign = 1 if command_name == "trade.buy" else -1
            before_qty = {}
            after_qty = {}
            for row in before["cargo"]:
                key = str(row.get("commodity", "")).upper()
                before_qty[key] = before_qty.get(key, 0) + int(row.get("quantity", 0) or 0)
            for row in after["cargo"]:
                key = str(row.get("commodity", "")).upper()
                after_qty[key] = after_qty.get(key, 0) + int(row.get("quantity", 0) or 0)
            requested = data.get("items") or []
            requested_delta = {}
            for row in requested:
                key = str(row.get("commodity", "")).upper()
                requested_delta[key] = requested_delta.get(key, 0) + sign * int(row.get("quantity", 0) or 0)
            receipt_lines = response_data.get("lines") or []
            expected_delta = {}
            for row in receipt_lines:
                key = str(row.get("commodity", "")).upper()
                quantity = row.get("quantity", row.get("units", 0))
                expected_delta[key] = expected_delta.get(key, 0) + sign * int(quantity or 0)
            if not receipt_lines:
                expected_delta = requested_delta
            actual_delta = {
                key: after_qty.get(key, 0) - before_qty.get(key, 0)
                for key in set(before_qty) | set(after_qty) | set(expected_delta)
            }
            expected["requested_cargo_delta"] = requested_delta
            expected["cargo_delta"] = expected_delta
            actual["cargo_delta"] = actual_delta
            expected["credits"] = response_data.get("credits_remaining")
            actual["credits"] = after.get("credits")
            consistent = expected_delta == actual_delta and (
                expected["credits"] is None or str(expected["credits"]) == str(actual["credits"])
            )

        outcome = {
            "ai_identity": identity,
            "test_seed": seed,
            "command": command,
            "response": response,
            "state_before": before,
            "state_after": after,
            "state_after_source": state_after_source,
            "events": events or [],
            "succeeded": succeeded,
            "expected_outcome": expected,
            "actual_outcome": actual,
            "consistent": consistent if verification_complete else None,
            "verification_complete": verification_complete,
        }
        try:
            with open(self.qa_log_file, "a", encoding="utf-8") as f:
                json.dump({"timestamp": datetime.utcnow().isoformat() + "Z",
                           "type": "gameplay_outcome", "data": redact_secrets(outcome)}, f)
                f.write("\n")
        except OSError as exc:
            logger.error("Failed to log gameplay outcome: %s", exc)

        if succeeded and verification_complete and consistent is False:
            self.report_defect(
                defect_id=f"live_{command_name}_{command.get('id', 'unknown')}",
                identity=identity, seed=seed, starting_state=before,
                commands=[command], responses=[response], events=events or [],
                expected=expected, actual=actual,
                reproduction=[command], severity="major", confidence="high",
                category="authoritative gameplay state mismatch",
            )
        return outcome

    def triage_protocol_error(self, command_name, response, game_state, code, message):
        """
        Identifies if an error is a new, unique bug and saves a report.
        """
        # We'll create a simple, unique ID based on the command and error code
        bug_id = f"err_{command_name}_{code}"
        bug_dir = os.path.join(self.report_path, bug_id)
        
        # If we haven't seen this exact bug type before, create a full report
        if not os.path.exists(bug_dir):
            try:
                os.makedirs(bug_dir)
                logger.warning(f"New unique bug identified: {bug_id}. Saving report.")
                
                # Save the key files for debugging
                self._save_bug_report(bug_dir, "response.json", response)
                self._save_bug_report(bug_dir, "game_state.json", game_state)
                
                # Save a summary
                summary = {
                    "bug_id": bug_id,
                    "first_seen": self._get_timestamp(),
                    "command": command_name,
                    "error_code": code,
                    "error_message": message
                }
                self._save_bug_report(bug_dir, "summary.json", summary)
                
            except OSError as e:
                logger.error(f"Failed to save bug report: {e}")

    def _save_bug_report(self, bug_dir, filename, data):
        """Helper to save a JSON file within a bug report directory."""
        try:
            filepath = os.path.join(bug_dir, filename)
            with open(filepath, 'w') as f:
                json.dump(redact_secrets(data), f, indent=4)
        except Exception as e:
            logger.error(f"Failed to write bug file {filename}: {e}")

    def triage_schema_validation_error(self, command_name, payload, schema, error, game_state):
        """
        Saves a detailed report for a payload that failed schema validation.
        """
        bug_id = f"err_schema_{command_name}"
        bug_dir = os.path.join(self.report_path, bug_id)
        
        if not os.path.exists(bug_dir):
            try:
                os.makedirs(bug_dir)
                logger.warning(f"Schema validation failed for '{command_name}'. Saving new report: {bug_id}")
                
                self._save_bug_report(bug_dir, "payload.json", payload)
                self._save_bug_report(bug_dir, "schema.json", schema)
                self._save_bug_report(bug_dir, "game_state.json", game_state)
                
                summary = {
                    "bug_id": bug_id,
                    "first_seen": self._get_timestamp(),
                    "command": command_name,
                    "error_message": error.message,
                    "error_path": list(error.path),
                    "error_validator": error.validator,
                }
                self._save_bug_report(bug_dir, "summary.json", summary)
                
            except OSError as e:
                logger.error(f"Failed to save schema validation bug report: {e}")

    def report_state_inconsistency(self, game_state, message):
        """
        Saves a detailed report when a state inconsistency is detected.
        """
        bug_id = "err_state_inconsistent"
        bug_dir = os.path.join(self.report_path, bug_id)

        try:
            if os.path.exists(bug_dir):
                logger.debug(f"State inconsistency already reported: {message}")
                return
            
            os.makedirs(bug_dir)
            logger.critical(f"State inconsistency detected: {message}. Saving report to {bug_dir}")

            self._save_bug_report(bug_dir, "game_state.json", game_state)

            summary = {
                "bug_id": bug_id,
                "first_seen": self._get_timestamp(),
                "message": message,
            }
            self._save_bug_report(bug_dir, "summary.json", summary)

        except OSError as e:
            logger.error(f"Failed to save state inconsistency report: {e}")

    def report_defect(
        self, *, defect_id, identity, seed, starting_state, commands,
        responses, events, expected, actual, reproduction, severity,
        confidence, category="gameplay",
    ):
        """Write a reproducible gameplay defect as JSON and Markdown.

        Callers should use this only after checking that the outcome is not an
        expected refusal (for example, insufficient credits or turns).
        """
        safe_id = "".join(ch if ch.isalnum() or ch in "-_" else "_" for ch in str(defect_id))
        if not safe_id:
            safe_id = "gameplay_defect"
        bug_dir = os.path.join(self.report_path, safe_id)
        os.makedirs(bug_dir, exist_ok=True)
        report = {
            "defect_id": safe_id,
            "category": category,
            "first_seen": self._get_timestamp(),
            "ai_identity": identity,
            "test_seed": seed,
            "starting_state": starting_state,
            "commands": commands,
            "responses": responses,
            "events": events,
            "expected_outcome": expected,
            "actual_outcome": actual,
            "reproduction_sequence": reproduction,
            "severity": severity,
            "confidence": confidence,
        }
        report = redact_secrets(report)
        self._save_bug_report(bug_dir, "defect.json", report)
        lines = [
            f"# {safe_id}", "", f"- Category: {category}",
            f"- AI identity: {identity}", f"- Test seed: {seed}",
            f"- Severity: {severity}", f"- Confidence: {confidence}",
            "", "## Expected", str(expected), "", "## Actual", str(actual),
            "", "## Reproduction", "```json",
            json.dumps(report["reproduction_sequence"], indent=2), "```", "",
        ]
        try:
            with open(os.path.join(bug_dir, "summary.md"), "w", encoding="utf-8") as report_file:
                report_file.write("\n".join(lines))
        except OSError as exc:
            logger.error("Failed to write human-readable defect summary: %s", exc)
        return bug_dir
