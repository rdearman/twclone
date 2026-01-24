# Stock Ambiguity Removal — Implementation Plan (v2)

**Primary Goal:** Eliminate "stock" ambiguity (commodity vs. equity) by clarifying protocol API and terminology  
**Secondary Goal (Optional, Phase 2):** Namespace clarity for commodity trading  
**Scope:** Protocol commands, C code, documentation, tests, event handling — NO schema changes  
**Timeline:** 3–5 weeks (Phase 1+3 core; Phase 2 behind optional go/no-go gate)  
**Risk Level:** LOW-MEDIUM (no database migrations, aliased backwards compatibility)

---

## Executive Summary: What's Changing

### Current Ambiguity

"**stock**" currently refers to **TWO unrelated systems**:

1. **Commodity inventory** on ports/planets (ore, organics, equipment quantities)
   - Example: "Check the stock of ore at Station Alpha"
   - Commands: `trade.buy`, `trade.sell`, `trade.port_info`

2. **Corporate equity shares** (ownership stakes)
   - Example: "Buy stock in Ferengi Industries"
   - Commands: `stock.ipo.register`, `stock.buy`, `stock.sell`, `stock.dividend.set`

**Result:** Confusion in code, documentation, client APIs, and player communications.

### Solution: API + Terminology Clarity

Rename equity commands to be unambiguous:
- **`stock.*`** → **`equity.*`** (corporate shares, IPO, dividends)
- Keep **`trade.*`** unchanged (commodity trading) — *for now*

Fix all human-facing terminology:
- "stock levels" → "inventory levels" (when referring to commodities)
- Remove ambiguous "stock" from help text, docs, comments

**Backwards Compatibility:**
- Old `stock.*` commands continue to work via **aliasing** (routed to new handlers)
- Deprecation warnings logged when old names used
- Clear sunset timeline (v2.0)

---

## Why NOT Renaming `trade.*` (Phase 2) Right Now

**Phase 1 + Phase 3 ALONE fixes the ambiguity.**

Renaming `trade.*` → `trade.commodity.*` is a **namespace rebrand**:
- ✅ Cleaner and more explicit
- ❌ Multiplies surface-area churn:
  - ~9 command names change
  - ~25 test invocations
  - Docs examples
  - Bot scripts
  - Autocomplete entries
  - Help text

**Decision framework:** Only do Phase 2 if you have **explicit buy-in** after Phase 1 lands cleanly.

---

## Phase 1: Equity Command Renaming (REQUIRED)

**Duration:** ~1.5 weeks  
**Effort:** ~7–8 hours  
**Risk:** MEDIUM (protocol changes, but fully aliased)  
**Exit Criterion:** Both `stock.*` (old) and `equity.*` (new) commands work; tests pass; no ambiguity

### 1.1 Identify All Touchpoints

**Files with stock.* references (enumerated):**

| File | Type | Occurrences | Example |
|------|------|-------------|---------|
| `src/server_loop.c` | Command registry | 7 entries | `{"stock.buy", cmd_stock, ...}` |
| `src/server_corporation.c` | Handlers | 14 functions | `cmd_stock()`, `cmd_stock_buy()` |
| `src/server_corporation.c` | Event emissions | 5 strings | `"stock.ipo.registered"`, `"stock.buy"` |
| `tests.v2/suite_economy_stocks.json` | Test invocations | ~40 | `stock.buy`, `stock.sell`, etc. |
| `tests.v2/suite_stock_market.json` | Test invocations | ~30 | IPO, dividend, portfolio tests |
| `docs/PROTOCOL.v3/25_Corporations_and_Stock_Market.md` | Protocol doc | 15+ refs | Command definitions, examples |
| Various `.c` files | Comments/strings | ~20 | "// stock operations", LOGI "stock update" |

**Total: ~132 identifiers across 7 file categories**

---

### 1.2 Rename Command Handlers

**File:** `src/server_corporation.c`  
**Changes:**

