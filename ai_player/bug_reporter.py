import logging
import datetime
import json
import os
import hashlib

logger = logging.getLogger(__name__)

class BugReporter:
    def __init__(self, bug_report_dir="bugs", state_manager=None, config=None, get_oracle_llm_response_func=None):
        self.bug_report_dir = bug_report_dir
        self.state_manager = state_manager # Store state_manager
        self.config = config # Store config
        self.get_oracle_llm_response_func = get_oracle_llm_response_func
        os.makedirs(self.bug_report_dir, exist_ok=True)

    def report_bug(self, bug_type: str, description: str, reproducer: dict, frames: list, server_caps: dict, agent_state: dict, last_commands_history: list, last_responses_history: list, sent_schema: dict = None, validated: bool = False, replay_commands: list = None, bug_category: str = "Unknown", suspected_component: str = "Unknown"):
        # Generate a deterministic hash for deduplication
        bug_hash_content = {
            "bug_type": bug_type,
            "description": description,
            "reproducer_command": reproducer.get('command'),
            "reproducer_actual": reproducer.get('actual'),
            "bug_category": bug_category,
            "suspected_component": suspected_component
        }
        bug_hash = hashlib.md5(json.dumps(bug_hash_content, sort_keys=True).encode('utf-8')).hexdigest()
        
        deduplicated_bugs = self.state_manager.get("deduplicated_bugs", {})
        timestamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")

        if bug_hash in deduplicated_bugs:
            deduplicated_bugs[bug_hash]["count"] += 1
            deduplicated_bugs[bug_hash]["latest_timestamp"] = timestamp
            self.state_manager.set("deduplicated_bugs", deduplicated_bugs)
            logger.warning(f"Deduplicated bug '{bug_type}' (hash: {bug_hash}). Count: {deduplicated_bugs[bug_hash]['count']}")
            return # Do not create a new file for a duplicate bug

        # If it's a new bug, add it to the deduplicated_bugs and proceed to create a new file
        deduplicated_bugs[bug_hash] = {
            "bug_type": bug_type,
            "description": description,
            "first_timestamp": timestamp,
            "latest_timestamp": timestamp,
            "count": 1,
            "bug_category": bug_category,
            "suspected_component": suspected_component,
            "bug_filename": "" # Will be filled after filename is determined
        }
        self.state_manager.set("deduplicated_bugs", deduplicated_bugs)

        bug_filename = os.path.join(self.bug_report_dir, f"BUG-{timestamp}-{bug_type}-{bug_hash[:8]}.md")
        deduplicated_bugs[bug_hash]["bug_filename"] = bug_filename # Update filename in state
        self.state_manager.set("deduplicated_bugs", deduplicated_bugs) # Save updated state

        # Extract config values for the report
        random_seed = self.config.get("random_seed", "N/A") if self.config else "N/A"
        bandit_epsilon = self.config.get("bandit_epsilon", "N/A") if self.config else "N/A"

        llm_analysis = "N/A"
        llm_suggested_command = "N/A"
        if self.get_oracle_llm_response_func and self.config.get("qa_mode"):
            bug_report_context = f"Bug Type: {bug_type}\nDescription: {description}\nReproducer: {reproducer}"
            oracle_response = self.get_oracle_llm_response_func(agent_state, self.config.get("ollama_model"), bug_report_context)
            if oracle_response:
                llm_analysis = oracle_response.get("analysis", "No analysis provided.")
                llm_suggested_command = json.dumps(oracle_response.get("suggested_command", "No command suggested."), indent=2)

        # Create a copy of agent_state and convert sets to lists for JSON serialization
        agent_state_for_json = agent_state.copy()
        for key, value in agent_state_for_json.items():
            if isinstance(value, set):
                agent_state_for_json[key] = list(value)
        
        frames_ndjson = ""
        for frame in frames:
            frames_ndjson += json.dumps(frame) + "\n"

        replay_script_content = ""
        if replay_commands:
            for i, cmd in enumerate(replay_commands):
                # Replace timestamp and id for replayability
                cmd_copy = cmd.copy()
                cmd_copy["id"] = f"{{str(uuid.uuid4())}}"
                cmd_copy["ts"] = f"{{datetime.datetime.now().isoformat() + 'Z'}}"
                replay_script_content += f"        send_command(sock, {json.dumps(cmd_copy)})\n"
                replay_script_content += f"        time.sleep(0.1) # Small delay\n"

        bug_report_content = f"""# Bug Report: {bug_type}

## Description
{description}

## Bug Category
{bug_category}

## Suspected Component
{suspected_component}

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
{frames_ndjson}
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

## Bandit Q-Table (Snippet)
```json
{json.dumps(agent_state_for_json.get('q_table', {}), indent=2)}
```

## Bandit N-Table (Snippet)
```json
{json.dumps(agent_state_for_json.get('n_table', {}), indent=2)}
```

## LLM Oracle Analysis
```
{llm_analysis}
```

## LLM Oracle Suggested Command
```json
{llm_suggested_command}
```

## Replay Script Snippet (Python)
```python
import socket
import json
import time

HOST = "127.0.0.1"  # Replace with actual game host
PORT = 1234         # Replace with actual game port

def send_command(sock, command):
    sock.sendall(json.dumps(command).encode('utf-8') + b'\\n')
    response = sock.recv(4096).decode('utf-8')
            print(f"Sent: {bug_report_data['last_command_sent']}")    print(f"Recv: {response}")
    return json.loads(response)

def replay():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.connect((HOST, PORT))
        sock.settimeout(1.0)

        # Initial hello
        send_command(sock, {{{{ "id": "1", "ts": "...", "command": "system.hello", "data": {{{{ "client_version": "AI_QA_Bot/1.1" }}}} }}}} )
        time.sleep(0.1)

        # Login
        send_command(sock, {{{{ "id": "2", "ts": "...", "command": "auth.login", "data": {{{{ "username": "{agent_state.get('player_username', 'ai_qa_bot')}", "password": "{self.config.get('player_password', 'quality')}" }}}} }}}} )
        time.sleep(0.1)

        # Replay commands
{replay_script_content}

if __name__ == "__main__":
    replay()
```

## Replay Script Snippet (JSON Commands)
```json
{json.dumps(replay_commands if replay_commands is not None else [], indent=2)}
```

## Replayable JSON Test Fixture
```json
{json.dumps([{"name": f"Replay: {cmd.get('command', 'unknown')}", "cmd": cmd.get("command"), "data": cmd.get("data", {})} for cmd in (replay_commands if replay_commands is not None else [])], indent=2)}
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

        self.report_bug(
            bug_type, description, reproducer, frames,
            agent_state.get("server_capabilities", {}), agent_state,
            last_commands_history, last_responses_history, sent_schema, validated,
            replay_commands=replay_commands,
            bug_category="Protocol Error",
            suspected_component=sent_command.get("command", "Unknown Command").split('.')[0] # e.g., "auth", "ship"
        )

    def triage_invariant_failure(self, invariant_name: str, current_value: any, expected_condition: str, agent_state: dict, error_message: str):
        bug_type = f"Invariant Failure: {invariant_name}"
        description = error_message # Use the provided error_message as the main description
        reproducer = {
            "command": "N/A",
            "expected": expected_condition,
            "actual": f"Value: {current_value}",
            "error_message": error_message
        }
        frames = []
        last_cmds = agent_state.get("last_commands_history", [])
        last_resps = agent_state.get("last_responses_history", [])
        
        # Extract commands for replay snippet (from last_cmds)
        replay_commands = []
        for cmd_dict in last_cmds: # last_cmds now contains full command dicts
            # Create a copy and remove transient fields for replayability
            cmd_copy = cmd_dict.copy()
            cmd_copy.pop("id", None)
            cmd_copy.pop("ts", None)
            cmd_copy.pop("auth", None)
            cmd_copy.pop("meta", None)
            replay_commands.append(cmd_copy)

        self.report_bug(
            bug_type, description, reproducer, frames,
            agent_state.get("server_capabilities", {}), agent_state, last_cmds, last_resps,
            replay_commands=replay_commands,
            current_state=state_manager.get_state(),
            command_history=state_manager.get("last_commands_history", []),
            bug_category="Invariant Violation",
            suspected_component="Agent State Manager"
        )