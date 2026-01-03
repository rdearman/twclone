# Portability Closure Audit

**Date:** 2026-01-03  
**Purpose:** Mechanical, countable checklist for MySQL 9.5+ migration

---

## 1. Dialect Inventory (Counts + Locations)

### 1.1 PostgreSQL $N Placeholders

**Total:** 706 occurrences in src/  
**Status:** NO driver primitive (requires placeholder translation layer)

| File | Count |
|------|------:|
| src/database_cmd.c | 132 |
| src/server_combat.c | 81 |
| src/server_ports.c | 58 |
| src/server_cron.c | 48 |
| src/server_planets.c | 47 |
| src/server_stardock.c | 41 |
| src/server_corporation.c | 39 |
| src/server_players.c | 32 |
| src/server_universe.c | 27 |
| src/server_bank.c | 23 |
| src/server_engine.c | 21 |
| src/db_player_settings.c | 19 |
| src/server_communication.c | 18 |
| src/server_clusters.c | 18 |
| src/engine_consumer.c | 17 |
| src/server_auth.c | 15 |
| src/server_ships.c | 12 |
| src/database_market.c | 11 |
| src/server_cmds.c | 8 |
| src/server_warp_post_processing.c | 6 |
| src/server_citadel.c | 6 |
| src/bigbang_pg_main.c | 6 |
| src/server_news.c | 5 |
| src/db/sql_driver.c | 4 |
| src/db/pg/db_pg.c | 4 |
| src/test_bang.c | 2 |
| src/s2s_keyring.c | 2 |
| src/server_loop.c | 1 |

---

### 1.2 RETURNING Clause

**Total:** 20 hardcoded in business logic  
**Driver primitive:** `db_exec_returning()` exists (stub)  
**Migrated:** 0

| Location | Pattern |
|----------|---------|
| src/server_players.c:1319 | INSERT...RETURNING id |
| src/server_players.c:1567 | UPDATE...RETURNING credits |
| src/server_players.c:1656 | UPDATE...RETURNING credits |
| src/server_players.c:2198 | UPDATE...RETURNING credits |
| src/server_players.c:2294 | UPDATE...RETURNING credits |
| src/server_planets.c:1732 | INSERT...RETURNING planet_id |
| src/server_engine.c:754 | INSERT...RETURNING system_notice_id |
| src/server_engine.c:844 | INSERT...RETURNING system_notice_id |
| src/server_combat.c:2150 | UPDATE...RETURNING fighters |
| src/server_combat.c:2202 | UPDATE...RETURNING mines |
| src/server_combat.c:2211 | UPDATE...RETURNING limpets |
| src/server_combat.c:3453 | DELETE...RETURNING sector_assets_id |
| src/server_combat.c:3468 | DELETE...RETURNING sector_assets_id |
| src/server_combat.c:3549 | UPDATE...RETURNING credits |
| src/server_combat.c:3826 | INSERT...RETURNING sector_assets_id |
| src/database_market.c:60 | INSERT...RETURNING commodity_orders_id |
| src/server_bank.c:66 | INSERT...RETURNING id, balance |
| src/server_bank.c:75 | INSERT...RETURNING id, balance |
| src/server_bank.c:481 | UPDATE...RETURNING balance |
| src/server_bank.c:558 | UPDATE...RETURNING balance |

---

### 1.3 ON CONFLICT DO NOTHING

**Total:** 10 hardcoded in business logic (excluding driver/comments)  
**Driver primitive:** `sql_insert_ignore_clause()` exists  
**Migrated:** 4 callsites

| Location | Migrated? |
|----------|:---------:|
| src/db_player_settings.c:139 | ✅ |
| src/server_players.c:175 | ✅ |
| src/server_clusters.c:92 | ✅ |
| src/server_corporation.c:515 | ✅ |
| src/bigbang_pg_main.c:367 | ❌ (raw libpq) |
| src/database_cmd.c:1167 | ❌ |
| src/database_cmd.c:3406 | ❌ |
| src/database_cmd.c:4190 | ❌ |
| src/server_auth.c:393 | ❌ |
| src/server_auth.c:547 | ❌ |
| src/server_bank.c:627 | ❌ |

**Remaining:** 6 (excluding bigbang_pg_main.c)

---

### 1.4 ON CONFLICT DO UPDATE

**Total:** 29 hardcoded in business logic (excluding driver)  
**Driver primitive:** `sql_upsert_do_update()` exists  
**Migrated:** 0

