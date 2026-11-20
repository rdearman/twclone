# TWClone V3 gameplay specification

This is the authoritative source for approved V3 gameplay rules and project-specific numerical defaults. Detailed wire behavior remains in [`PROTOCOL.v3/`](PROTOCOL.v3/00_index.md). Historical TradeWars behavior is identified separately from TWClone decisions; a V3 rule is not a historical-parity claim unless its cited source establishes that behavior.

Current requirement classifications and executable acceptance evidence are maintained only in [`V3_PARITY.md`](V3_PARITY.md). Any status or verification column retained from an approval snapshot is historical context and does not supersede that document.

V3 covers the approved original-game play loop and explicitly approved TWClone additions. V4 StarPort ownership, freight exchange ownership, and other deferred systems are outside this specification unless explicitly listed below.


---

# Approved gameplay decision register

## Planet population, labour and capture

| ID | Subsystem | Approved behaviour / configuration | Implementation references | Protocol references |
| --- | --- | --- | --- | --- |
| POP-01 | Population | A planet is `COLONIST`, `SLAVE`, or `UNPOPULATED`; never mixed. Preserve counts and allocations. Existing populated planets migrate as colonists; empty planets remain unpopulated. | Migrations `171_v3_planet_population_state.sql`, `123_v3_planet_population_state.sql`; `src/server_planets.c`; `src/db/repo/repo_planets.c` | `24_Planets_Outposts_Stations.md` |
| POP-02 | Acquisition | Evil players may buy slaves or choose enslavement on successful conquest. Neutral may inherit but cannot buy/enslave. Good cannot retain slaves after qualifying capture. Never create remote planetary population without safe transport. | `src/server_ports.c`, `src/server_planets.c::cmd_planet_colonists_set`, `src/db/repo/repo_planets.c::repo_planets_deposit_slaves`, `cmd_combat_attack_planet` | `22_Trade_and_Port_Commands.md`, `23_Combat_and_Weapons.md`, `24_Planets_Outposts_Stations.md` |
| POP-03 | Slave productivity | Ore, organics and equipment output is `floor(normal_output * 50 / 100)`, once after normal authoritative calculation; no second morale penalty. | `src/slave_population.h::slave_population_effective_output`, `src/db/repo/repo_cron.c` | `24_Planets_Outposts_Stations.md` |
| POP-04 | Morale | Slave morale is permanently low and cannot improve. No additional continuous output modifier. Preserve existing colonist rules; invent no dynamic colonist morale. | `src/server_planets.c` | `24_Planets_Outposts_Stations.md` |
| POP-05 | Unrest | Daily recurrence every 24 game-hours; one production-cycle stoppage; loss of 1% of slave population per qualifying daily loss event, using integer-floor accounting. Deterministic, persisted, restart/idempotency-safe. Production-cycle rules remain +2 unrest with slave labour, −1 without, bounded 0–100, disruption chance `floor(unrest/10)%` capped at 10%. May not change ownership, destroy planets, liberate or convert population. Loss is allocated proportionally across assignment buckets by deterministic largest-remainder allocation. | Migrations `192_v3_approved_slave_unrest_rules.sql`, `171_v3_planet_population_state.sql`, `190_v3_slave_unrest_production_cycles.sql`; `src/server_config.c`, `src/server_sysop.c`, `src/db/repo/repo_cron.c`, `src/server_engine.c` | `24_Planets_Outposts_Stations.md`, `29_SysOp_Commands.md` |
| POP-06 | Emancipation | Owner or authorized corporation management (Leader/Officer) converts all slaves to colonists atomically, preserving population and allocations; no turn/credit cost; no partial conversion. | `src/server_planets.c`, `src/db/repo/repo_planets.c`, Python and Godot landed-planet menus | `24_Planets_Outposts_Stations.md` |
| POP-07 | Capture conversion | Capture requires defeated defenses and successful invasion. Evil explicitly chooses retain/enslave colonists; good automatically liberates slaves; neutral retains inherited slaves; evil may retain them. Planet resources/treasury remain with captured planet. No corporation-capture rule is inferred. | `src/server_planets.c::cmd_combat_attack_planet`, `src/db/repo/repo_planets.c::db_planets_capture` | `23_Combat_and_Weapons.md`, `24_Planets_Outposts_Stations.md` |
| HARV-01 | Harvest capacity | `planet.harvest` transfers accumulated ore/organics/equipment only to currently landed ship, capped to available holds; remainder stays on planet; zero turns; no generation; atomic. | `src/server_planets.c::cmd_planet_harvest`, `src/db/repo/repo_planets.c::repo_planets_harvest` | `24_Planets_Outposts_Stations.md` |
| HARV-02 | Harvest permission | Use existing authoritative ownership/corporation permissions; no unrestricted withdrawal. | `src/db/repo/repo_planets.c::repo_planets_harvest`, `cmd_planet_withdraw` | `24_Planets_Outposts_Stations.md` |
| HARV-03 | Exchange/harvest | Planet surplus may be sold directly to Exchange; sale immediately removes available planetary stock, so sold goods cannot later be harvested. | `src/server_planets.c`, `src/server_cron.c`, `src/db/repo/repo_market.c` | `22_Trade_and_Port_Commands.md`; Galactic Exchange contract |