| Current Function | Proposed Function | Lines | Impact |
|------------------|-------------------|-------|--------|
| `cmd_stock()` | `cmd_equity()` | 1–50 | Primary handler (used by multiple commands) |
| `cmd_stock_buy()` | `cmd_equity_buy()` | 1700–1820 | Explicit buy handler |
| `cmd_stock_sell()` | `cmd_equity_sell()` | 1964–2090 | Explicit sell handler |
| `cmd_stock_dividend_set()` | `cmd_equity_dividend_set()` | 1825–1935 | Dividend declaration |
| `h_get_corp_stock_id()` | `h_get_corp_equity_id()` | Helper | Called by multiple handlers |
| `h_get_stock_info()` | `h_get_equity_info()` | Helper | Called by handlers |

**Effort:** ~1.5 hours  
**Tools:** IDE refactor (rename symbol across file)

---

### 1.3 Register Both Old and New Commands

**File:** `src/server_loop.c` (command registry in `cmd_table`)  
**Changes:**

| Current Entry | New Entry (Primary) | Alias Entry (Deprecated) | Notes |
|---------------|-------------------|--------------------------|-------|
| `stock.ipo.register` | `equity.ipo_register` | `stock.ipo_register` → `cmd_equity()` | Register corp equity IPO |
| `stock.buy` | `equity.buy` | `stock.buy` → `cmd_equity_buy()` | Purchase shares |
| `stock.sell` | `equity.sell` | `stock.sell` → `cmd_equity_sell()` | Sell shares |
| `stock.dividend.set` | `equity.dividend_set` | `stock.dividend_set` → `cmd_equity_dividend_set()` | Set dividend payout |
| `stock.exchange.list_stocks` | `equity.exchange.list` | `stock.exchange.list_stocks` → `cmd_equity()` | List exchange stocks |
| `stock.exchange.orders.create` | `equity.exchange.order.create` | `stock.exchange.orders.create` → (same) | Create order |
| `stock.exchange.orders.cancel` | `equity.exchange.order.cancel` | `stock.exchange.orders.cancel` → (same) | Cancel order |
| `stock.portfolio.list` | `equity.portfolio.list` | `stock.portfolio.list` → (same) | List holdings |

**Implementation:**
- Add **new** entries to `cmd_table` with `cmd_equity_*` handlers
- Keep **old** entries pointing to same handlers (aliases)
- Mark old entries with **deprecated flag** (add `deprecated: true` field if schema supports it, or document in registry)

**Effort:** ~1 hour

---

### 1.4 Update `system.cmd_list` Output

**Problem:** Command discovery must clearly indicate canonical vs. deprecated.

**Current behavior:** `system.cmd_list` returns all commands; clients see both old and new.

**Desired behavior:**
- **Option A:** Only return **new** `equity.*` command names in `system.cmd_list`
  - Old `stock.*` commands still work but are hidden from discovery
  - Cleaner client experience; forces gradual migration
- **Option B:** Return **both**, but mark old names with `deprecated: true` flag
  - Backwards compatible; clients can see sunset timeline
  - More transparent

**Recommendation:** **Option B** (more transparent deprecation)

**Implementation:**
- Modify `cmd_system_cmd_list()` in `src/server_envelope.c`
- For each command, include metadata:
  ```json
  {
    "name": "equity.buy",
    "help": "Purchase corporate shares",
    "canonical": true,
    "deprecated": null
  },
  {
    "name": "stock.buy",
    "help": "Purchase corporate shares (DEPRECATED: use equity.buy)",
    "canonical": false,
    "deprecated": {
      "sunset_version": "v2.0",
      "replacement": "equity.buy"
    }
  }
  ```

**Effort:** ~1.5 hours (modify cmd_list output + add deprecation metadata)

---

### 1.5 Handle Engine Event Types (Persistence Constraint)

**Critical Issue:** Event types are **stored in DB** (`engine_events.type` column) and must not break consumers.

**Current event types emitted:**
```
"stock.ipo.registered"
"stock.buy"
"stock.sell"
"stock.dividend.declared"
```

**Decision point:**

**Option A: Rename event types** (cleaner, but risky)
- Emit `"equity.ipo.registered"`, `"equity.buy"`, etc.
- **Risk:** Existing events in DB have old type; engine event handlers won't match
- **Mitigation:** Data migration (cron job renames event types) OR dual handlers

**Option B: Keep event types unchanged** (safest)
- Continue emitting `"stock.ipo.registered"` etc. internally
- Only change external documentation/terminology
- **Trade-off:** Internal naming doesn't match external API (acceptable for now)
- **Future:** In Phase 3 deprecation, revisit if doing full cleanup

