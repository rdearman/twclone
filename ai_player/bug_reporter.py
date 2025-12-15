import os
import json
import logging
from datetime import datetime

logger = logging.getLogger(__name__)

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

    def log_command(self, command_dict):
        """
        Logs a sent command to the central QA log.
        --- NEW FUNCTION ---
        """
        try:
            log_entry = {
                "timestamp": datetime.utcnow().isoformat() + "Z",
                "type": "command",
                "data": command_dict
            }
            with open(self.qa_log_file, 'a') as f:
                json.dump(log_entry, f)
                f.write('\n')
        except Exception as e:
            logger.error(f"Failed to log command: {e}")

    def log_response(self, response_dict):
        """
        Logs a received response to the central QA log.
        --- NEW FUNCTION ---
        """
        try:
            log_entry = {
                "timestamp": datetime.utcnow().isoformat() + "Z",
                "type": "response",
                "data": response_dict
            }
            with open(self.qa_log_file, 'a') as f:
                json.dump(log_entry, f)
                f.write('\n')
        except Exception as e:
            logger.error(f"Failed to log response: {e}")

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
                json.dump(data, f, indent=4)
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
        bug_id = f"err_state_inconsistent_{self._get_timestamp()}"
        bug_dir = os.path.join(self.report_path, bug_id)

        try:
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

