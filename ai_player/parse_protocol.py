import json
import re
import sys # Import sys for stderr
from typing import Any

def _generate_schema_from_example(example_data: dict) -> dict:
    """
    Generates a basic JSON schema from an example dictionary.
    Infers types and marks all top-level fields as required.
    """
    if not example_data:
        return {"type": "object", "properties": {}}

    properties = {}
    required = []
    for key, value in example_data.items():
        properties[key] = {"type": _get_json_schema_type(value)}
        required.append(key)
    
    return {
        "type": "object",
        "properties": properties,
        "required": required
    }

def _get_json_schema_type(value: Any) -> str:
    """Infers JSON schema type from a Python value."""
    if isinstance(value, str):
        return "string"
    elif isinstance(value, int):
        return "integer"
    elif isinstance(value, float):
        return "number"
    elif isinstance(value, bool):
        return "boolean"
    elif isinstance(value, list):
        # For simplicity, assume array of any type for now
        return "array"
    elif isinstance(value, dict):
        # Recursively generate schema for nested objects
        return _generate_schema_from_example(value)
    else:
        return "string" # Default to string for unknown types

def parse_protocol_markdown(filepath):
    commands_detailed = {} # Use a dict to easily handle duplicates and prioritize detailed info
    commands_indexed = set() # Use a set for quick lookup of indexed commands

    with open(filepath, 'r') as f:
        content = f.read()



    # Regex for detailed commands (with description and example JSON)
    detailed_pattern = re.compile(
        r'^\*   `([^`]+)`: ([^\n]+)\n' +  # Command name and description
        r'(?:(?!\*?Example Client Request:\*?).)*?' + # Match any content non-greedily until "Example Client Request:"
        r'\*?Example Client Request:\*?\n' +     # Match the literal string "Example Client Request:" with optional asterisks
        r'(?:(?!```json).)*?' +            # Match any content non-greedily until "```json"
        r'```json\n' +                   # Start of JSON block
        r'(\{[\s\S]*?\})\n' +                   # The JSON content (non-greedy, including newlines)
        r'```\n' +
        r'(?:(?!\*?Example Server Response:\*?).)*?' + # Match any content non-greedily until "Example Server Response:"
        r'(?:\*?Example Server Response:\*?\n' + # Optional: Match the literal string "Example Server Response:"
        r'(?:(?!```json).)*?' +            # Match any content non-greedily until "```json"
        r'```json\n' +                   # Start of JSON block
        r'(\{[\s\S]*?\})\n' +                   # The JSON content (non-greedy, including newlines)
        r'```)?',                         # Make the entire server response block optional
        re.MULTILINE | re.DOTALL
    )

    found_detailed_matches = False
    for match in detailed_pattern.finditer(content):
        found_detailed_matches = True

        command_name_raw = match.group(1).strip()
        command_names = [c.strip() for c in command_name_raw.split('/')]
        description = match.group(2).strip()
        request_json_str = match.group(3)
        response_json_str = match.group(4) # This will be None if no response example

        print(f"[DEBUG PARSER] Captured Request JSON string for {command_name_raw}:\n{request_json_str}", file=sys.stderr)
        if response_json_str:
            print(f"[DEBUG PARSER] Captured Response JSON string for {command_name_raw}:\n{response_json_str}", file=sys.stderr)

        try:
            request_example_json = json.loads(request_json_str)
            print(f"[DEBUG PARSER] Parsed request example JSON for {command_name_raw}:\n{json.dumps(request_example_json, indent=2)}", file=sys.stderr)
            
            # Store the 'data' part of the request example as the request_schema
            request_data_example = request_example_json.get("data", {})
            request_schema_obj = _generate_schema_from_example(request_data_example)
            
            response_schema_obj = {}
            response_type = None
            if response_json_str:
                response_example_json = json.loads(response_json_str)
                print(f"[DEBUG PARSER] Parsed response example JSON for {command_name_raw}:\n{json.dumps(response_example_json, indent=2)}", file=sys.stderr)
                
                # Store the 'data' part of the response example as the response_schema
                response_data_example = response_example_json.get("data", {})
                response_schema_obj = _generate_schema_from_example(response_data_example)
                response_type = response_example_json.get("type")

            for command_name in command_names:
                commands_detailed[command_name] = {
                    "name": command_name,
                    "description": description,
                    "request_schema": json.dumps(request_schema_obj, indent=2), # Store as JSON string
                    "response_schema": json.dumps(response_schema_obj, indent=2), # Store as JSON string
                    "response_type": response_type # Store the response type
                }
        except json.JSONDecodeError as e:
            print(f"[parser] JSON decode failed for command '{command_name_raw}': {e}", file=sys.stderr)
            continue

    


    # Regex for commands in the "Full Command & Event Index"
    # Look for lines like '*   `command.name`' under the "Client-to-Server Commands" heading
    index_pattern = re.compile(
        r'(^### Client-to-Server Commands\n)' + # Heading
        r'(.*?)(?=\n^###|\Z)', # Non-greedy match until next heading or end of string
        re.MULTILINE | re.DOTALL
    )
    index_match = index_pattern.search(content)

    if index_match:

        index_block = index_match.group(2)
        command_names_in_index = re.findall(r'^\*\s+`([^`]+)`', index_block, re.MULTILINE)
        for cmd_name in command_names_in_index:
            commands_indexed.add(cmd_name.strip())




    # Combine lists, prioritizing detailed info
    final_commands = []
    for cmd_name in sorted(list(commands_indexed)): # Sort for consistent order
        if cmd_name in commands_detailed:
            final_commands.append(commands_detailed[cmd_name])
        else:
            # Add commands found only in the index
            final_commands.append({
                "name": cmd_name,
                "description": "No detailed description available. Refer to context for usage.",
                "request_schema": "{}", # Initialize as empty JSON string
                "response_schema": "{}", # Initialize as empty JSON string
                "response_type": None # No response type available
            })
            
    return final_commands

if __name__ == "__main__":
    protocol_filepath = '/home/rick/twclone/docs/PROTOCOL.v2.md'
    extracted_commands = parse_protocol_markdown(protocol_filepath)
    # Optionally, print the extracted commands for verification
    # print(json.dumps(extracted_commands, indent=2))