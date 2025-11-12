import logging
import datetime
import json
import os

logger = logging.getLogger(__name__)

class BugReporter:
    def __init__(self, bug_report_dir="bugs"):
        self.bug_report_dir = bug_report_dir
        os.makedirs(self.bug_report_dir, exist_ok=True)

    def report_bug(self, bug_type: str, description: str, reproducer: dict, frames: list, server_caps: dict, agent_state: dict, last_commands_history: list, last_responses_history: list):
        timestamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
        bug_filename = os.path.join(self.bug_report_dir, f"BUG-{timestamp}-{bug_type}.md")

        bug_report_content = f"""# Bug Report: {bug_type}

## Description
{description}

## Trigger
`{reproducer.get('command')}` in sector {agent_state.get('player_location_sector')}

## Expected
{reproducer.get('expected', 'N/A')}

## Actual
{reproducer.get('actual', 'N/A')}

## Reproducer Steps
1. Connect (system.hello)
2. auth.login as {agent_state.get('player_username', 'N/A')}
3. ... (steps leading to the bug, derived from frames)
{reproducer.get('steps', 'N/A')}

## Frames (Last 3 Request/Response)
```json
{json.dumps(frames, indent=2)}
```

## Last Commands History
```json
{json.dumps(last_commands_history, indent=2)}
```

## Last Responses History
```json
{json.dumps(last_responses_history, indent=2)}
```

## Server Capabilities
```json
{json.dumps(server_caps, indent=2)}
```

## Agent State (Relevant Snippet)
```json
{json.dumps(agent_state, indent=2)}
```
"""
        try:
            with open(bug_filename, "w") as f:
                f.write(bug_report_content)
            logger.error(f"Bug reported: {bug_filename}")
        except IOError as e:
            logger.critical(f"Failed to write bug report to {bug_filename}: {e}")

    def triage_protocol_error(self, sent_command: dict, response: dict, agent_state: dict, error_message: str, last_commands_history: list, last_responses_history: list):
        bug_type = "Protocol Error"
        description = f"Server returned a protocol error: {error_message}"
        reproducer = {
            "command": sent_command.get("command"),
            "expected": f"Valid response for {sent_command.get('command')}",
            "actual": f"Error: {error_message}"
        }
        # Combine the last commands and responses into a chronological frame for better repro
        frames = []
        # Interleave commands and responses, assuming they are roughly chronological
        # This is a simplified approach; a more robust solution might sort by timestamp
        for i in range(max(len(last_commands_history), len(last_responses_history))):
            if i < len(last_commands_history):
                frames.append({"type": "command", "data": last_commands_history[i]})
            if i < len(last_responses_history):
                frames.append({"type": "response", "data": last_responses_history[i]})
        
        # Add the current sent command and its error response at the end
        frames.append({"type": "command", "data": sent_command})
        frames.append({"type": "response", "data": response})

        self.report_bug(bug_type, description, reproducer, frames, agent_state.get("server_capabilities", {}), agent_state, last_commands_history, last_responses_history)

    def triage_invariant_failure(self, invariant_name: str, description: str, agent_state: dict):
        bug_type = f"Invariant Failure: {invariant_name}"
        reproducer = {
            "command": "N/A",
            "expected": f"Invariant '{invariant_name}' to hold true",
            "actual": description
        }
        frames = [] # No specific frames for invariant failure unless tied to a response
        self.report_bug(bug_type, description, reproducer, frames, agent_state.get("server_capabilities", {}), agent_state)