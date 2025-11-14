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
        r'```',
        re.MULTILINE | re.DOTALL
    )

    found_detailed_matches = False
    for match in detailed_pattern.finditer(content):
        found_detailed_matches = True

        command_name_raw = match.group(1).strip()
        command_names = [c.strip() for c in command_name_raw.split('/')]
        description = match.group(2).strip()
        json_str = match.group(3)
        print(f"[DEBUG PARSER] Captured JSON string for {command_name_raw}:\n{json_str}", file=sys.stderr)

        try:
            example_json = json.loads(json_str)
            print(f"[DEBUG PARSER] Parsed example JSON for {command_name_raw}:\n{json.dumps(example_json, indent=2)}", file=sys.stderr)
            data_schema = example_json.get("data", {})
            
            if not data_schema:
                formatted_data_schema = "{}"
            else:
                schema_for_llm = {}
                for key, value in data_schema.items():
                    if isinstance(value, str):
                        schema_for_llm[key] = "<string>"
                    elif isinstance(value, int):
                        schema_for_llm[key] = "<integer>"
                    elif isinstance(value, float):
                        schema_for_llm[key] = "<float>"
                    elif isinstance(value, bool):
                        schema_for_llm[key] = "<boolean>"
                    elif isinstance(value, list):
                        schema_for_llm[key] = "<array>"
                    elif isinstance(value, dict):
                        schema_for_llm[key] = "<object>"
                    else:
                        schema_for_llm[key] = "<any>"
                formatted_data_schema = json.dumps(schema_for_llm, indent=2)

            for command_name in command_names:
                commands_detailed[command_name] = {
                    "name": command_name,
                    "description": description,
                    "data_schema": formatted_data_schema
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
                "data_schema": "{}"
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