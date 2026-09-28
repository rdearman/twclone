# V2 Gameplay Systems: Existing Seams and Contracts

This note records the current implementation seams for issues #439, #405,
#404, #403, #397, and #411. It is intended to keep new mechanics inside the
existing DB/server/engine boundaries.

## Existing capabilities

- Sector entry already calls `server_combat_apply_entry_hazards()` from warp
  and transwarp movement. Its current order is quasar, mines, fighters, then
  limpets. This is the appropriate resolver for environmental damage and
  defensive-grid reactions; a second movement hazard service would duplicate
  the existing path.
- `sectors.nebulae` contains generated descriptive sector names, not a reliable
  nebula flag. `sectors.navhaz` is an untyped level and has no entry effect.
  Environmental hazards now use `sector_hazards` and the shared entry
  resolver; see [ENGINE.md](ENGINE.md#810-environmental-sector-hazards).
- `sector_assets` persists sector fighters and mines. No cross-sector grid
  membership model was found. Grid membership must be durable and resolved
  from DB state, rather than cached as authoritative engine state.
- `h_npc_step()` is registered as an engine cron task and advances the existing
  ISS, Ferengi, and Orion systems. `h_handle_npc_encounters()` currently logs
  a generic event, but the search found no call site; `npc.encounter` is not a
  complete encounter flow.
- `h_planet_growth()` runs population growth and commodity stock production.
  Production uses the DB-backed `planet_production` rates. Production bonuses
  must adjust the calculation/rate, not repeatedly add to stored rates on each
  tick.
- Ship personality fields and Orion movement policies now exist through
  migration 111. No Psychic Probe action was found. Ship `probes` are existing
  inventory and are not themselves the Psychic Probe mechanic.

## Contracts for implementation

| Issue | Persistence and owner | Runtime contract | External dependency |
|---|---|---|---|
| #439 Special encounters | Encounter definitions, eligibility/cooldowns, and outcomes persist in DB. Engine owns scheduled/spawn selection; server owns the player-triggered movement boundary and result delivery. | Movement-triggered encounter selection must be deterministic under an injectable RNG and must not block movement on a non-encounter. Scheduled encounters run via `npc_step`. Resolve rewards/effects once, keyed by encounter/event ID. | Server command/event mapping and client message presentation need agreement before protocol changes. Existing `npc.encounter` logging alone does not deliver or resolve an encounter. |
| #405 Ship personalities | Implemented for Orion: ship override, corporation default, then ship-type default in DB. | Orion engine movement now resolves offensive, defensive, looter, blockader, and balanced policies. | Runtime scope is automated Orion movement; NPC combat stance/retreat policies and player-facing configuration commands remain future work. |
| #404 Sector hazards | Persist hazard type/intensity/config on sector or a normalized hazard relation. | Apply environmental effects once for each successful sector entry through the existing entry resolver. Define whether each hazard affects shields, hull, scanners, or movement before implementation; avoid applying twice on warp/transwarp. | Scan/route estimates need server response fields and client display, owned by other workers. |
| #403 Multi-sector grids | Persist grid identity, owner, member sectors, and asset membership/coverage. | On entry, resolve eligible connected grid defenses from DB and feed them through the existing defense resolver with stable ordering and one-use accounting. Membership changes become effective on the next resolution. | UI needs grid status/coverage fields; API owner must agree on the shape. |
| #397 Psychic Probe | Use existing ship probe inventory; persist action outcome/audit in `engine_events` or the established action log. | Server validates actor, active ship, probe stock, target, range, and cooldown; engine-side resolution consumes exactly one probe and returns success/failure plus revealed fields. | Requires a server protocol command and a client result view. Protocol handler is outside this ownership lane. |
| #411 Production and auto-defence | Persist corporation/rating/policy inputs and planet defence stock using existing planet production/defence state. | Planet growth computes effective production from base rate and bonus exactly once per tick. Defence is added as a capped quantity and consumed by the existing planet/sector combat path; retries must not duplicate output. | UI needs effective production and defence values. Server/API owns exposure. |

## Regression coverage required

1. **Hazards:** each hazard independently damages/applies its disruption once on
   warp and transwarp; no effect when remaining in-sector; destroyed ships do
   not receive later hazard effects in the entry sequence.
2. **Defensive grids:** linked sectors trigger only when membership/ownership
   permits, assets are consumed once, and unrelated sectors are unaffected.
3. **Encounters:** fixed RNG vectors cover no encounter, each encounter type,
   cooldown, duplicate event replay, reward application, and movement outcome.
4. **Personalities:** default/override precedence, explicit player action
   precedence, and stable choices for a fixed state/RNG vector.
5. **Psychic Probe:** insufficient stock, invalid target/range, success,
   failure, exactly-once resource consumption, and audit record.
6. **Planet bonuses/defence:** bonus arithmetic, caps, zero/negative inputs,
   repeated tick idempotency, and combat consumption.

Tests should use the existing isolated v2 DB test rig or a disposable DB. They
must not use a shared database. Pure selection/formula helpers should accept
explicit state and RNG inputs so these rules can also be tested without a DB.

## Current integration constraints

At the time this audit was written, the shared worktree has uncommitted edits
in `src/server_combat.c`, `src/server_universe.c`, `src/server_cron.c`, and
`src/db/repo/repo_cron.c`. Those are precisely the integration points for
hazards, movement encounters, NPC scheduling, production bonuses, and
planetary auto-defence. `src/server_s2s.c` and `src/schemas.c` are also dirty
and owned by the server/API workstream. Avoid editing these files until their
owners confirm the changes are integrated or the worktree is otherwise
coordinated.
