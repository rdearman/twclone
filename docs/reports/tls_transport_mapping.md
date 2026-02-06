# TLS Transport Mapping Discovery Report
## twclone Server (Client↔Server Port 1234)

**Date:** 2026-02-06  
**Scope:** Client↔Server transport only (port 1234)  
**Out-of-Scope:** Engine communication (port 4321, s2s protocol)  
**Status:** Read-only discovery; NO CODE CHANGES MADE

---

## A. Architecture Summary

### Network Configuration
- **Client↔Server Port:** 1234 (TCP, IPv4)
- **Protocol:** Newline-delimited JSON (one JSON object per line)
- **Thread Model:** Per-connection threads (PTHREAD_CREATE_DETACHED)
- **Socket Acceptance:** Polling-based listener loop + per-connection thread spawn
- **Existing Crypto:** OpenSSL already linked (libssl.so.3) for s2s HMAC/EVP operations

### Key Design Fact
The server creates ONE thread per accepted connection. Each thread reads lines from a FILE* wrapped around the socket FD, parses JSON, dispatches commands, and writes responses back to the same FD.

---

## B. Connection Lifecycle Map

### B.1 Listener Initialization

**Location:** `src/server_loop.c:1283` in function `server_loop()`

```c
int listen_fd = make_listen_socket (g_cfg.server_port);
```

**Detailed Function:** `src/server_loop.c:957–990`

```
File:     src/server_loop.c
Function: make_listen_socket()
Lines:    957–990
```

**Key Steps:**
- **957–959:** Create socket with `socket(AF_INET, SOCK_STREAM, 0)`
- **965–970:** Set `SO_REUSEADDR` socket option via `setsockopt()`
- **971–976:** Build `sockaddr_in` structure (0.0.0.0:port)
- **977–982:** Call `bind(fd, &sa, sizeof(sa))`
- **983–988:** Call `listen(fd, 128)` (backlog=128)
- **989:** Return listen_fd

**Socket Option Observed:**
```
SO_REUSEADDR enabled (line 953)
No TCP_NODELAY, SO_KEEPALIVE, or other tuning observed.
```

---

### B.2 Accept Loop

**Location:** `src/server_loop.c:1297–1369` in function `server_loop()`

```c
while (*running) {
  // ...polling code (lines 1299–1324)...
  if (pfd.revents & POLLIN) {
    client_ctx_t *ctx = calloc(1, sizeof(*ctx));
    // ...error checks...
    int cfd = accept(listen_fd, (struct sockaddr*)&ctx->peer, &sl);
    // ...error checks...
    pthread_create(&th, &attr, connection_thread, ctx);
  }
}
```

**Key Accept Points:**

| Aspect | Location | Details |
|--------|----------|---------|
| Poll setup | Line 1291 | `struct pollfd pfd = {.fd = listen_fd, .events = POLLIN}` |
| Poll call | Line 1300 | `poll(&pfd, 1, 100)` (100ms timeout) |
| Accept call | Line 1335 | `accept(listen_fd, (struct sockaddr*)&ctx->peer, &sl)` |
| Thread spawn | Line 1356 | `pthread_create(&th, &attr, connection_thread, ctx)` |

**Error Handling:**
- Accept errors (line 1337–1345): EINTR/EAGAIN skipped; others logged and skipped
- Thread creation failure (line 1364–1368): Client removed, socket closed, context freed

---

### B.3 Connection Structure

**Type:** `client_ctx_t`  
**Location:** `src/common.h:34–54`

```c
typedef struct {
  int fd;                           // Socket FD
  volatile sig_atomic_t *running;   // Global shutdown flag
  struct sockaddr_in peer;          // Client peer address
  uint64_t cid;                     // Connection ID
  int player_id;                    // Authenticated player ID (0 if unauth)
  int ship_id;                      // Player's active ship
  int sector_id;                    // Player's current sector
  int corp_id;                      // Player's corporation
  time_t rl_window_start;           // Rate limit window start
  int rl_count;                     // Rate limit counter
  int rl_limit;                     // Rate limit threshold (60 req/60sec)
  int rl_window_sec;                // Rate limit window size
  json_t *captured_envelopes;       // Bulk execution capture buffer
  int captured_envelopes_valid;     // Validity flag (1 if allocated by server)
  int responses_sent;               // Hardening counter (must be > 0)
} client_ctx_t;
```

