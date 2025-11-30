import json
import sys

try:
    with open('tests/tavern_tests.json', 'r') as f:
        json.load(f)
    print("JSON is valid")
except json.JSONDecodeError as e:
    print(f"JSON error: {e}")
