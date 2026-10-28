# TWClone Python Client

A socket-based, menu-driven, keyboard-led terminal client for the TWClone
server. Used as the reference implementation for validating the real
client/server protocol.

For architecture, completed work, server-contract details, known
limitations, and the continuation backlog, see the authoritative
[development handover](../../docs/python-client-development-handover.md).
For the current audit of player-facing news, notices, ship services, and
server-dependent gameplay backlog, see the [v2 client gameplay audit](../../docs/V2_CLIENT_GAMEPLAY_BACKLOG.md).

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
python3 client.py --host localhost --port 1234
```

The client prompts for the username and password without echoing the password.
Avoid supplying a password with `--passwd`, since command-line arguments may
be visible to local process inspection tools.

To create a new account through the server's public registration command, run
`python3 client.py --register --host localhost --port 1234`. The client asks
for the captain name, password twice using hidden prompts, and an optional ship
name. A successful registration establishes the new session and enters the
game; it does not print or save the password or session token.

To change your password after logging in, open **User Settings → Change
password**. Enter the current password, the new password, and its confirmation
at the hidden prompts. A successful change applies to the next login; the
current authenticated session remains active. If the current password is
rejected, the client reports that without printing the submitted values.

Useful flags:

* `--menus <path>` — load an alternate `menus.json`.
* `--debug` — expose diagnostic/protocol tools (bulk execute and testing
  menus) that are hidden during normal play. Sensitive fields are redacted in
  protocol diagnostics.

Run `python3 client.py --help` for the full list of options.

## Test

```bash
pytest client/python_client/tests/ -q
```