## Galactic Exchange and economy

| ID | Subsystem | Approved behaviour / configuration | Implementation references | Protocol references |
| --- | --- | --- | --- | --- |
| EXC-01 | Participants | Planets buy/sell Exchange goods; ports buy from Exchange; preserve compatible port, order and NPC trade paths. | `src/server_planets.c`, `src/server_cron.c`, `src/db/repo/repo_market.c` | `22_Trade_and_Port_Commands.md` |
| EXC-02 | Settlement | Validate source stock and destination funds/capacity, transfer inventory and credits, record durable transaction atomically; completed goods arrive immediately. | `src/server_planets.c`, `src/server_cron.c`, `src/db/repo/repo_market.c`, `src/server_bank.c` | `22_Trade_and_Port_Commands.md` |
| EXC-03 | Ring-fencing | Sold goods leave seller ownership and cannot be harvested, resold, built with, consumed or counted twice. | `src/db/repo/repo_market.c`, market migrations | Galactic Exchange contract |
| EXC-04 | Virtual transport | Deliveries are immediate economic transfers; no physical NPC ships, movement, fuel, holds, docking or delay. | `src/server_cron.c` | Galactic Exchange contract |
| EXC-05 | Orders | Keep placement, reservation, execution and settlement distinct. Open orders wait for matches; partial fills preserve remainder; no duplicate settlement. | `src/server_cron.c`, `src/db/repo/repo_market.c` | `22_Trade_and_Port_Commands.md` |
| EXC-06 | Port demand | Port demand follows current target/pricing/capacity rules; ports purchase Exchange goods to replenish. Do not bypass Exchange with artificial supply unless a separate source is documented. | `src/server_cron.c` | `22_Trade_and_Port_Commands.md` |
| EXC-07 | Conservation | Every trade has a source and destination; seller decrease equals exchange increase or buyer increase; no trade creates/destroys goods. Production/consumption are separate. | `src/db/repo/repo_market.c`, `src/server_cron.c` | Galactic Exchange contract |
| EXC-08 | Finance | Atomic credits, durable records, retry/idempotency/concurrency safety and no stale/cancelled settlement. A virtual clearing account may receive explicit, configurable game-engine liquidity injections; the game need not be financially self-sufficient. Player assets/transactions remain protected. | `src/db/repo/repo_market.c`, `src/server_cron.c`, `src/server_bank.c`, `src/server_config.c`, `src/server_sysop.c` | Galactic Exchange contract, `26_Banking_and_Ledger.md` |
| EXC-09 | Game-economy principle | Favor enjoyable, sustainable gameplay over real-world financial realism. Explicit/configurable engine injections must remain distinguishable from ordinary player transactions. Evaluate accounting invariants over 30 simulated game days. | `src/server_cron.c`, `src/server_config.c`, `src/server_sysop.c`; `tests.v2/manual_acceptance/economy_30_day_simulation.json` | Galactic Exchange contract, `29_SysOp_Commands.md` |
| SHIP-05 | Used-hull resale | Used hull is acquired through existing shipyard workflow at floor((original hull value + retained equipment value) * configured percentage), default 80%; preserve hull identity/equipment and do not create a new hull. Standalone ship-sale payout remains 60%. | `src/server_stardock.c`, `src/db/repo/repo_stardock.c`, migrations 170/177 | `19_Ship_Commands.md`, `29_SysOp_Commands.md` |
| INS-02 | Guaranteed insurance funding | Every qualifying hull claim pays the covered replacement value directly to the player's bank regardless of insurer reserves. Explicit system credit creation/virtual insurer funding is allowed and must be identifiable. Exactly once; cargo/equipment excluded; escape pod independent. | `src/db/repo/repo_ships.c`, migrations 174–175, 179 | `23_Combat_and_Weapons.md`, `26_Banking_and_Ledger.md` |
| TRADE-01 | Pricing | No haggling. Existing fixed-at-quote authoritative price and configured dynamic supply/demand/black-market pricing remain. | `src/server_ports.c`, `src/db/repo/repo_ports.c` | `22_Trade_and_Port_Commands.md` |