**Recommendation:** **Option B** (safer for Phase 1)

**Implementation:**
- Do NOT rename event type strings in code
- Document this decision clearly in codebase
- Add comment at emission sites:
  ```c
  // Event type kept as "stock.buy" for DB compatibility.
  // External API uses "equity.buy" (see deprecation plan).
  db_log_engine_event(..., "stock.buy", ...);
  ```

**Effort:** ~0.5 hour (decision + documentation)

---

### 1.6 Update Test Suites

**Files to modify:**
- `tests.v2/suite_economy_stocks.json`
- `tests.v2/suite_stock_market.json`

**Changes:**
- Update all `stock.*` command invocations to `equity.*` equivalents
- Add **regression tests** using old `stock.*` command names (verify aliases work)

**Scope:** ~40 invocations across both files

**Example:**
```json
// Old test (update this):
{"command": "stock.buy", "data": {"stock_id": 1, "quantity": 10}}

// Becomes:
{"command": "equity.buy", "data": {"stock_id": 1, "quantity": 10}}

// Add regression test:
{"command": "stock.buy", "data": {"stock_id": 1, "quantity": 10}, "expect_deprecated": true}
```

**Effort:** ~1.5 hours

---

### 1.7 Update Protocol Documentation

**Files to modify:**
- `docs/PROTOCOL.v3/25_Corporations_and_Stock_Market.md` (rewrite commands section)
- `docs/PROTOCOL.v3/02_Envelope_and_Metadata.md` (add deprecation metadata to command envelope section)

**Changes:**
- Document new `equity.*` command names with full examples
- Add **deprecation notice** on old `stock.*` names
- Add **glossary** explaining terminology split:
  - **Equity:** Corporate shares, IPO, dividends, indices
  - **Commodity:** Port inventory, ore, organics, equipment
- Document event type decision (kept as `stock.*` for now)

**Effort:** ~1.5 hours

---

### 1.8 Smoke Testing

**Tests to execute:**
1. IPO flow with **new** `equity.*` commands
2. IPO flow with **old** `stock.*` commands (alias path)
3. Buy/sell shares with both APIs
4. Dividend declaration and payout
5. Portfolio listing
6. Exchange order creation/cancellation
7. Verify `system.cmd_list` returns both old/new with proper metadata
8. Verify deprecation metadata present in command list
9. Verify event emission works (events still use `stock.*` type)
10. Verify no errors in logs

**Effort:** ~1 hour

---

### Phase 1 Exit Criteria

✅ **API Clarity:**
- `equity.*` commands fully functional
- `stock.*` commands still work via aliasing
- `system.cmd_list` shows both with deprecation metadata

✅ **Tests Pass:**
- All equity tests pass with new command names
- Regression tests pass with old command names
- No functional changes (same DB effects, same response payloads)

✅ **Documentation:**
- Protocol docs updated with `equity.*` examples
- Deprecation notices clear and linked
- Event type decision documented

✅ **No Breakage:**
- Old clients using `stock.*` still work
- Event handling unchanged (events still use `stock.*` type)

---

## Phase 3: Terminology Cleanup (REQUIRED After Phase 1)

**Duration:** ~1–2 weeks  
**Effort:** ~5–6 hours  
**Risk:** LOW (docs and strings, no logic changes)  
**Exit Criterion:** No ambiguous "stock" terminology in player-facing text

### 3.1 Search and Fix "Stock Levels" → "Inventory Levels"

**Scope:** Anywhere "stock" is used to describe commodity quantities on ports/planets

**Files to audit (with estimated occurrences):**

| File | Type | Occurrences | Pattern |
|------|------|-------------|---------|
| `src/server_ports.c` | Help text, logs, comments | ~15 | "stock of", "stock levels", "port stock" |
| `docs/GALACTIC_ECONOMY.md` | Conceptual docs | ~8 | "stock of commodities", "stock replenishment" |
| `docs/PROTOCOL.v3/22_Trade_and_Port_Commands.md` | Command examples, descriptions | ~12 | "trade stock", "stock available" |
| `docs/PROTOCOL.v3/24_Planets_Outposts_Stations.md` | Planet mechanics | ~6 | "planet stock levels" |
| `README.md` | Examples | ~2 | Trade examples |
| Various `.c` comments | Code comments | ~8 | "// check stock", "// update stock" |

