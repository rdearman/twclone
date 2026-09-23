# Godot Client Phase 1 Implementation Report

## Scope

Phase 1 implements the audit roadmap's Transport and Protocol Boundary and State Models/Authoritative Refresh slices. The existing `Main.tscn` shell is preserved; no Phase 2 gameplay-shell redesign or gameplay-domain work was started.

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

## Slice 2 — Normalized state and authoritative refresh

### Architecture and ownership

- `ClientState.gd` owns the client-owned normalized player, ship and sector models. It never stores or exposes protocol envelopes.
- `AuthoritativeRefresh.gd` owns refresh sequencing, request ownership, refresh generations and late-result rejection. It is constructed by `Main.gd` and uses the Slice 1 transport and `AuthSession` without knowing about UI nodes.
- `Main.gd` subscribes to `ClientState.changed`, `refresh_finished` and `refresh_failed`, and renders only the snapshot/HUD view model. It no longer reads player, ship or sector response dictionaries to populate the shell.
- A refresh generation is incremented for every authenticated refresh. Pending requests from an older generation are retired; late, duplicate or unknown replies cannot update the current snapshot.

### Authoritative protocol contract

The implemented handlers and server response construction establish this sequential refresh:

1. `player.my_info`, payload `{}`, expected successful type `player.info`, normalized from `data.player`.
2. `ship.status`, payload `{}`, expected successful type `ship.status`, normalized from `data.ship`.
3. `sector.info`, payload `{}`, expected successful type `sector.info`, normalized from the sector scan object, including its required `sector_id`.

The requests are sequential so the coordinator has one deterministic partial-failure boundary per domain. Each request carries the authenticated session token through the existing transport. Only a correlated `ok` reply with the expected command and response type is accepted.

### Models and freshness

Missing values remain absent/`null` in the HUD view model; valid zeroes remain zero. Credits are retained as integer-valued money without float conversion. Optional ship cargo and sector entity fields are tolerated, while structurally invalid required objects are rejected. Availability is explicit: `unavailable`, `refreshing`, `fresh`, `partially available`, `stale` and `disconnected`.

Authentication marks prior session-bound values stale, then starts a new refresh. A refusal, timeout, malformed reply or unexpected response type marks only the affected domain partially available and retains its last confirmed value. Disconnect marks retained values disconnected/stale in the state boundary, fails the active refresh and prevents late replies from mutating state. A successful reauthentication therefore obtains a new snapshot rather than treating cached values as live.

### Slice 2 files

- `client/godot_client/godot/ClientState.gd`
- `client/godot_client/godot/AuthoritativeRefresh.gd`
- `client/godot_client/godot/Main.gd`
- `client/godot_client/godot/tests/state_model_test.gd`
- this implementation report

### Slice 2 tests and validation

The state harness contains 20 focused test groups covering player/ship/sector normalization, missing-versus-zero values, integer credits, optional and malformed fields, authentication gating, correlated success, refusal/timeout/disconnect retention, partial refresh, late generations, unexpected types, reauthentication, the Main state boundary, normal-output JSON exclusion and diagnostic redaction.

Executed results:

- `Godot state tests: 20 passed`.
- Slice 1 regression: `Godot protocol tests: 8 passed`.
- Main scene headless smoke: process exited successfully.
- Python regression: `112 passed`.

The exact commands use Godot 4.5.1 with `XDG_DATA_HOME=/tmp/twclone-godot-xdg-data` and `XDG_CACHE_HOME=/tmp/twclone-godot-xdg-cache`; the report's Slice 1 command remains the reproducible protocol baseline.

### Phase 1 status

Phase 1 is complete only when the Slice 1 and Slice 2 acceptance criteria are demonstrated by the passing Godot transport/state tests, the Main.tscn headless smoke and the 112-test Python regression suite. No live server was started, so live login/refresh validation remains deferred. Phase 2 primary gameplay shell, HUD redesign, movement, trading and communications remain intentionally unstarted.