## Banking and player visibility

| ID | Subsystem | Approved behaviour / configuration | Implementation references | Protocol references |
| --- | --- | --- | --- | --- |
| BANK-01 | Standing-order schedule | Daily, weekly, monthly at specified UTC time; any existing player may be recipient. ISO weekday numbering is used on the wire; monthly day is retained as configured. | `src/server_bank.c::bank_order_first_run`, `db_bank_process_orders`, `src/server_engine.c::h_bank_orders_tick`; migrations 172/124 | `26_Banking_and_Ledger.md` |
| BANK-02 | Insufficient funds | Skip that occurrence without debt or same-occurrence retry; next occurrence remains active. | `src/server_bank.c::db_bank_process_orders` | `26_Banking_and_Ledger.md` |
| BANK-03 | Monthly edge | If configured day is absent, run last day of month and retain original configured day. Gregorian leap-year rules apply. | `src/bank_schedule.h::bank_schedule_monthly_at`, `src/server_bank.c::bank_order_first_run`, `db_bank_process_orders` | `26_Banking_and_Ledger.md` |
| BANK-04 | Invalid recipient | If recipient no longer exists, cancel order automatically; completed transfers remain final. | `src/server_bank.c::db_bank_process_orders` | `26_Banking_and_Ledger.md` |
| BANK-05 | Settlement | Create/list/cancel; durable exact-once occurrence, atomic bank debit/credit/ledger; cancellation serializes with execution; no arbitrary order cap. | `src/server_bank.c`, migrations 172/124, `src/server_engine.c::h_bank_orders_tick` | `26_Banking_and_Ledger.md` |
| PLAYER-01 | Online list | 1-based page/limit; deduplicate authenticated players before pagination; deterministic ordering; return only ID, username, corporation (null if none), alignment. | `src/server_players.c`, `src/schemas.c`, Python/Godot Comms menus | `20_Player_Commands.md` |
| PLANET-01 | Overview | `planet.view` read-only; `planet.info` detailed management; appropriate population/production/morale; treasury only owner/authorized corporation member. | `src/server_planets.c::cmd_planet_view`, `src/db/repo/repo_cron.c::db_cron_planet_get_production_overview_json`, Python/Godot surface menus | `24_Planets_Outposts_Stations.md` |

## NPC population and Federation jurisdiction

| ID | Subsystem | Approved behaviour / configuration | Implementation references | Protocol references |
| --- | --- | --- | --- | --- |
| NPC-POP-01 | Ferengi population | Active target is 2% of universe sectors, minimum 3; canonical roster has at least 250 stable, individually named identities; reconciliation is idempotent and preserves established NPC state. | `src/db/repo/repo_cron.c`, Ferengi seed migrations | `27_NPC_and_Ferengi_AI.md` |
| NPC-POP-02 | Orion population | Active target is 0.5% of sectors, minimum 5; canonical roster has at least 100 distinct stable names; existing personalities and ship classes remain. | `src/db/repo/repo_cron.c`, Orion seed migrations | `27_NPC_and_Ferengi_AI.md` |
| NPC-POP-03 | Ferengi fleet | Five NPC-only classes: Trade Master/Cruiser/Interceptor/Warship/Dreadnought; allocation 40/30/16/10/4 percent with deterministic counts summing exactly to target population; retain existing Warship values. | Ship-type migrations and Ferengi reconciler | NPC protocol and ship catalogue |
| FED-01 | Captain Z | Overt aggression in sectors 1–10 triggers immediate Captain Z intervention, destroying attacker ship and podding player; no travel delay or duplicate ISS dispatch. | `src/server_combat.c` | `23_Combat_and_Weapons.md` |
| FED-02 | MSL | BigBang-generated `msl_sectors` is authoritative and unchanged during game; outside sectors 1–10, deployable assets are cleared by the once-daily UTC Federation sweep. Geography comes from generated warp topology, never sector numbering. | BigBang MSL generation, `src/server_cron.c` | `23_Combat_and_Weapons.md`, `29_SysOp_Commands.md` |
| FED-03 | ISS | ISS is distinct from Captain Z and may act under explicit Federation jurisdiction/protected-asset rules; Captain Z violations do not also dispatch ISS. ISS pursues active protected-asset incidents using movement and shared NPC combat. This is a TWClone V3 contract; historical TradeWars parity is not claimed solely from this behavior. | `src/server_combat.c`, `src/server_engine.c`, `src/server_universe.c::iss_patrol_step`, security repositories | `27_NPC_and_Ferengi_AI.md` |
| NPC-FER-01 | Ferengi retaliation and destruction bounty | Attacking or destroying a Ferengi places offender on the Ferengi Empire wanted list with a bounty; the most powerful Ferengi captain hunts that offender. Separately, an eligible player destroying a Ferengi NPC ship earns 5,000 system-generated gameplay credits once per destruction. | `src/server_combat.c::handle_ship_attack`, `src/server_universe.c::ferengi_handle_hostile_attack`, `ferengi_destruction_rewards`, Ferengi scheduler | `27_NPC_and_Ferengi_AI.md`, `V3_GAMEPLAY_SPEC.md` |
| NPC-ORI-01 | Orion blockade | Orion NPCs attack eligible non-allied players entering an Orion-controlled sector through supported travel, subject to existing safe-zone and combat protections; preserve existing NPC movement, cooldown and combat settlement. | `src/server_universe.c::ori_move_all_ships`, `repo_universe_get_orion_ships`, Orion deployment/combat repositories | `27_NPC_and_Ferengi_AI.md` |