**Total: ~51 occurrences**

**Changes:**
```
"stock of ore"         → "inventory of ore" or "ore holdings"
"stock levels"         → "inventory levels"
"port stock"           → "port inventory"
"Check the stock"      → "Check the inventory"
"stock available"      → "quantity available"
"update stock"         → "update inventory"
"stock replenishment"  → "commodity replenishment" or "inventory replenishment"
```

**Effort:** ~2 hours (grep + systematic replace + review)

---

### 3.2 Fix "Stock" in Help Text and Error Messages

**Scope:** Player-facing help text where "stock" is ambiguous

**Files:**
- `src/server_ports.c` (help text for `trade.*` commands)
- Any error messages mentioning "stock"

**Changes:**
- Remove word "stock" from all help text about commodities
- Use "commodity", "inventory", or "goods" instead
- Verify no ambiguity remains

**Example:**
```c
// Before:
"help": "Buy stock from a port"

// After:
"help": "Buy commodities from a port"
```

**Effort:** ~1.5 hours

---

### 3.3 Update Code Comments

**Scope:** C code comments referring to commodity "stock"

**Changes:**
```c
// Before:
// Check if port has stock of commodity

// After:
// Check if port has sufficient inventory of commodity
```

**Effort:** ~1 hour

---

### 3.4 Update README and Main Documentation

**Files:**
- `README.md` (or equivalent)

**Changes:**
- Update any example code mentioning "stock" to use "inventory"
- Add glossary section explaining the terminology split
- Ensure first-time readers understand commodity vs. equity

**Effort:** ~0.5 hour

---

### Phase 3 Exit Criteria

✅ **Terminology Clarity:**
- No "stock levels" in player-facing docs (now "inventory levels")
- All commodity-related "stock" strings changed to "commodity" or "inventory"
- Protocol docs fully reflect terminology distinction

✅ **No Remaining Ambiguity:**
- Reader cannot confuse commodity inventory with equity shares
- Every use of "stock" (if any remains) refers only to equity

---

## Phase 2: Commodity Command Renaming (OPTIONAL After Phase 1 Go/No-Go)

⚠️ **This phase is OPTIONAL and addresses namespace clarity, not ambiguity.**

**Duration:** ~1 week (only if approved)  
**Effort:** ~4–5 hours  
**Risk:** LOW (isolated, straightforward aliasing)

**Gate Condition:** Full go/no-go after Phase 1 lands + team review.

**If approved**, the scope is:
- Rename `trade.*` → `commodity.*` (9 command names)
- Add aliases for backwards compat
- Update ~25 test invocations
- Update docs examples

**We will NOT execute Phase 2 without explicit sign-off.**

---

## Implementation Timeline

| Phase | Focus | Duration | Effort | Dependencies | Status |
|-------|-------|----------|--------|--------------|--------|
| **Phase 1** | Equity API + handler rename | 1.5w | 7–8h | None (foundation) | **REQUIRED** |
| **Phase 3** | Terminology cleanup | 1–2w | 5–6h | Follows Phase 1 | **REQUIRED** |
| **Phase 2** | Commodity namespace (optional) | 1w | 4–5h | Gate after P1 | **OPTIONAL** |
| **Total** | | 3–5w | **12–19h** | Phase 1 first | |

---

## Backwards Compatibility Strategy

### Command Routing (Aliasing)

All old command names continue to work via **explicit alias entries**:

```
Client sends:  stock.buy
Server lookup: "stock.buy" found in cmd_table (alias entry)
Dispatch to:   cmd_equity_buy()
Handler runs:  same logic as always
Response sent: same format (with optional deprecation warning in meta)
```

### Response Metadata (Deprecation Signal)

When old command name is used, response includes deprecation warning:

```json
{
  "id": "...",
  "status": "ok",
  "reply_to": "...",
  "data": { /* result */ },
  "meta": {
    "warnings": [
      {
        "code": "DEPRECATED_COMMAND",
        "message": "stock.buy is deprecated; use equity.buy instead",
        "replacement": "equity.buy",
        "sunset_version": "v2.0"
      }
    ]
  }
}
```

### Deprecation Timeline

- **v1.0:** New names available, old names aliased (transparent, no warnings by default)
- **v1.5 (optional):** Deprecation warnings begin appearing in response metadata
- **v2.0 (optional):** Old names removed entirely (breaking change announced in advance)