**Key Property for TLS:**
- `fd` is the raw socket FD; will be wrapped by `fdopen(fd, "r")` in connection_thread
- **No SSL_CTX or SSL* pointer fields exist** — will need to add if doing per-connection SSL

---

### B.4 Thread Model

**Type:** Per-connection detached threads

**Evidence:**
```c
src/server_loop.c:1295  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
src/server_loop.c:1356  int prc = pthread_create(&th, &attr, connection_thread, ctx);
```

**Implication for TLS:**
- Each thread independently reads/writes to its own connection
- No shared global SSL_CTX state needed (but one SSL_CTX should be created once at server startup)
- Each thread will need its own `SSL*` connection object

---

### B.5 Connection Teardown

**Location:** `src/server_loop.c:1249–1274` in function `connection_thread()`

```c
while (getline(&line, &len, f) != -1) {
  // ... process message ...
}

free(line);
fclose(f);  // Line 1269: Closes FILE* (underlying FD also closed by fclose())
db_close_thread();
loop_remove_client(ctx);
free(ctx);
```

**Teardown Points:**
| Action | Line | Note |
|--------|------|------|
| Loop exit | 1249 | `getline()` returns -1 (EOF/error) |
| Free line buffer | 1268 | Dynamic buffer from getline |
| Close FILE* | 1269 | Also closes underlying socket FD |
| Close DB thread handle | 1270 | Thread-local DB cleanup |
| Remove client from loop | 1271 | Housekeeping (broadcast cleanup) |
| Free context | 1272 | Final cleanup |

**Important:** `fclose(f)` closes the underlying socket FD. Under TLS, we must ensure SSL_shutdown() happens before fclose().

---

## C. Read Path (Byte Flow)

### C.1 Wrapper Layer (FILE* abstraction)

**Location:** `src/server_loop.c:1238–1244`

```c
FILE *f = fdopen(fd, "r");
if (!f) {
  close(fd);
  free(ctx);
  return NULL;
}
```

**Key Detail:**
- `fdopen(fd, "r")` wraps the raw socket in a buffered FILE* stream
- Mode is `"r"` (read-only from client perspective)
- FILE* buffer will be used by `getline()`

### C.2 Lowest-Level Read: getline()

**Location:** `src/server_loop.c:1249`

```c
while (getline(&line, &len, f) != -1) {
  json_error_t jerr;
  json_t *root = json_loads(line, 0, &jerr);
  // ...
}
```

**Implementation Details:**
- `getline(&line, &len, f)` is POSIX standard (GNU C library)
- **Dynamic buffering:** Automatically resizes `line` buffer as needed
- **Framing:** Reads until `\n` (newline)
- **Return value:** Bytes read (including `\n`), or -1 on EOF/error

**Key Assumption to Preserve Under SSL_read():**
```
getline() assumes bytes are delivered in a stream.
Under SSL_read(), we may get partial reads that do NOT include a complete newline.
RISK: If SSL_read() returns partial data, getline() will block waiting for more.
MITIGATION: Buffering strategy (see Section E below)
```

### C.3 Message Framing Mechanism

**Format:** Newline-delimited JSON (NDJSON)

**Framing Logic:**
1. Read bytes from socket until `\n` is encountered (handled by getline)
2. Line is passed to `json_loads(line, 0, &jerr)` — JSON parser
3. Parser validates and builds JSON object tree
4. If valid, pass to `process_message()`; if invalid, send error

**Buffer Behavior:**
- `getline()` uses internal FILE* buffer (default typically 8KB)
- Can expand dynamically if line exceeds buffer size
- BUF_SIZE constant (8192) is defined but not used in read path

---

### C.4 JSON Parse Entrypoint