## Ships, combat, movement and insurance

| ID | Subsystem | Approved behaviour / configuration | Implementation references | Protocol references |
| --- | --- | --- | --- | --- |
| SHIP-01 | Sale locations | Sell at StarDock, authorized shipyards and evil/Orion shipyards using DB facility classifications and existing access rules. | `src/server_ships.c`, `src/server_stardock.c`, `src/db/repo/repo_stardock.c` | `19_Ship_Commands.md` |
| SHIP-02 | Valuation | `floor((hull base + installed equipment) * percentage)`; default 60%, integer basis points. Cargo excluded. Use classic TradeWars 2002/TWGS 3.13 reference prices for Armid mines (1000), Limpets (10000), Ether probes (3000), photons (40000), and beacons (100) credits. | `src/server_config.c`, `src/db/repo/repo_stardock.c`, `hardware_items`; migrations `178_ship_sale_equipment_catalogue.sql` / MySQL `130_ship_sale_equipment_catalogue.sql` | `19_Ship_Commands.md`, `29_SysOp_Commands.md`, [`V3_EQUIPMENT_CATALOGUE.md`](V3_EQUIPMENT_CATALOGUE.md) |
| SHIP-03 | Sale effects | Cargo forfeited, equipment accompanies hull, insurance ends without refund, persistent used inventory, atomic credit/ownership/audit. | `src/db/repo/repo_stardock.c`, migrations 170/177 | `19_Ship_Commands.md` |
| SHIP-04 | Active/last ship | Cannot sell last eligible ship. Selling active hull requires explicit eligible replacement; switch and sale atomic. | `src/server_ships.c`, `src/db/repo/repo_stardock.c` | `19_Ship_Commands.md` |
| COMBAT-01 | Destruction | Every player ship destruction yields escape-pod respawn, independent of installed pod; no starter ship. Cargo/equipment lost; bank, planets and corporate assets persist; exactly one valid active ship; idempotent loss. Terra enforcement is not an exception and must not seize unrelated ships, credits, bank balances, planets or faction assets. | `src/db/repo/repo_ships.c::repo_ships_handle_destruction`, `src/server_ships.c::handle_ship_destruction`, player/NPC combat and engine callers, `cmd_combat_attack_planet` | `23_Combat_and_Weapons.md` |
| COMBAT-02 | Capture | Defeated defenses plus successful invasion; planet resources and entire treasury stay with transferred planet; personal/corporate unrelated assets remain. | `src/server_combat.c`, `src/db/repo/repo_planets.c` | `23_Combat_and_Weapons.md` |
| INS-01 | Insurance | Only ship hulls may be insured. Coverage is hull replacement only; qualifying destruction payout is guaranteed to the player's bank; no cargo/equipment cover, direct ship replacement or duplicate payout; voluntary sale terminates coverage without refund. | `src/schemas.c::schema_insurance_policies_buy`, `src/server_loop.c::cmd_insurance_policies_buy`, `src/db/repo/repo_ships.c::repo_ships_insurance_buy_policy`, `settle_destruction_insurance` | `26_Banking_and_Ledger.md`, `23_Combat_and_Weapons.md` |
| INS-03 | Underinsured policy settlement | The policy's configured payout may be below hull replacement value; pay that configured amount, subject to the aggregate hull replacement cap. A separate global valuation setting is not required. | `src/db/repo/repo_ships.c::settle_destruction_insurance` | `26_Banking_and_Ledger.md` |
| MOVE-03 | Interruption trigger policy | Interrupted transwarp is excluded from V3 acceptance unless a canonical trigger is subsequently established. No arbitrary random interruption. | `src/server_universe.c::cmd_move_transwarp` | `21_Sector_and_Movement_Commands.md` |
| EQUITY-01 | Corporate equity scope | Corporate stock-market functions are an expansion, not original TW2002 parity; exclude them from V3 implementation and release acceptance. Existing explicit `NOT_IMPLEMENTED` responses may remain. | `src/server_corporation.c::cmd_equity`, `src/server_loop.c` equity registrations | `25_Corporations_and_Stock_Market.md`, `V3_PARITY.md` |
| MOVE-01 | Atomic synchronous transwarp | Eligibility, fuel and destination checks precede movement. Accepted movement completes at the destination; destination-entry hazards resolve after arrival in the same transaction. Mid-flight interruption is absent and excluded from V3 acceptance unless a canonical trigger is later established. | `src/server_universe.c::cmd_move_transwarp`, `repo_universe_move_player_and_ship`, shared entry-hazard resolver | `21_Sector_and_Movement_Commands.md` |
| MOVE-04 | No V3 mid-flight interruption | Transwarp is synchronous and atomic; no interruption event is defined in V3. Future acceptance requires a canonical trigger, not arbitrary randomness. | `src/server_universe.c::cmd_move_transwarp` | `21_Sector_and_Movement_Commands.md` |
| MOVE-02 | Entry hazards | Apply hazards immediately on normal/transwarp sector entry; persist damage/destruction, trigger pod lifecycle; no disconnect/prompt bypass. | `src/server_universe.c`, `src/server_combat.c` | `21_Sector_and_Movement_Commands.md`, `23_Combat_and_Weapons.md` |