---

## Testing Strategy: Behavioural Equivalence + Deprecation Checks

### Test Categories

#### 1. Functional Equivalence Tests
Test that `equity.*` and `stock.*` produce **identical results**:
```
Inputs:  equity.buy(stock_id=1, quantity=10)
         stock.buy(stock_id=1, quantity=10)

Expected:
  - Identical DB state (player shares, corp treasury)
  - Identical response payload
  - Only difference: optional deprecation warning in meta
```

**Effort:** ~1 hour (add 5–10 equivalence test cases)

#### 2. Command List Tests
Verify `system.cmd_list` returns proper metadata:
```
Test: 
  - system.cmd_list returns "equity.buy" marked canonical
  - system.cmd_list returns "stock.buy" marked deprecated
  - Both have proper help text
  - Deprecation metadata includes sunset_version
```

**Effort:** ~0.5 hour

#### 3. Deprecation Warning Tests
Verify old command invocations return warnings:
```
Test:
  - Command with new name → no warning
  - Command with old name → warning in response meta
  - Warning includes replacement name
```

**Effort:** ~0.5 hour

#### 4. Event Emission Tests
Verify event handling unchanged:
```
Test:
  - equity.buy emits "stock.buy" event (by design)
  - Engine still processes "stock.buy" events
  - No dead-letter events
```

**Effort:** ~0.5 hour

#### 5. Regression Tests
Run full equity workflow with both APIs:
```
Scenarios to test:
  - IPO creation → equity.ipo_register + stock.ipo_register
  - Buy/sell flow → equity.buy/.sell + stock.buy/.sell
  - Dividend payout → equity.dividend_set + stock.dividend_set
  - Exchange trading → equity.exchange.* + stock.exchange.*
  - Portfolio listing → equity.portfolio.list + stock.portfolio.list

Verify:
  - Same final state with both APIs
  - Event logs match
  - Prices/calculations unchanged
```

**Effort:** ~2 hours

---

## Rollback Strategy and Instrumentation

### Rollback Scenario
If issues arise post-deployment:

