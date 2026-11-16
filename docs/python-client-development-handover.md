# Python client developer guide

The menu-driven terminal client is a normal V3 player client and a useful
reference for protocol workflows. The published command contracts are in
[`PROTOCOL.v3`](PROTOCOL.v3/00_index.md); do not treat old change logs or this
guide as a second protocol specification.

## Source map

- `client.py`: entry point, request/workflow dispatch and terminal menus
- `menus.json`: player-facing command and context menu definitions
- `protocol.py`: framing, correlation, event handling and diagnostic
  sanitization
- `workflows/`: separated gameplay workflows where present
- `tests/`: client unit and workflow regressions

Use `rg` against the current tree before relying on a path or symbol; the
client has been reorganized over time.

## Run and test

Follow [`../client/python_client/README.md`](../client/python_client/README.md)
for installation, connection and test commands. For server-backed work, use
the isolated setup in [`testing/V3_INTEGRATION_QA.md`](testing/V3_INTEGRATION_QA.md).

## Client rules

- Treat server responses as authoritative. Do not fabricate success or infer
  entity identity from unrelated response fields.
- Refresh state from the server after state-changing operations and preserve
  server refusals and error codes in the UI.
- Keep gameplay operations reachable through ordinary menus; debug/RPC tools
  are not substitutes for player workflows.
- Sanitize diagnostic exchanges before buffering or display. Never capture
  credentials, private messages or unapproved personal data.
- Preserve request correlation and idempotency keys across uncertain retries
  when the operation contract requires it.
- Client tests should assert protocol behavior and fail-closed handling, not
  brittle terminal wording unless that wording is part of the user contract.

Current suite counts and live-client evidence are summarized in
[`V3_GAMEPLAY_COMPLETION_TRACKER.md`](V3_GAMEPLAY_COMPLETION_TRACKER.md) and
the linked acceptance reports. Re-run the relevant suite after changing code;
historical counts in old reports are not a current baseline.