## Planetary trade agreements and StarPorts

| ID | Subsystem | Approved behaviour / configuration | Implementation references | Protocol references |
| --- | --- | --- | --- | --- |
| PTA-V3-01 | Planetary Trade Agreement | A player with ownership or existing trading authority may bulk-sell planetary ORE/ORG/EQU to a StarPort in the same sector. The port must buy each commodity. Apply authoritative dynamic pricing, stock, capacity, funding and existing access rules. Direct port settlement is separate from Galactic Exchange `planet.market.*`; no ship/cargo is required; zero turns; no port ownership model. | `src/server_planets.c::cmd_planet_trade_port`, `src/db/repo/repo_ports.c`, `src/db/repo/repo_planets.c` | `22_Trade_and_Port_Commands.md`, `24_Planets_Outposts_Stations.md` |
| STARPORT-V3-01 | StarPort construction/lifecycle | V3 parity includes constructing a port in an eligible sector, selecting its trading class at construction, upgrading commodity trading capacity and destroying ports under original TradeWars mechanics. The eight buy/sell combinations are specified; class selection is only at construction, not reclassification. At most one active port per sector. Destroyed port records are retained as inactive history because trade/audit tables hold port foreign keys; a replacement port may use the sector. No persistent player/corporation port ownership is introduced. | `sql/pg/000_tables.sql`, `sql/my/000_tables.sql`, migrations `180_one_port_per_sector.sql` and `181_active_starport_sector_uniqueness.sql`, port-generation SQL | `22_Trade_and_Port_Commands.md`, `24_Planets_Outposts_Stations.md` |
| STARPORT-V4-01 | Port ownership (V4) | V4 may add explicit owner type/ID for Galactic Exchange institution, player, corporation and optionally unowned ports; acquisition/construction/control; owner-configured commodity/tariff/access rules; preferential same-owner internal transfers. Ordinary commercial PTA remains available between independently owned assets. Ownership changes preserve inventory, outstanding trades and liabilities. | No V3 implementation | Future V4 contract |

## Additional original-gameplay findings