| Location | Conflict Target |
|----------|-----------------|
| src/server_cmds.c:317 | player_id |
| src/server_ports.c:232 | entity_type, entity_id, commodity_code |
| src/server_ports.c:2885 | port_id, player_id |
| src/server_ports.c:2892 | player_id |
| src/server_ports.c:3184 | cluster_id, player_id |
| src/server_ports.c:3205 | player_id |
| src/server_ports.c:3250 | port_id, player_id |
| src/server_ports.c:3275 | cluster_id, player_id |
| src/server_ports.c:3310 | player_id |
| src/server_bank.c:1035 | player_id |
| src/server_auth.c:65 | player_id, event_type |
| src/server_auth.c:78 | player_id, event_type |
| src/server_auth.c:95 | player_id, event_type |
| src/server_auth.c:139 | player_id, event_type |
| src/bigbang_pg_main.c:186 | key (raw libpq) |
| src/engine_consumer.c:102 | key |
| src/engine_consumer.c:150 | engine_events_deadletter_id |
| src/db_player_settings.c:35 | player_id, event_type |
| src/db_player_settings.c:81 | player_id, name |
| src/db_player_settings.c:211 | player_id, scope, key |
| src/db_player_settings.c:350 | player_id, key |
| src/database_cmd.c:965 | planet_id, commodity |
| src/database_cmd.c:1770 | player_id |
| src/database_cmd.c:1790 | port_id, player_id |
| src/database_cmd.c:3262 | key |
| src/server_players.c:137 | player_id, key |
| src/server_players.c:151 | player_id, name |
| src/server_players.c:223 | player_id, topic |
| src/server_cron.c:1679 | entity_type, entity_id, commodity_code |
| src/server_cron.c:4182 | draw_date |
| src/server_communication.c:408 | notice_id, player_id |
| src/server_citadel.c:315 | planet_id |

**Remaining:** 29 (excluding bigbang_pg_main.c)

---

### 1.5 FOR UPDATE SKIP LOCKED

**Total:** 12 in server_combat.c (7 hardcoded SQL, 5 comments)  
**Driver primitive:** `sql_for_update_skip_locked()` exists  
**Migrated:** 5

| Location | Migrated? |
|----------|:---------:|
| src/server_combat.c:503 | ✅ |
| src/server_combat.c:550 | ✅ |
| src/server_combat.c:829 | ✅ |
| src/server_combat.c:1734 | ✅ |
| src/server_combat.c:1770 | ✅ |
| src/server_combat.c:2616 | ❌ |
| src/server_combat.c:2696 | ❌ |
| src/server_combat.c:3173 | ❌ |
| src/server_combat.c:3448 | ❌ |
| src/server_combat.c:3463 | ❌ |
| src/server_combat.c:3982 | ❌ |
| src/server_combat.c:4654 | ❌ |

**Remaining:** 7

---

### 1.6 JSON SQL Functions

**Total:** 0 hardcoded in business logic  
**Driver primitives:** `sql_json_object_fn()`, `sql_json_arrayagg_fn()` exist  
**Migrated:** 1 (all)

| Location | Migrated? |
|----------|:---------:|
| src/server_cron.c:1987 | ✅ |

**Remaining:** 0 ✅

---

### 1.7 PostgreSQL Type Casts (::type)

**Total:** 5 in business logic  
**Driver primitive:** NONE

| Location | Cast |
|----------|------|
| src/server_combat.c:2282 | ::bigint |
| src/server_combat.c:2339 | ::bigint |
| src/server_universe.c:813 | ::int |
| src/engine_consumer.c:192 | ::json |
| src/engine_consumer.c:430 | ::json |

---

### 1.8 ILIKE Pattern Operator

**Total:** 3  
**Driver primitive:** NONE

| Location |
|----------|
| src/server_universe.c:627 |
| src/server_universe.c:633 |
| src/server_universe.c:639 |

---

### 1.9 EXTRACT(EPOCH FROM NOW())

**Total:** 7 in business logic (excluding comments)  
**Driver primitive:** `sql_epoch_now()` exists  
**Migrated:** 0

| Location |
|----------|
| src/database_cmd.c:1167 |
| src/database_cmd.c:3383 |
| src/database_cmd.c:3406 |
| src/database_cmd.c:3601 |
| src/server_combat.c:2282 |
| src/server_combat.c:2339 |
| src/server_combat.c:3825 |

**Remaining:** 7

---

## 2. Driver Coverage Map

| Pattern | Has Primitive? | Migrated | Remaining | % Complete |
|---------|:--------------:|:--------:|:---------:|:----------:|
| $N placeholders | ❌ | 0 | 706 | 0% |
| RETURNING | ✅ (stub) | 0 | 20 | 0% |
| ON CONFLICT DO NOTHING | ✅ | 4 | 6 | 40% |
| ON CONFLICT DO UPDATE | ✅ | 0 | 29 | 0% |
| FOR UPDATE SKIP LOCKED | ✅ | 5 | 7 | 42% |
| JSON functions | ✅ | 1 | 0 | 100% |
| ::type casts | ❌ | 0 | 5 | 0% |
| ILIKE | ❌ | 0 | 3 | 0% |
| EXTRACT(EPOCH) | ✅ | 0 | 7 | 0% |

