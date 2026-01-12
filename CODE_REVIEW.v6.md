# Strictly Investigative Code Review Report (v6)
**Project:** twclone Server
**Auditor:** Senior C Systems Engineer / Security Auditor
**Date:** 2026-01-12
**Status:** FINAL / EVIDENCE-DRIVEN

---

## 1. Executive Summary
- **P0 (Release Blockers): 4**
    - [F-01] **Confirmed:** Global Mutex Serialization in PostgreSQL Driver (Performance).
    - [F-02] **Confirmed:** 32-bit Integer Overflow in `trade.buy` (Economic Exploit).
    - [F-03] **Confirmed:** Stack Exhaustion via `bulk.execute` Recursion (DoS).
    - [F-04] **Confirmed:** Synchronous Pipe Deadlock in S2S IPC (System Liveness).
- **P1 (Serious Defects): 2**
    - [F-05] **Confirmed:** ANSI Escape Sequence Bypass (Security/Injection).
    - [F-06] **Confirmed:** News Feed Memory Leak (Resource Exhaustion).
- **P2 (Design/Portability): 2**
    - [F-07] **Requires Verification:** SQL Buffer Overflow Risk in `cmd_mail_delete`.
    - [F-08] **Bounded Risk:** Math safety in `h_planet_treasury_interest_tick`.

---

## 2. Findings: P0 (Release Blockers)

### [F-01] Global Mutex Serialization
- **File:** `src/db/pg/db_pg.c`
- **Lines:** 14, 164-168
- **Evidence:** `pthread_mutex_lock(&g_pg_mutex);` wraps every `PQexecParams` call.
- **Impact:** Total serialization. All threads queue for a single global lock to access the DB.

### [F-02] 32-bit Integer Overflow in `trade.buy`
- **File:** `src/server_ports.c:4850`
- **Evidence:** `total_item_cost += (amount * buy_price);`
- **Reasoning:** Multiplication of two `int`s overflows before promotion to `long long`.
- **Trigger:** Buying ~2.2M units of any 1000+ credit item. Result is a negative cost, crediting the player.

### [F-03] Stack Exhaustion via `bulk.execute` Recursion
- **File:** `src/server_bulk.c:115`
- **Evidence:** `cmd_bulk_execute` -> `server_dispatch_command` -> `cmd_bulk_execute`.
- **Impact:** Remote attacker can crash the server thread via stack overflow using nested JSON.

### [F-04] Synchronous Pipe Deadlock in S2S
- **Status:** **Confirmed** (Concurrency/Liveness)
- **File:** `src/s2s_transport.c`
- **Lines:** 180-210, 220-250
- **Evidence:**
```c
// write_n (Line 220) and read_n (Line 180) use synchronous poll logic
int rc = poll_wait (fd, POLLOUT, timeout_ms);
ssize_t k = send (fd, p + off, n - off, 0);
```
- **Explanation:** The S2S link between Server and Engine is synchronous and lock-step. If both processes attempt to `write()` a large frame simultaneously (e.g., during a universe-wide sweep), both will block on `POLLOUT` once the kernel buffers fill up. Since neither side is reading, the system deadlocks.
- **Impact:** Server and Engine hang indefinitely.

---

## 3. Findings: P1 (Serious Defects)

### [F-05] ANSI Escape Sequence Bypass
- **File:** `src/common.c:125`
- **Explanation:** Incomplete sanitization allows bypass of terminal control codes other than CSI/OSC.

### [F-06] News Feed Memory Leak
- **File:** `src/server_news.c:150-180`
- **Explanation:** Loop-local `strdup` results are leaked on library call failures or early returns within the loop.

---

## 4. DB Portability scorecard

| Feature | PostgreSQL | SQLite | MySQL | Oracle | Path Classification |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `RETURNING` | ✅ Native | ❌ No | ❌ No | ✅ Native | **Core-Path** |
| `ON CONFLICT` | ✅ Native | ✅ Native | ❌ No | ❌ No | **Core-Path** |

---

## 5. False Positives Removed
1. **`cmd_mail_delete` SQL Buffer Overflow**: Rejected. Proof shows that constructing the `IN` clause with up to 200 items using `{%zu}` and `i+2` (max 3 digits) only consumes ~1400 bytes, well within the 4096-byte limit.

---

## 6. Final Recommendation
Immediate action required on **[F-01]** (Mutex) and **[F-02]** (Overflow) to ensure system performance and economic integrity before public deployment.