| ID | Subsystem | Approved behaviour / configuration | Implementation references | Protocol references |
| --- | --- | --- | --- | --- |
| PARITY-PORT-ATTACK-01 | Port attack | V3 names `combat.attack.port` and `player.port_strike.v1`; `ports.size` is capacity, not hull strength. | `src/server_loop.c`, `src/server_combat.c`, `sql/pg/000_tables.sql::ports` | `23_Combat_and_Weapons.md`, `08_Engine_Event_Contract.md` |
| PARITY-PHOTON-01 | Photon missiles | Eligible ship classes, adjacent sector firing at required `target_ship_id`, shielded direct-impact block while consuming one photon and applying independent sector suppression, unshielded persisted combat disablement through configured expiry, no stacking and max-expiry refresh, prohibition on disabled-ship attacks/weapons, navigation/retreat/non-combat availability, atomic ammunition/direct-effect/sector-effect/receipt settlement, idempotency, cooldown and configured expiry. | `cmd_combat_fire_photon`; combat entry-point guards; persisted `ships.combat_disabled_until`, sector effect/request tables; Python and Godot workflows; hazard/interdiction/citadel reaction checks | `23_Combat_and_Weapons.md`, `19_Ship_Commands.md`; [manual](https://electraphysics.com/documents/tradewarsmanual.pdf), photon section. Direct ship disablement semantics are approved TWClone V3 behavior, not a historical parity claim. |
| PARITY-TOLL-01 | Toll fighters | Original manual: toll fighters stop non-hostile passers, request payment based on deployed quantity, and fight if attacked. | `move_warp_toll_offer`, normal/transwarp settlement, `apply_sector_fighters_on_entry`, Python/Godot movement flows; migration 184 | `21_Sector_and_Movement_Commands.md`, `23_Combat_and_Weapons.md`; [manual](https://electraphysics.com/documents/tradewarsmanual.pdf), pp. 8–9 |
| PARITY-TW-HOMING-01 | Transwarp homing fighter | A deployed destination-sector fighter is the homing reference; validate and lock it inside the movement transaction before turn/location mutations. | `src/server_universe.c::cmd_move_transwarp`, `src/db/repo/repo_universe.c::repo_universe_lock_sector_fighter` | `21_Sector_and_Movement_Commands.md` |
| MOVE-05 | Transwarp fuel quantity | TWGS 3.13 baseline: 3 Fuel Ore per sector of distance, measured as the shortest directed warp-hop count in the authoritative universe graph. | `src/server_universe.c::cmd_move_transwarp`, `src/db/repo/repo_universe.c::repo_universe_get_transwarp_distance`, `src/db/repo/repo_cargo.c::repo_cargo_consume_locked` | `21_Sector_and_Movement_Commands.md` |

## Explicit noncanonical V3 defaults

The product owner approved completing these mechanics and authorized deterministic
twclone V3 defaults wherever the checked TradeWars 2002 manual and existing
decision register do not establish numbers. The values below are **not
canonical TWGS 3.13 values**. They must be configurable where identified and
must not be described as historical parity. Existing approved behavior above
continues to take precedence.

| ID | Mechanic | V3 default | Rationale |
|---|---|---|---|
| V3-PLANET-RATE-01 | Commodity production | Per 10-minute production tick, workers required per unit (ORE/ORG/EQU): M 100/100/100; L 50/100/100; O 100/50/100; K 50/100/100; H 50/100/50; C 100/100/100; U disabled. Integer remainder carries per planet and commodity. | Deterministic class differentiation with simple integer settlement. Noncanonical; versioned in migration 182 and configurable by editing class-rate rows. Worker assignment and the approved 50% slave-output multiplier apply before remainder settlement. Fighter production remains governed by its existing contract. |
| V3-TOLL-RATE-01 | Toll amount | Default 1 credit per deployed toll fighter, charged once per attempted sector entry; configurable integer credits per fighter, minimum 0. | Simple deterministic interpretation of the manual's quantity-dependent toll. Noncanonical; zero disables collection without changing toll-fighter combat response. |
| V3-PHOTON-DURATION-01 | Photon suppression | Default 600 seconds from successful launch; configurable duration in seconds, minimum 1. | One production interval gives a bounded, reproducible effect and uses the existing 10-minute world tick cadence. Noncanonical; persisted UTC expiry controls effect and restart recovery. |
| V3-PORT-BUILD-01 | Construction and upgrades | Initial construction: 100,000 credits plus 1,000 each ORE/ORG/EQU; three daily deliveries of 100 each commodity (1,300 total of each material); class-specific exchange directions fixed at completion. Capacity upgrades: 10,000 credits and 100 units of the selected commodity per level, increasing that commodity's capacity by 1,000 per level, with maximum level 10. The construction planet supplies materials; the upgrading ship supplies the selected commodity and the active port receives the credit cost. | Explicit modest costs and a short, deterministic construction schedule. Noncanonical; costs/timing/capacity limit are versioned configuration data and are not TWGS 3.13 claims. |
| V3-PORT-COMBAT-01 | Port defence/durability | 10,000 durability; 1,000 defensive strength; each attack consumes one turn and deals `max(1, shiptypes.maxattack - 1,000)` durability damage; retaliation deals 1,000 damage in the existing shield→fighter→hull order. Remaining commodity stock is destroyed on port destruction. FedSpace Captain Z enforcement applies. | Deterministic, separately modelled capacity/defence/durability; no salvage. Durability, defence and attack cost are versioned noncanonical V3 defaults; damage order and FedSpace enforcement reuse existing approved combat rules. Preserve an inactive port tombstone and settle attack, retaliation, stock clearing and destruction event atomically. |

These defaults supersede only unresolved numeric details for those mechanics. They do not alter the approved fighter-production contract, planet-population rules, port-class directions, shield/citadel restrictions, one-active-port invariant, or inactive-history policy.

## Approved Photon ship-targeting semantics

`combat.fire_photon` requires an explicit `target_ship_id` in an eligible
adjacent sector. These six approved TWClone V3 rules govern direct impact and
sector suppression:

1. A shielded target consumes one photon and receives sector suppression, but
   receives no direct combat disablement.
2. An unshielded target's combat systems are disabled until the shot's
   configured expiry.
3. A disabled ship cannot initiate attacks or fire weapons through any combat
   entry point. Navigation, retreat, status and non-combat operations remain
   available.
4. Repeated hits set expiry to `max(existing_expiry, new_expiry)` and never
   shorten an existing disablement.
5. Ammunition, direct impact, sector suppression and the idempotency receipt
   commit or roll back together. Invalid targets, insufficient ammunition and
   non-adjacent sectors produce no partial effects.
6. Disablement persists across reconnect and server restart. Expired effects
   cease to apply by timestamp comparison and need no cleanup job.

Sector suppression remains independent of direct ship impact and uses the
configured duration. Existing planetary-shield behavior remains in force.
Direct ship disablement is an approved TWClone V3 rule, not a claim about
canonical historical TradeWars behavior. See [`V3_PARITY.md`](V3_PARITY.md) for
executable evidence.


## SysOp session invalidation

| ID | Subsystem | Approved behaviour / configuration | Implementation references | Protocol references |
| --- | --- | --- | --- | --- |
| SYS-07 | Player kick | `sysop.player.kick` must invalidate every durable session for the target and disconnect active connections; a subsequent request using an old token must be unauthenticated. | `src/server_sysop.c::cmd_sysop_player_kick`, `src/db/repo/repo_session.c::repo_session_revoke_player_sessions`, `src/server_loop.c::process_message` | `29_SysOp_Commands.md` |

## Configuration

| ID | Subsystem | Approved configuration | Source / protocol |
| --- | --- | --- | --- |
| CFG-01 | Ship sale | `shipyard_sale_factor_bp`, integer 0–10000, default 6000; distinct from trade-in. | `src/server_config.c`; `29_SysOp_Commands.md` |
| CFG-02 | Slave output | Fixed 50%; not an adjustable setting under current policy. | `src/db/repo/repo_cron.c`; `24_Planets_Outposts_Stations.md` |
| CFG-03 | Slave unrest | Approved POP-05 configuration is daily recurrence every 24 game-hours, one production-cycle stoppage and 1% integer-floor slave population loss per qualifying event. Production-cycle unrest remains separately governed by ruleset 1.2. | `src/server_config.c`, `src/server_sysop.c`, migration 192, protocol 29 |
| CFG-04 | Insurance | Existing eligibility and coverage parameters retained; hull-only payout to bank. | Insurance config/repository; `26_Banking_and_Ledger.md` |

---

---

## Ruleset 1.2 — approved mechanics

These rule descriptions incorporate the approved 19 November 2026 project-specific mechanics. Historical implementation/status statements from source addenda remain in Git history; current requirement verification is recorded only in `V3_PARITY.md`.

## Slave unrest
These values govern the separate production-cycle unrest mechanic:

| Parameter | Approved V3 value |
|---|---:|
| Range | 0–100 |
| Eligible cycle using slave labor | +2 unrest |
| Eligible cycle without slave labor | −1 unrest |
| Disruption chance | `min(floor(unrest / 10), 10)%` |
| Disruption effect | Skip that cycle's production; no population loss from the disruption |

Unrest is clamped after its cycle transition and the probability is evaluated
from the resulting unrest level. The roll is deterministic for the planet and
production tick so retrying that tick cannot draw a new result.

This does not replace or weaken POP-05. POP-05 remains the separate daily,
persisted stoppage, recovery and configured population-loss mechanism. A
persistent POP-05 stoppage suppresses slave production; it is not converted
into a per-cycle disruption and does not change the cycle unrest level while no
eligible slave-production cycle occurs. The cycle disruption itself neither
changes population nor consumes production remainders.

Compatibility finding: **no direct conflict** between the numeric cycle rules
and POP-05 when the two consequences remain separate as above. Existing POP-05
stoppage recovery and possible population loss remain authoritative.

The qualifying POP-05 loss event retains the existing daily scheduler trigger:
it is due for a `SLAVE` planet with positive allocated slave population while
the daily policy is enabled. The event is recorded even if integer-floor 1%
loss rounds to zero people. An empty slave allocation advances its next
evaluation time without recording a stoppage or population-loss event. This
documents the existing trigger; it does not add another unrest scheduler.

## Ferengi destruction reward
An eligible player receives exactly 5,000 gameplay credits for destroying a
genuine Ferengi NPC ship. This is a **system-generated V3 economic source**.
The reward is recorded with its recipient and source in
`ferengi_destruction_rewards` and is settled atomically with ship destruction.
Its once-only key is the NPC ship's destruction sequence, so a retry cannot pay
twice and a later respawn followed by a new destruction has a distinct event.
Player-owned ships in a Ferengi corporation do not qualify.

This reward is separate from the existing Ferengi bounty penalty/wanted-list
mechanism. It does not replace that penalty or fund it. The reward credits the
player's gameplay balance; no treasury debit is implied.

## Orion targeting
Orion attacks remain subject to safe-zone protection and established
combat/ownership rules. Sectors 1–10 are excluded before an attack action is
claimed. Ships owned by Orion corporation members are treated as allied and are
excluded for every Orion combat personality. The scheduler processes each
eligible player in a sector rather than stopping after the first target.

These protections do not create a general player immunity or a new alliance
system. The current project contract only supplies the existing Orion
corporation relationship and FedSpace safe-zone protection.

## Economy acceptance horizon
The approved horizon is 30 simulated game days. The hidden SysOp acceptance
command `sys.cron.advance_to` dispatches the actual registered cron handlers and
their existing rescheduling logic at a deterministic QA epoch. It is gated by
`test.deterministic_scheduler_enabled=1`; the engine's wall-clock cron dispatch
is paused while this isolated test switch is on. It is not a gameplay feature
or a balance rule.

Simulation telemetry can demonstrate accounting/state invariants and reveal
sources or sinks. The 30-day PostgreSQL simulation passed 64/64 cases using the
actual scheduler dispatcher. It captured daily player/corporation/bank credits,
planet and port stock, population, production remainders, reward counts,
bank/corporation flows by mechanism and planetary production totals. Daily
checks found no negative balances, stock or population and no hardware stock
above capacity. This establishes the tested invariants for this fixture, not
economy-wide conservation or balance. There is still no approved quantitative
balance criterion; no rates or prices were changed.

The black-market hardware regression passed 34/34 against disposable
PostgreSQL. Its restock deadline is advanced through the same registered
scheduler dispatcher, preserving the exact stock assertion without a
wall-clock wait.

---

# Gameplay scope and evidence policy

## Authority and scope

1. [`PROTOCOL.v3/00_index.md`](PROTOCOL.v3/00_index.md) defines the published
   client/server and server/engine contracts.
2. [`V3_GAMEPLAY_SPEC.md`](V3_GAMEPLAY_SPEC.md) records approved
   gameplay rules, configured noncanonical defaults and unresolved decisions.
3. The [`parity ledger`](V3_PARITY.md) records current classifications and
   links executable acceptance evidence. Source code or unit tests alone do not
   establish playable parity.

V3 targets the approved original-game player loop. V4 StarPort ownership,
freight exchanges and other expressly deferred expansions are not V3 parity
requirements. The exact original executable/configuration has not been pinned;
the published TradeWars manual is a discovery reference and cannot override
approved V3 decisions.

## Gameplay areas

| Area | V3 scope | Verification approach |
|---|---|---|
| Player and navigation | Registration, persistence, movement, scans, routes, avoids and turns | Ordinary-player client journey plus authoritative state checks |
| Trading and economy | Port modes, cargo/credit/stock settlement, equipment and scheduled economy | Client workflow, transaction accounting, retries and restart checks |
| Ships and combat | Acquisition, repair, transfers, weapons, sector defenses, destruction and recovery | Two-player client journey with damage, resource and recovery assertions |
| Planets | Landing, colonists, production, harvest, citadels and planetary combat | Ownership/access checks, production accounting and restart checks |
| Corporations | Membership, permissions, treasury and authorized shared transfers | Multiple ordinary accounts and conservation assertions |
| StarPorts | Construction, deliveries, capacity upgrades, attack and destruction | Concurrency, idempotent replay, rollback and history/stock assertions |
| Feedback | Authenticated reports, safe diagnostics and durable local queue | Redaction, ownership, idempotency and mock publisher tests |

Current classifications and acceptance evidence are in
[`V3_PARITY.md`](V3_PARITY.md).

## Known scope boundaries

- StarPort class, construction, upgrade, destruction and noncanonical defaults
  follow the approved V3 decisions; do not infer V4 ownership or salvage.
- Photon and toll-fighter numeric defaults are configurable and explicitly
  noncanonical unless a cited source establishes otherwise.
- Planet production ratios are versioned configuration. Preserve the approved
  worker allocation and citadel-gated fighter-production rules.
- Full MySQL game-server runtime is not supported by the current evidence.
- Passing client and server regression suites does not clear external
  deployment; consult the release-candidate audit.