**Location:** `src/server_loop.c:1249–1265`

```c
while (getline(&line, &len, f) != -1) {
  json_error_t jerr;
  json_t *root = json_loads(line, 0, &jerr);
  
  if (!root || !json_is_object(root)) {
    send_enveloped_error(fd, NULL, ERR_INVALID_SCHEMA, "Malformed JSON");
    if (root) json_decref(root);
  } else {
    process_message(ctx, root);
    json_decref(root);
  }
}
```

**JSON Parser:** Jansson library (`json_loads`)  
**Error Handling:** On parse failure, error sent immediately; loop continues

---

## D. Write Path (Byte Flow)

### D.1 Response Generation & Dispatch

**Location:** `src/server_loop.c:1262–1264`

```c
process_message(ctx, root);  // Dispatches to command handler
json_decref(root);
```

Command handlers call one of:
- `send_response_ok_*()` — Success with data
- `send_response_error()` — Error response
- Other functions (`send_enveloped_*()`) — Direct envelope responses

### D.2 Envelope Building

**Locations:**
- `src/server_envelope.c:57–90` — `send_response_ok_take()`
- `src/server_envelope.c:539–571` — `send_enveloped_error()`
- `src/server_envelope.c:497–533` — `send_enveloped_ok()`

**Typical Flow:**
```
send_response_ok_take(ctx, req, type, data)
  ├─ Build JSON envelope (type, meta, data)
  └─ Call send_all_json(ctx->fd, envelope)
```

### D.3 Lowest-Level Write: send_all()

**Location:** `src/server_envelope.c:341–360`

```c
static int send_all(int fd, const char *buf, size_t len) {
  size_t off = 0;
  while (off < len) {
    ssize_t n = send(fd, buf + off, len - off, MSG_NOSIGNAL);
    if (n < 0) {
      if (errno == EINTR) continue;
      return -1;
    }
    off += (size_t) n;
  }
  return 0;
}
```

**Key Details:**
- Uses `send(fd, ...)` with `MSG_NOSIGNAL` flag (prevent SIGPIPE)
- Implements **partial write handling** (loop until all bytes sent)
- Returns -1 on error, 0 on success
- EINTR (signal) is handled by retry

### D.4 JSON Serialization

**Location:** `src/server_envelope.c:419–450` — `send_all_json()`

```c
void send_all_json(int fd, json_t *obj) {
  char *s;
  if (fd == -1) {
    s = json_dumps(obj, JSON_INDENT(2) | JSON_SORT_KEYS | JSON_ENCODE_ANY);
    printf("%s\n", s);
    fflush(stdout);
  } else {
    s = json_dumps(obj, JSON_COMPACT);  // Compact JSON
  }
  
  if (s) {
    (void) send_all(fd, s, strlen(s));
    (void) send_all(fd, "\n", 1);      // IMPORTANT: Newline sent separately
    free(s);
  }
}
```

**Key Details:**
- Compact JSON format (no whitespace) for network
- JSON is serialized to C string via `json_dumps()`
- **Newline appended separately** (line 447) — ensures framing boundary
- send_all() called twice: once for JSON, once for `\n`

### D.5 Write Path Summary

```
Command handler
  ├─ send_response_ok_*() / send_enveloped_*()
  ├─ Build JSON envelope
  └─ send_all_json(fd, obj)
      ├─ json_dumps(obj, JSON_COMPACT)  → C string
      ├─ send_all(fd, json_str, len)   → network bytes
      └─ send_all(fd, "\n", 1)         → newline for framing
```

**No buffering beyond send()** — responses are sent immediately.

---

## E. TLS Insertion Plan

### E.1 Strategy Choice: **TLS-Only on Port 1234**

**Rationale:**
- Single port simplifies deployment (no dual-port complexity)
- Client library can be updated atomically (all clients use TLS)
- Backward compat not needed for internal game protocol
- Cleaner than SNI-based dual protocol

**Alternative considered:** Dual-port (1234 plaintext + 1235 TLS) — rejected due to operational burden

---

