import json
import re
import sys # Import sys for stderr

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
            request_data_schema = request_example_json.get("data", {})
            
            formatted_request_data_schema = {}
            if request_data_schema:
                for key, value in request_data_schema.items():
                    if isinstance(value, str):
                        formatted_request_data_schema[key] = "<string>"
                    elif isinstance(value, int):
                        formatted_request_data_schema[key] = "<integer>"
                    elif isinstance(value, float):
                        formatted_request_data_schema[key] = "<float>"
                    elif isinstance(value, bool):
                        formatted_request_data_schema[key] = "<boolean>"
                    elif isinstance(value, list):
                        formatted_request_data_schema[key] = "<array>"
                    elif isinstance(value, dict):
                        formatted_request_data_schema[key] = "<object>"
                    else:
                        formatted_request_data_schema[key] = "<any>"
            
            formatted_response_data_schema = {}
            if response_json_str:
                response_example_json = json.loads(response_json_str)
                print(f"[DEBUG PARSER] Parsed response example JSON for {command_name_raw}:\n{json.dumps(response_example_json, indent=2)}", file=sys.stderr)
                response_data_schema = response_example_json.get("data", {})
                response_type = response_example_json.get("type")

                if response_data_schema:
                    for key, value in response_data_schema.items():
                        if isinstance(value, str):
                            formatted_response_data_schema[key] = "<string>"
                        elif isinstance(value, int):
                            formatted_response_data_schema[key] = "<integer>"
                        elif isinstance(value, float):
                            formatted_response_data_schema[key] = "<float>"
                        elif isinstance(value, bool):
                            formatted_response_data_schema[key] = "<boolean>"
                        elif isinstance(value, list):
                            formatted_response_data_schema[key] = "<array>"
                        elif isinstance(value, dict):
                            formatted_response_data_schema[key] = "<object>"
                        else:
                            formatted_response_data_schema[key] = "<any>"

            for command_name in command_names:
                commands_detailed[command_name] = {
                    "name": command_name,
                    "description": description,
                    "request_schema": json.dumps(formatted_request_data_schema, indent=2),
                    "response_schema": json.dumps(formatted_response_data_schema, indent=2),
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

def format_commands_for_llm(commands):
    formatted_string = "### Available Game Commands:\n\n"
    for i, cmd in enumerate(commands):
        formatted_string += f"**{i+1}. `{cmd['name']}`**\n"
        formatted_string += f"   - Description: {cmd['description']}\n"
        formatted_string += f"   - Data Schema: ```json\n{cmd['data_schema']}\n```\n\n"
    return formatted_string

if __name__ == "__main__":
    protocol_filepath = '/home/rick/twclone/docs/PROTOCOL.v2.md'
    extracted_commands = parse_protocol_markdown(protocol_filepath)
    llm_protocol_string = format_commands_for_llm(extracted_commands)
    print(llm_protocol_string)