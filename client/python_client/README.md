# TWClone Python Client

A socket-based, menu-driven, keyboard-led terminal client for the TWClone
server. Used as the reference implementation for validating the real
client/server protocol.

For architecture, completed work, server-contract details, known
limitations, and the continuation backlog, see the authoritative
[development handover](../../docs/python-client-development-handover.md).

## Setup

Requires Python 3.9+ and a running TWClone server (see the repository root
`README.md` for building/running `./bin/server`).

```bash
cd client/python_client
python3 -m venv .venv        # optional
source .venv/bin/activate    # optional
pip install -r requirements.txt  # if present; otherwise no extra deps are required
```

## Run

```bash
python3 client.py --host localhost --port 1234 --user <username> --passwd <password>
```

Useful flags:

* `--menus <path>` — load an alternate `menus.json`.
* `--debug` — expose diagnostic/protocol tools (raw JSON, bulk execute,
  testing menus) that are hidden during normal play.

Run `python3 client.py --help` for the full list of options.

## Test

```bash
pytest client/python_client/tests/ -q
```