### E.2 Exact TLS Insertion Points

#### **E.2.1 Startup: Create SSL Context (once)**

**Location to insert:** `src/server_loop.c` — early in `server_loop()` function

```
File:     src/server_loop.c
Function: server_loop()
Insert:   Before listener initialization (before line 1283)

Pseudo-code:
  1. SSL_library_init()  // Init OpenSSL
  2. SSL_load_error_strings()
  3. SSL_CTX *ctx = SSL_CTX_new(TLS_server_method())
  4. SSL_CTX_set_certificate_chain_file(ctx, CERT_PATH)
  5. SSL_CTX_set_private_key_file(ctx, KEY_PATH, SSL_FILETYPE_PEM)
  6. Store ctx in global or pass to connection handler
```

**Key design:** One SSL_CTX for all connections (reusable, thread-safe after init)

---

#### **E.2.2 Per-Connection: SSL Handshake**

**Location to insert:** `src/server_loop.c:1335` (just after `accept()`)

```
File:     src/server_loop.c
Function: server_loop()
After:    Line 1346 (after error check for accept)

Pseudo-code:
  1. SSL *ssl = SSL_new(global_ctx)
  2. SSL_set_fd(ssl, cfd)
  3. if (SSL_accept(ssl) <= 0) {
       SSL_free(ssl);
       close(cfd);
       free(ctx);
       continue;
     }
  4. Store ssl in ctx (requires adding SSL* field to client_ctx_t)
  5. Pass both cfd and ssl to connection_thread()
```

**Blocking behavior:** SSL_accept() is blocking. No issue (new connection, not in use yet).

---

#### **E.2.3 Read Path: Replace getline() with SSL_read()**

**Location to modify:** `src/server_loop.c:1238–1249`

**Current code:**
```c
FILE *f = fdopen(fd, "r");
while (getline(&line, &len, f) != -1) { ... }
```

**TLS challenge:** `getline()` expects a regular stream. SSL_read() is event-based (may return partial data).

**Solution: Custom line-reading loop over SSL_read()**

```
File:     src/server_loop.c
Function: connection_thread()
Replace:  Lines 1238–1249

Pseudo-code (high-level):
  char line_buf[4096];  // Temporary accumulator
  char *line = NULL;
  size_t line_len = 0;
  
  while (1) {
    unsigned char buf[1024];
    int nread = SSL_read(ssl, buf, sizeof(buf));
    
    if (nread <= 0) {
      int err = SSL_get_error(ssl, nread);
      if (err == SSL_ERROR_ZERO_RETURN) break;  // Shutdown
      else if (err == SSL_ERROR_WANT_READ) continue;  // Partial
      else { /* hard error */ break; }
    }
    
    // Append buf[0..nread-1] to line accumulator
    // Check for '\n'; if found, dispatch message
    // Reset accumulator, continue
  }
```

**Key insight:** We must buffer SSL_read() output ourselves; we cannot rely on FILE* buffering.

---

#### **E.2.4 Write Path: Replace send() with SSL_write()**

**Location to modify:** `src/server_envelope.c:346`

**Current code:**
```c
ssize_t n = send(fd, buf + off, len - off, MSG_NOSIGNAL);
```

**TLS solution:** Create wrapper function `tls_write_all()`

```
File:     src/server_envelope.c
Function: send_all()
Replace:  Lines 341–360

Pseudo-code:
  int tls_write_all(SSL *ssl, const char *buf, size_t len) {
    size_t off = 0;
    while (off < len) {
      int n = SSL_write(ssl, buf + off, len - off);
      if (n <= 0) {
        int err = SSL_get_error(ssl, n);
        if (err == SSL_ERROR_WANT_WRITE) continue;
        else return -1;
      }
      off += n;
    }
    return 0;
  }
```

**Dispatch:** At runtime, check if SSL* is present in ctx; if so, use tls_write_all(); else use send().

---

### E.3 State Management: Adding SSL* to client_ctx_t

**File:** `src/common.h:34–54`

