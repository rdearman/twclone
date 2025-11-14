import logging
import datetime
import json
import os

logger = logging.getLogger(__name__)

class BugReporter:
    def __init__(self, bug_report_dir="bugs", state_manager=None, config=None):
        self.bug_report_dir = bug_report_dir
        self.state_manager = state_manager # Store state_manager
        self.config = config # Store config
        os.makedirs(self.bug_report_dir, exist_ok=True)

    def report_bug(self, bug_type: str, description: str, reproducer: dict, frames: list, server_caps: dict, agent_state: dict, last_commands_history: list, last_responses_history: list, sent_schema: dict = None, validated: bool = False, replay_commands: list = None):
        timestamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
        bug_filename = os.path.join(self.bug_report_dir, f"BUG-{timestamp}-{bug_type}.md")

        # Extract config values for the report
        random_seed = self.config.get("random_seed", "N/A") if self.config else "N/A"
        bandit_epsilon = self.config.get("bandit_epsilon", "N/A") if self.config else "N/A"

        # Create a copy of agent_state and convert sets to lists for JSON serialization
        agent_state_for_json = agent_state.copy()
        for key, value in agent_state_for_json.items():
            if isinstance(value, set):
                agent_state_for_json[key] = list(value)

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
{json.dumps(agent_state_for_json, indent=2)}
```

## Client Version
{agent_state.get('client_version', 'N/A')}

## Configuration & State at Bug
- Random Seed: {random_seed}
- Bandit Epsilon: {bandit_epsilon}
- Current Stage: {agent_state.get('stage', 'N/A')}

## Replay Script Snippet (JSON Commands)
```json
{json.dumps(replay_commands if replay_commands is not None else [], indent=2)}
```
"""
        try:
            with open(bug_filename, "w") as f:
                f.write(bug_report_content)
            logger.error(f"Bug reported: {bug_filename}")
            if self.state_manager:
                self.state_manager.set("bug_reported_this_tick", True)
        except IOError as e:
            logger.critical(f"Failed to write bug report to {bug_filename}: {e}")

    def triage_protocol_error(self, sent_command: dict, response: dict, agent_state: dict, error_message: str, last_commands_history: list, last_responses_history: list, sent_schema: dict = None, validated: bool = False):
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

        # Extract commands for replay snippet
        replay_commands = []
        for frame in frames:
            if frame["type"] == "command":
                cmd = frame["data"].copy() if isinstance(frame["data"], dict) else {}
                cmd.pop("id", None) # Remove ID for replayability
                cmd.pop("auth", None) # Remove auth for replayability
                cmd.pop("ts", None) # Remove timestamp for replayability
                replay_commands.append(cmd)

        self.report_bug(bug_type, description, reproducer, frames, agent_state.get("server_capabilities", {}), agent_state, last_commands_history, last_responses_history, sent_schema, validated, replay_commands=replay_commands)

    def triage_invariant_failure(self, invariant_name: str, description: str, agent_state: dict):
        bug_type = f"Invariant Failure: {invariant_name}"
        reproducer = {
            "command": "N/A",
            "expected": f"Invariant '{invariant_name}' to hold true",
            "actual": description
        }
        frames = []
        last_cmds = agent_state.get("last_commands_history", [])
        last_resps = agent_state.get("last_responses_history", [])
        
        # Extract commands for replay snippet (from last_cmds)
        replay_commands = []
        # Assuming last_commands_history stores command names, not full command dicts
        # This needs to be improved if full command dicts are available
        for cmd_name in last_cmds:
            replay_commands.append({"command": cmd_name})

        self.report_bug(
            bug_type, description, reproducer, frames,
            agent_state.get("server_capabilities", {}), agent_state, last_cmds, last_resps,
            replay_commands=replay_commands
        )