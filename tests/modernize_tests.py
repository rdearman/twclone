import json
import uuid

with open('tests/tests_all.json', 'r') as f:
    data = json.load(f)

suffix = str(uuid.uuid4())[:8]

if 'suites' in data and isinstance(data['suites'], dict):
    for suite_name, tests in data['suites'].items():
        for test in tests:
            if isinstance(test, dict) and test.get('command', {}).get('data', {}).get('user_name'):
                test['command']['data']['user_name'] += f'_{suffix}'

with open('tests/tests_all.json', 'w') as f:
    json.dump(data, f, indent=2)