**Required addition:**
```c
typedef struct {
  int fd;
  // ... existing fields ...
  void *ssl_conn;  // SSL* (void* to avoid explicit OpenSSL include in common.h)
  // ... remaining fields ...
} client_ctx_t;
```

**Rationale:** Avoid polluting common.h with OpenSSL headers; use opaque pointer.

---

### E.4 Handshake Error Handling & Logging

**SSL_accept() failures:**
- Log error via `SSL_get_error()` and `ERR_get_error()` stack
- Close socket, free SSL, free context
- Continue accept loop

**SSL_read() errors:**
- `SSL_ERROR_ZERO_RETURN`: Clean shutdown (exit read loop)
- `SSL_ERROR_WANT_READ`: Retry (no bytes available)
- Other: Hard error, exit loop

**SSL_write() errors:**
- `SSL_ERROR_WANT_WRITE`: Retry
- Other: Hard error, close connection

**Logging approach:** Use existing `LOGE()` macro from server_log.h

---

## F. Risks & Gotchas

### F.1 Partial Reads Under SSL_read()

**Risk:** SSL_read() may return fewer bytes than requested.

**Current assumption:** `getline()` reads until `\n` is found in stream.

**Under TLS:** SSL_read() returns only what's available in the TLS record, which may not include a complete newline.

**Mitigation:**
- Implement buffering layer (accumulate SSL_read() output)
- Check buffer for `\n` after each SSL_read()
- Only dispatch JSON when a complete line (newline-terminated) is available

**Code location:** New line-reading logic in `connection_thread()` (src/server_loop.c)

---

### F.2 Framing Assumptions

**Current assumption:** Each JSON message is one line (terminated by `\n`).

**Under TLS:** Newline is still the framing boundary (unchanged).

**Risk:** None — framing is orthogonal to transport.

**Verification:** Test that multi-line JSON (invalid) is still rejected.

---

### F.3 Maximum Message Size

**Current:** No hard limit enforced; `getline()` grows buffer dynamically.

**Under TLS:** Same — newline-based framing means messages can be arbitrarily large (memory-limited).

**Risk:** Large message DoS potential, but same as plaintext.

**Mitigation:** Add max line length check in line-reading loop (~1MB?), same as before.

---

### F.4 Interaction with Partial Writes

**Current:** `send_all()` loops until all bytes are sent.

**Under TLS:** Same pattern works with SSL_write() (returns bytes sent per call).

**Risk:** None — SSL_write() behaves like send() in this regard.

---

### F.5 Thread Safety of SSL_CTX

**Risk:** Multiple threads calling SSL_new(ctx) on the same SSL_CTX.

**Status:** OpenSSL guarantees SSL_CTX is thread-safe for SSL_new() and SSL_set_fd().

**Verification:** Each thread gets its own SSL* object, so no contention.

---

### F.6 Signal Handling

**Current:** SIGPIPE ignored (line 1280–1281), EINTR handled in loops.

**Under TLS:** No change needed — SSL_write/read return error codes (not interrupted by signals).

**Risk:** None.

---

## G. Verification Approach

### G.1 Self-Signed Certificate Generation

**Dev environment setup (not code):**

```bash
# Generate private key
openssl genrsa -out server.key 2048

# Generate self-signed certificate (valid 365 days)
openssl req -new -x509 -key server.key -out server.crt -days 365

# Place in known location, e.g., /etc/twclone/tls/
# Update CERT_PATH and KEY_PATH in server_loop.c startup code
```

---

### G.2 Handshake Validation

**Using openssl s_client:**

```bash
openssl s_client -connect localhost:1234 \
  -servername twclone.local \
  -showcerts
```

**Expected output:** TLS handshake success, certificate displayed.

---

### G.3 Protocol Validation (No JSON Changes)

**Test client sends plaintext login JSON over TLS:**

```json
{
  "command": "auth.login",
  "meta": {
    "id": "client-1"
  },
  "data": {
    "username": "testuser",
    "password": "testpass"
  }
}
```

**Expected:** Server responds with normal JSON envelope (over TLS).