**Callsite migration:** 10 done / 77 remaining = **11.5%**  
**Note:** Placeholder translation (706) is driver-level, not per-callsite.

---

## 3. Concurrency Reality Check

### g_pg_mutex Analysis

**Location:** src/db/pg/db_pg.c:15

```c
// GOAL C: Temporary serialization of DB calls with a mutex
static pthread_mutex_t g_pg_mutex = PTHREAD_MUTEX_INITIALIZER;
```

**Behavior:** YES, it serializes ALL libpq calls (21 lock/unlock pairs):
- PQfinish, BEGIN, COMMIT, ROLLBACK
- All PQexecParams calls (exec, query)
- PQstatus, PQerrorMessage

**Intentional?** YES - marked as "GOAL C" and "Temporary"

**Impact:**
- All DB calls serialized globally, even with per-thread connections
- Per-thread connections exist via pthread_key_t (game_db.c:82-127)
- The mutex defeats the purpose of per-thread connections

**Recommendation:** 
- Remove g_pg_mutex when confident per-thread connections work
- MySQL driver should NOT have global serialization

---

## 4. UTC DATETIME Plan

### Current Time Column Types

**TIMESTAMPTZ columns:** 50+ (standard, portable)  
**BIGINT epoch columns:** 9

| Column | Default | Callsites |
|--------|---------|-----------|
| lottery.last_win_ts | none | 0 |
| podded_status.podded_last_reset | none | database_cmd.c:1167 |
| port_busts.last_bust_at | none | database_cmd.c:1790 |
| player_last_rob.last_attempt_at | none | database_cmd.c:1770 |
| entity_stock.last_updated_ts | EXTRACT(EPOCH) | server_ports.c:232, server_cron.c:1679 |
| bank_accounts.last_interest_tick | none | 0 |
| engine_cursor.last_event_ts | none | engine_consumer.c:102 |
| engine_events.created_ts | none | 0 |
| sector_log.published_ts | none | 0 |

### Recommendation

**Standard:** UTC DATETIME everywhere (PostgreSQL TIMESTAMPTZ, MySQL DATETIME)

**Migration blast radius:**
- Schema: Change 9 bigint columns to TIMESTAMPTZ/DATETIME
- Code: ~6 callsites (update SQL to use NOW() instead of EXTRACT(EPOCH))
- Low risk: Most columns already use TIMESTAMPTZ

---

## 5. Burndown Checklist

### Phase 1: Missing Driver Primitives

- [ ] sql_ilike() - 3 callsites
- [ ] sql_cast_int() - 1 callsite  
- [ ] sql_cast_json() - 2 callsites
- [ ] Placeholder translation ($1→?) - driver layer

### Phase 2: Callsite Migration

| Pattern | Remaining | Est. PRs |
|---------|----------:|:--------:|
| ON CONFLICT DO NOTHING | 6 | 1 |
| ON CONFLICT DO UPDATE | 29 | 3-4 |
| FOR UPDATE SKIP LOCKED | 7 | 1 |
| EXTRACT(EPOCH) | 7 | 1 |
| RETURNING | 20 | 2-3 |
| ILIKE | 3 | 1 |
| ::type casts | 5 | 1 |
| **TOTAL** | **77** | **10-12** |

---

## Verification Commands

```bash
rg -n '\$[0-9]+' src/ | grep -v '\.before_cocci' | wc -l                    # 706
rg -n 'RETURNING' src/ | grep -v '\.before_cocci\|db_api\|db_pg' | wc -l   # 21
rg -n 'ON CONFLICT DO NOTHING' src/ | grep -v '\.before_cocci' | wc -l     # 14
rg -n 'ON CONFLICT.*DO UPDATE' src/ | grep -v '\.before_cocci' | wc -l     # 42
rg -n 'FOR UPDATE SKIP LOCKED' src/ | grep -v sql_driver | wc -l           # 11
rg -n 'sql_for_update_skip_locked' src/ | grep -v sql_driver | wc -l       # 5
rg -n 'json_build_object' src/ | grep -v sql_driver | wc -l                # 0
rg -n '::[a-z]+' src/ | grep -v sql_driver | wc -l                         # 5
rg -n 'ILIKE' src/ | wc -l                                                  # 3
rg -n 'EXTRACT\(EPOCH' src/ | grep -v sql_driver | wc -l                   # 8
rg -n 'g_pg_mutex' src/db/pg/db_pg.c | wc -l                               # 21
```
