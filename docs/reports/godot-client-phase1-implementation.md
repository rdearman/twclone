# Godot Client Phase 1 Implementation Report

## Scope

Phase 1 implements the audit roadmap's Transport and Protocol Boundary slice. The existing `Main.tscn` shell is preserved; no Phase 2 state-model or gameplay work was started.

## Baseline

- Starting HEAD: `7c69ff13 feat(python-client): add responsive terminal rendering`
- Working tree: extensively dirty and contained an existing untracked `client/godot_client/` tree plus unrelated changes.
- Development branch: `codex/godot-slice1-transport`
- Godot: `/home/rick/bin/Godot_v4.5.1-stable_linux.x86_64`, version `4.5.1.stable.official`
- Python reference suite before and after: `112 passed`
- Existing Godot test infrastructure: none; Phase 1 adds a lightweight executable GDScript harness.

The existing Godot directory, audit document, backups, protocol drafts and unrelated working-tree files were preserved. Generated `.godot/` import/editor data was not staged.

## Files added or modified for Phase 1

- `client/godot_client/godot/Main.gd`
  - consumes the extracted transport;
  - handles correlated success/refusal responses;
  - uses explicit connection states;
  - uses the `passwd` authentication field;
  - prevents authenticated requests before login;
  - keeps player-facing messages free of raw protocol JSON.
- `client/godot_client/godot/Protocol.gd`
  - validates response envelopes;
  - classifies response status;
  - builds requests;
  - creates structured result dictionaries;
  - redacts password/session/token fields for diagnostics;
  - classifies event categories.
- `client/godot_client/godot/RequestManager.gd`
  - unique request IDs;
  - pending request registry;
  - deterministic timeout and cleanup operations.
- `client/godot_client/godot/ProtocolTransport.gd`
  - TCP connection lifecycle;
  - newline-delimited JSON framing;
  - partial and multi-frame reads;
  - `reply_to` correlation;
  - timeout and pending-request failure signals;
  - bounded asynchronous event queue;
  - safe disconnect propagation;
  - redacted diagnostic signals.
- `client/godot_client/godot/AuthSession.gd`
  - explicit unauthenticated/authenticating/authenticated states;
  - validated login payload construction;
  - duplicate login prevention;
  - session-token storage and invalidation;
  - refusal, timeout and malformed-login handling.
- `client/godot_client/godot/tests/protocol_transport_test.gd`
  - executable headless tests for Phase 1 protocol behaviours.
- Core existing Godot project files required to make the untracked client runnable are included in the implementation commit only where needed; generated caches, backups and draft protocol documents are excluded.

## Behaviour delivered

- Every outbound request gets a unique `godot-NNNN` ID and is registered before sending.
- Replies are matched by `reply_to`, not by response type.
- Out-of-order replies are delivered to their matching request.
- Unknown, duplicate and late replies are ignored with a safe diagnostic warning.
- Partial reads and multiple newline-delimited frames are supported.
- Malformed and structurally invalid envelopes produce concise protocol errors without displaying the raw frame.
- Server `ok`, `refused` and `error` statuses are represented consistently.
- Timeouts and disconnects fail pending requests with distinguishable result kinds.
- Events are queued separately from replies, bounded to 200 entries, and rendered only as compact type labels by the current shell.
- Login sends `passwd`, not `password`.
- Login cannot be submitted twice while a login is in flight.
- Session state is cleared after disconnect.
- Authenticated requests are blocked while unauthenticated.
- Persistent diagnostics redact `password`, `passwd`, `token`, `session`, `secret` and `authorization` values.
- The existing shell's static banner now reports connection state instead of continuing to imply live news.

## Tests executed

Reproducible Godot test command:

```bash
XDG_DATA_HOME=/tmp/twclone-godot-xdg-data \
XDG_CACHE_HOME=/tmp/twclone-godot-xdg-cache \
/home/rick/bin/Godot_v4.5.1-stable_linux.x86_64 \
  --headless --log-file /tmp/twclone-godot-tests-xdg.log \
  --path client/godot_client/godot \
  --script tests/protocol_transport_test.gd
```

Result:

```text
Godot protocol tests: 8 passed
```

The eight groups cover:

1. envelope validation, statuses and diagnostic redaction;
2. unique IDs, registration, resolution and timeout expiry;
3. fragmented and multiple-frame input with out-of-order replies;
4. malformed JSON, known/unknown events and uncorrelated frames;
5. late replies, disconnect propagation and pending-request failure;
6. `passwd`, credential validation, duplicate-login prevention, successful/refused login and session invalidation;
7. structured success/refusal/timeout/disconnection results and request construction;
8. event category classification.

Main-scene smoke command:

```bash
XDG_DATA_HOME=/tmp/twclone-godot-xdg-data \
XDG_CACHE_HOME=/tmp/twclone-godot-xdg-cache \
/home/rick/bin/Godot_v4.5.1-stable_linux.x86_64 \
  --headless --log-file /tmp/twclone-godot-main-xdg.log \
  --path client/godot_client/godot \
  --scene res://Main.tscn --quit-after 1
```

Result: process exited successfully and loaded the existing main scene.

Python regression command:

```bash
pytest client/python_client/tests/ -q
```

Result: `112 passed in 3.11s`.

## Server/protocol evidence used

- `src/server_envelope.c`: reply envelopes carry `reply_to`; refusals use `status: refused`, `type: error` and an error object.
- `src/schemas.c`: `auth.login` requires `username` and `passwd`.
- `src/server_auth.c`: login handler reads `username` and `passwd`.
- `client/python_client/protocol.py`: correlated RPCs, event separation, bounded event queue and safe disconnect behaviour.

No live server or database was started. No live login was attempted and no existing game database was modified.

## Remaining limitations

- The transport has not been tested against a live server in this environment.
- The current shell still has legacy schema-harvesting/menu code; Phase 1 prevents schema harvesting from blocking a password already entered during connection, but broader UI/state extraction belongs to later slices.
- The event queue is implemented, but full readable communications/event presentation belongs to the communications slice.
- No external Godot test framework was introduced.
- Godot editor validation attempted to save editor settings under the sandboxed home directory and reported a permissions error; headless script and scene execution succeeded with temporary XDG data/cache paths.
- The existing Godot project contains legacy backup files and draft protocol documents that remain intentionally uncommitted.

## Acceptance status

The Slice 1 transport, correlation, structured-result, authentication-boundary, event-separation and diagnostic acceptance criteria are demonstrated by the passing headless tests and main-scene smoke run. Live protocol validation remains deliberately deferred because no safe live-server fixture was available.