**Single revert commit:**
1. Remove all `equity.*` entries from `cmd_table`
2. Remove alias entries from `cmd_list` output
3. Revert handler renames (or keep; they're internal)
4. Restore old command registry

**Effort:** ~30 minutes (pre-written revert script)

### Instrumentation

Add usage telemetry to track migration:

**Option A:** Count command invocations in logs
```c
// In cmd dispatcher:
LOGD("cmd_executed: %s (deprecated=%s)", cmd_name, is_alias ? "true" : "false");
```

**Option B:** Count in database
```sql
-- Add column to audit table:
CREATE TABLE cmd_audit (ts, player_id, command_name, is_deprecated);

-- Query usage:
SELECT command_name, COUNT(*) as invocations
FROM cmd_audit
WHERE ts > '2025-01-01'
GROUP BY command_name
ORDER BY invocations DESC;
```

**Effort:** ~1 hour (add instrumentation during Phase 1)

---

## Risk Register: Stringly-Typed Landmines

### Hard-coded Command Names (Audit Required)

**Must explicitly search for and enumerate:**

| Location | Search Pattern | Risk | Mitigation |
|----------|---|------|-----------|
| Test files | `"stock\."` | HIGH | Update all test files |
| Bots/scripts | `stock\.buy\|stock\.sell` | HIGH | Notify bot maintainers |
| Metrics/telemetry | Command names in keys | MEDIUM | Update metric dashboards |
| Docs/examples | `stock\.(ipo\|buy\|sell)` | MEDIUM | Update all docs |
| Auth/permissions | Command allow-lists | MEDIUM | Verify permissions work for aliases |
| Audit logs | Stored command names | MEDIUM | Don't change stored; only new logs use new names |

**Pre-implementation audit task:**
```bash
# Find all hardcoded stock command references:
grep -r "stock\." /home/rick/twclone/tests* --include="*.json" | wc -l
grep -r "stock\." /home/rick/twclone/docs --include="*.md" | wc -l
grep -r "stock\." /home/rick/twclone/src --include="*.c" | wc -l
grep -r '"stock\.' /home/rick/twclone --include="*.py" --include="*.js"
```

**Effort:** ~0.5 hour (grep audit before implementation)

---

## Effort Summary (Detailed Breakdown)

### Phase 1: Equity Renaming (7–8 hours)

| Task | Files | Edits | Effort | Notes |
|------|-------|-------|--------|-------|
| Rename handlers | `server_corporation.c` | 5 functions | 1.5h | IDE refactor |
| Register cmd names | `server_loop.c` | 8 entries | 1h | Add aliases |
| Update cmd_list output | `server_envelope.c` | ~30 lines | 1.5h | Add deprecation metadata |
| Event type decision | `server_corporation.c` | ~5 lines (docs) | 0.5h | Document, don't rename |
| Update tests | `suite_*.json` | ~40 invocations | 1.5h | Global replace + verify |
| Update protocol docs | `PROTOCOL.v3/25*.md` | ~20 refs | 1.5h | Examples + deprecation |
| Smoke tests | (testing) | — | 1h | Execute test scenarios |
| **Total Phase 1** | | **~133 edits** | **7–8h** | |

### Phase 3: Terminology (5–6 hours)

| Task | Files | Occurrences | Effort |
|------|-------|-------------|--------|
| Fix "stock levels" | 6 files | ~51 | 2h |
| Fix help/error text | 2 files | ~10 | 1.5h |
| Fix code comments | 3 files | ~8 | 1h |
| Update README | 1 file | ~2 | 0.5h |
| **Total Phase 3** | | **~71 occurrences** | **5–6h** |

### Phase 2 (If Approved): Commodity Namespace (4–5 hours)

| Task | Files | Changes | Effort |
|------|-------|---------|--------|
| Rename handlers | `server_ports.c` | 9 functions | 1h |
| Register cmd names | `server_loop.c` | 9 entries | 1h |
| Update tests | `suite_*.json` | ~25 invocations | 1h |
| Update docs | `PROTOCOL.v3/22*.md` | ~15 refs | 0.5h |
| Smoke tests | (testing) | — | 0.5h |
| **Total Phase 2** | | **~59 edits** | **4–5h** |

### Grand Total (Phase 1 + 3)
- **Hours:** 12–14
- **Edits:** ~204
- **Files touched:** ~15
- **Complexity:** MEDIUM

---

## Critical Unknowns to Resolve BEFORE Implementation

**Must explicitly confirm:**

- [ ] Protocol versioning: Does schema support "deprecated" metadata on commands?
  - If yes: easy to add to `system.cmd_list`
  - If no: alternative (tag in help text)?
- [ ] Command registry structure: Confirm current format in `cmd_table`
- [ ] Can we modify `system.cmd_list` output to include metadata?
- [ ] Are there any bots/external tools hardcoding `stock.*` commands?
- [ ] Does the event system have consumer handlers that will break if we rename event types?
- [ ] Any analytics/telemetry dashboards tracking "stock.*" command invocations?
- [ ] Confirm both PostgreSQL and MySQL DB connection paths (no schema changes, but need to verify)
- [ ] Any external documentation or API references to update?

---

## Success Criteria (After Phase 1 + 3)

✅ **API Clarity:**
- `equity.*` is the canonical command namespace for corporate shares
- `stock.*` commands still work but are marked deprecated
- Every command in `system.cmd_list` is unambiguous

✅ **Terminology Clarity:**
- No "stock levels" in player-facing docs (now "inventory levels")
- No ambiguous use of "stock" for commodities
- Glossary explicitly separates equity from commodity trading

✅ **Documentation:**
- Protocol v3 docs use only `equity.*` examples
- Old `stock.*` names noted as deprecated with migration guidance
- Event type design decision documented

✅ **Backwards Compatibility:**
- Clients using old `stock.*` commands work without code changes
- Deprecation warnings appear in response metadata (optional)
- Clear sunset timeline communicated (v2.0)

✅ **Code Quality:**
- Handler names match their domain (`cmd_equity_*` not `cmd_stock_*`)
- No remaining ambiguity in variable names or comments
- Event types documented (kept as `stock.*` for persistence)

---

## Recommended Team & Roles

- **1 Backend Engineer** (C code changes, command routing, handler renames) — 5–6 hours
- **1 QA/Tester** (test updates, regression testing, smoke tests) — 2–3 hours
- **0.5 Documentation Writer** (update docs, glossary, examples) — 1–2 hours

**Can be done by 1–2 people over 3–5 weeks if time-boxed.**

---

## Next Steps (Before Implementation)

### Pre-Implementation Checklist

- [ ] Team reviews this plan
- [ ] Confirm protocol versioning supports deprecated metadata
- [ ] Audit all hardcoded `stock.*` references (grep output)
- [ ] Confirm event system design (event type naming decision)
- [ ] Notify any known bot/external tool maintainers
- [ ] Confirm timeline and resource allocation
- [ ] Create Phase 1 implementation branch
- [ ] Set up automated testing framework for both old/new commands

### Go/No-Go Gates

**After Phase 1 lands:**
- [ ] Phase 1 tests all pass
- [ ] Command list output correct (with deprecation metadata)
- [ ] No regressions in equity trading workflows
- [ ] Old and new command names work equivalently
- [ ] Deprecation warnings visible in response meta

**If gate passes:** Proceed to Phase 3 (required)

**After Phase 3 lands:**
- [ ] All terminology updated
- [ ] No "stock" ambiguity in docs/help text
- [ ] Smoke tests pass
- [ ] Ready for v1.0 release

**Decision on Phase 2:**
- [ ] Team evaluates Phase 1 success
- [ ] If clean: decide whether commodity namespace rebrand is worth the churn
- [ ] If yes: schedule Phase 2
- [ ] If no: document decision; can always do later

---

## Appendix: File-by-File Changes (Summary)

### Modified Files (Phase 1)

1. **`src/server_corporation.c`**
   - Rename: `cmd_stock()` → `cmd_equity()`
   - Rename: `cmd_stock_buy()` → `cmd_equity_buy()`
   - Rename: `cmd_stock_sell()` → `cmd_equity_sell()`
   - Rename: `cmd_stock_dividend_set()` → `cmd_equity_dividend_set()`
   - Rename: `h_get_corp_stock_id()` → `h_get_corp_equity_id()`
   - Rename: `h_get_stock_info()` → `h_get_equity_info()`
   - Keep: All event type strings (`"stock.buy"` etc. — documented)
   - Add: Comments explaining event type design decision

2. **`src/server_loop.c`**
   - Add new entries: `equity.ipo_register`, `equity.buy`, `equity.sell`, `equity.dividend_set`, etc.
   - Keep old entries: `stock.*` as aliases (same handlers)
   - Mark old entries: `deprecated: true` (add to structure if supported)

3. **`src/server_envelope.c`**
   - Modify: `cmd_system_cmd_list()` to include deprecation metadata
   - Add fields: `canonical`, `deprecated` (with sunset version)

4. **`tests.v2/suite_economy_stocks.json`**
   - Update: All `stock.*` invocations to `equity.*`
   - Add: Regression tests using old `stock.*` names

5. **`tests.v2/suite_stock_market.json`**
   - Update: All command invocations to new names
   - Add: Deprecation warning tests

6. **`docs/PROTOCOL.v3/25_Corporations_and_Stock_Market.md`**
   - Rewrite commands section to show `equity.*`
   - Add deprecation notice for `stock.*`
   - Add glossary entry

7. **`docs/PROTOCOL.v3/02_Envelope_and_Metadata.md`**
   - Add section documenting deprecation metadata in response envelope

### Modified Files (Phase 3)

1. **`src/server_ports.c`**
   - Replace: "stock levels" → "inventory levels"
   - Replace: "stock of X" → "inventory of X"
   - Fix: Help text for trade commands

2. **`docs/GALACTIC_ECONOMY.md`**
   - Replace: All commodity "stock" references → "inventory"

3. **`docs/PROTOCOL.v3/22_Trade_and_Port_Commands.md`**
   - Replace: "stock" → "commodity" where it means goods
   - Update examples

4. **`docs/PROTOCOL.v3/24_Planets_Outposts_Stations.md`**
   - Replace: "stock levels" → "inventory levels"

5. **`README.md`**
   - Update trade examples
   - Add glossary section

---

## Document Version History

| Version | Date | Changes |
|---------|------|---------|
| v2 | 2025-01-23 | Integrated 10 feedback points: Phase 2 optional, explicit file counts, test strategy, event handling, deprecation policy, cmd_list design, rollback plan, stringly-typed audit, early terminology fixes |

