# V3 NPC and Federation phases

This document records the implementation boundary for the eight approved V3
phases.  Federation topology is created by BigBang and is immutable until a
new universe is generated.

1. **MSL enforcement:** `fedspace_cleanup` runs once per UTC day at 06:00.
   `fedspace_sweep_runs` claims the UTC date, making the sweep safe across
   restarts and concurrent scheduler paths.  Captain Z's sectors 1--10 path
   is unchanged.
2. **NPC action safety:** NPC actions use durable idempotency keys in
   `npc_action_log`; movement, trade, attacks and defensive deployment cannot
   be applied twice by the cron and engine paths.
3. **Ferengi economy:** Ferengi traders trade at ports using the existing
   authoritative stock, cargo and account helpers, then continue their normal
   route and player-offer behaviour.
4. **Orion economy and piracy:** Orion ships use the same market settlement;
   offensive and looter personalities can attack a player in the same
   unprotected sector, while defensive ships deploy persistent fighters.
5. **NPC combat/assets:** NPC combat and defensive deployment use the shared
   combat repository transaction primitives.  FedSpace remains excluded.
6. **ISS incidents:** attacks on assets explicitly registered in
   `federation_protected_assets` create durable, expiring security incidents
   outside sectors 1--10.  These incidents are the authoritative hand-off for
   delayed ISS response; they never duplicate Captain Z.
7. **Intelligence:** same-sector observations create expiring records in
   `npc_intelligence`, allowing later pursuit logic to use only information
   acquired by the NPC.
8. **Contracts and verification:** migrations, action keys, incident state and
   engine events are documented here and are covered by build, schema and
   runtime scenario tests.

The recurring jobs must not call BigBang or repopulate `msl_sectors`.  The
daily sweep only removes deployed assets from the already-generated MSL set.

## Verification

Apply migrations 162 and 163 to an isolated PostgreSQL database, build the
server, and run:

```sh
psql "$TEST_DATABASE_URL" -v ON_ERROR_STOP=1 \
  -f tests.v2/test_pg_v3_npc_federation.sql
python3 tests.v2/json_runner.py tests.v2/suite_v3_npc_federation_phases.json
```

The authenticated JSON suite must be run against a running server because the
NPC ticks, trade settlements, combat, and ISS pursuit are server-side effects.