```json
{
  "type": "auth.login.response",
  "meta": { "id": "srv-1", "ts": "..." },
  "in_reply_to": "client-1",
  "data": {
    "session_token": "...",
    "player_id": 123
  }
}
```

**Verification:** No protocol changes; TLS is transparent to JSON.

---

### G.4 Regression Testing Approach

**Current test harness:** tests.v2/*.json suites use HTTP-like RPC protocol (not direct sockets).

**Issue:** tests.v2/ likely cannot open TLS sockets directly (depends on test framework).

**Solution (minimal, no code):**
1. Create standalone TLS smoke test script (not in tests.v2/):
   ```bash
   # Pseudo-script: tls_smoke_test.sh
   # 1. Send login JSON over TLS
   # 2. Verify response is valid JSON
   # 3. Check session token is present
   # 4. Verify response is signed correctly (meta.ts, etc)
   ```
2. Do NOT modify tests.v2/ (would require updating test framework for TLS).
3. Run smoke test as part of CI/pre-integration verification.
4. Existing tests.v2/ suites can be adapted later (out of scope for TLS mapping).

---

## H. Implementation Checklist (for future work)

This section is **NOT TO BE IMPLEMENTED** in this discovery; listed for reference only.

- [ ] Add SSL* field to client_ctx_t (src/common.h)
- [ ] Create SSL_CTX at server_loop startup (src/server_loop.c)
- [ ] Implement SSL_accept per connection (src/server_loop.c)
- [ ] Rewrite read loop with SSL_read + buffering (src/server_loop.c)
- [ ] Update send_all() to support SSL_write (src/server_envelope.c)
- [ ] Add SSL_shutdown at connection teardown (src/server_loop.c)
- [ ] Add -lssl -lcrypto to build system (configure.ac, Makefile.am)
- [ ] Create TLS smoke test script (tools/ or similar)
- [ ] Generate self-signed dev certificate (docs/tls/)
- [ ] Update PROTOCOL.v3 to note TLS requirement
- [ ] Test with openssl s_client
- [ ] Test with curl (if applicable)
- [ ] Performance test (TLS overhead)

---

## I. Summary: Minimal Refactor Scope

To introduce TLS on port 1234 **without breaking JSON protocol:**

### Code changes needed:
1. **src/common.h** — Add SSL* field to client_ctx_t (~2 lines)
2. **src/server_loop.c** — Add SSL init at startup, SSL_accept per connection, rewrite read loop (~100 lines)
3. **src/server_envelope.c** — Add SSL_write wrapper, branch in send_all() (~30 lines)
4. **Build system** — Link -lssl -lcrypto (~2 lines in configure.ac/Makefile.am)

### No changes to:
- JSON framing (newline-delimited)
- Message structure (envelope unchanged)
- Command dispatch (unchanged)
- Response serialization (unchanged)
- Database layer (unchanged)
- Any other protocol aspects

### Transport is **fully swappable:**
- At socket level (accept → SSL_accept)
- At read level (getline → SSL_read loop)
- At write level (send → SSL_write)

---

## Appendix: Exact Code Locations Reference

| Component | File | Function | Lines |
|-----------|------|----------|-------|
| Port config | src/server_config.c | config_set_defaults | 66 |
| Listener init | src/server_loop.c | make_listen_socket | 957–990 |
| Accept loop | src/server_loop.c | server_loop | 1297–1369 |
| Connection struct | src/common.h | (typedef) | 34–54 |
| Thread spawn | src/server_loop.c | server_loop | 1356 |
| Read entry | src/server_loop.c | connection_thread | 1238–1249 |
| JSON parse | src/server_loop.c | connection_thread | 1252 |
| Dispatch | src/server_loop.c | process_message | 1125 |
| Response dispatch | src/server_envelope.c | send_response_ok_take | 57 |
| Write entry | src/server_envelope.c | send_all_json | 419–450 |
| Raw send | src/server_envelope.c | send_all | 341–360 |
| Teardown | src/server_loop.c | connection_thread | 1268–1272 |

---

**End of Report**
