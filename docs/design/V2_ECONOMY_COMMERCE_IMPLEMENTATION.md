# V2 economy and commerce implementation contract

This document records the approved behavior for #247, #285, #297, and #288.
Issue #264 is explicitly deferred. This is PostgreSQL v2 scope; it does not
change MySQL or general database abstraction behavior.

## #247 Special-port hardware inventory

`hardware_items.price` remains the base price. A black-market port sells only
hardware with a configured `porttype_items.can_buy` row and a per-port stock
row. Inventory is persisted by `(port_id, hardware_items_id)`. A successful
purchase atomically debits credits, installs the hardware, and decrements
stock. A failed purchase changes neither credits, ship nor stock.

Daily restocking adds each row's configured restock quantity at its due time,
capped at maximum stock, then advances the due time by its configured
interval. Rows do not get implicit stock: game data explicitly chooses which
black-market ports sell which hardware and configures the quantities.

Stock modifies the displayed and charged unit price only at black markets. The
stock curve is bounded and preserves the base price at half stock:

```
effective_price = ceil(base_price * (1.5 - stock_quantity / max_stock))
```

This means empty stock is 1.5x base, half stock is base, and full stock is
0.5x base. `hardware.list` keeps its existing fields and `price` means the
effective price; an additive `base_price` field exposes the unchanged catalog
price. Stardock/Class-0 pricing and purchase behavior are unchanged.

## #285 Ferengi travelling traders

Ferengi identities are named persistent records tied to persistent ships;
their current sector is read from the ship row. `ferengi_trader_definitions`
is the data roster: each active row supplies a stable trader code, display
name, and ship type. The active definition row count is the trader count;
there is no separate count setting to drift out of sync.
`ferengi_trader_definition_rotation` stores the ordered, foreign-key-validated
commodity rotation. Initial cargo is defined in
`ferengi_trader_definition_cargo`. Provisioning creates a missing trader and
ship from those rows; it runs at startup and on the 15-minute trader
processing pass, so a new active definition can be added without a C source
change. A sysop tick forces the pass immediately. Marking a
definition inactive disables its existing trader on that provisioning pass.
Existing ships, location, visits, and identity remain in `ferengi_traders` and
`ships`.

The NPC tick first expires due offers, then checks each trader's current sector
for players, records an idempotent encounter for that trader visit, and creates
one fixed offer per player and visit. Offer generation walks the configured
commodity rotation, selling available stock up to 10 units. If the trader has
no stock in the rotation it creates a 5-unit buy offer, provided the faction
account can pay. Current prices remain fixed at 110% of base when selling and
90% when buying. The default offer lifetime is 21,600 seconds, controlled by
the existing integer config key `ferengi.offer_lifetime_seconds` (valid
configured range: 60 to 604,800 seconds). Offer terms are persisted and are
rechecked and settled once on acceptance; player cargo and credits are not
reserved when an offer is created. Rejecting or accepting is idempotent.

`ferengi_player_relationships` stores each player's reputation and encounter
count with each trader. `ferengi_trader_interactions` is the durable event
history, including encounter, acceptance, rejection, and expiry; idempotency
keys prevent a retry from applying reputation twice. Acceptance currently adds
5 reputation and rejection subtracts 1. Reputation is informational and does
not change today's fixed offer terms. Traders move one connected sector per
NPC visit and retain their same records when they return.

The player API is `ferengi.traders` (current-sector traders, reputation, and
their outstanding offers), `ferengi.deal.accept`, and
`ferengi.deal.reject`. A new offer emits `ferengi.trader.offer_v1` to the
player. The server does not reserve player funds or cargo when offering and
rechecks both sides at settlement. Offer generation uses the named trader's
persistent cargo and faction account; it does not create random merchant
identities.

Future pricing work can replace the fixed base-price multipliers at the offer
generation point in the repository while keeping persisted deal terms stable.
The current contract has no haggling, dynamic pricing, or diplomacy system.
Defaults for the roster, rotation, starting cargo, and six-hour lifetime are
seeded by PostgreSQL migration 113.

Implementation assumption: the repository's current Ferengi Alliance is the
faction owner for these traders. Named independent traders are individual
ship/identity records under that faction. Offer terms are fixed when created;
the server revalidates cargo, stock and funds at acceptance. NPC behavior
remains server-owned.

## #297 Temporary market shocks

A persisted shock names its source (`random`, `player`, or `npc`), optional
actor, optional commodity, optional port/sector scope, multiplier, start time,
and expiry. Null commodity means all commodities in scope; null location means
universe-wide. Exact-location shocks apply at that location; a sector shock
applies to ports in that sector. Multiple active shocks multiply in stable
ID order and the final multiplier is clamped to 0.25x–4x. The same common
quote/settlement price path applies shocks. Expired rows are ignored by reads
and removed by cron.

Player-triggered shocks use an explicit server command, require an authenticated
player and an eligible port in the player's current sector, and are recorded
with source `player`. This contract does not define a credit cost or cooldown;
the initial slice therefore applies a conservative server-configured
per-player daily limit and an auditable minimum/maximum duration. Random and
NPC sources use the same persisted row path.

## #288 Daily colony taxation

The day's taxable base is gross colony economic activity, recorded in a
planet activity ledger by production and completed planet market trades.
Internal ship/planet transfers are not economic activity. Each planet has an
enabled tax rate in basis points. At the daily tax task, tax is
`floor(taxable_value * rate_bps / 10000)` and is credited to the planet owner:
the player account for player-owned planets and the corporation account for
corporation-owned planets. A unique `(planet_id, tax_date)` assessment makes
retries idempotent. An activity row can only be assessed once. Missing/disabled
policy means a zero rate. Tax assessment is recorded even when rounded tax is
zero.

Assumptions needed to make the approved rule executable: activity is valued
using the server-confirmed commodity unit value at the time production/trade
completes; default policy is disabled; tax credits are owner revenue, not a
debit from the owner. These defaults preserve existing worlds until configured.

## Required schema additions

- `port_hardware_stock`: port/item stock, maximum, restock quantity/interval,
  and next restock time.
- `ferengi_traders`: stable trader key/name, faction, ship, travel visit and
  interaction timestamps; `ferengi_trader_deals`: trader/player offer
  lifecycle and settlement idempotency; `ferengi_player_relationships`:
  per-player reputation and encounter count; `ferengi_trader_interactions`:
  durable interaction history.
- `market_shocks`: trigger source/actor, scope, commodity, multiplier and
  start/expiry timestamps, with indexes for active lookup/expiry.
- `planet_tax_policies`, `planet_economic_activity`, and
  `planet_tax_assessments`: per-planet rate, append-only taxable activity,
  and one assessment per planet/day.
- No #264 schema change; that issue remains deferred.

These are additive PostgreSQL migration objects. No existing columns or
version envelopes are changed.

## Behavior regression coverage

- #247: mapped versus unmapped hardware, no-stock refusal, exact stock
  decrement, price curve endpoints/midpoint, insufficient funds rollback,
  restock due/not-due and capacity cap.
- #285: stable named trader identity and ship across ticks, travel persistence,
  one deal per idempotency key, acceptance revalidation, one-time settlement,
  reputation/history update and expiry.
- #297: random/player/NPC source persistence; commodity/location matching;
  combined multiplier clamp; quote and settlement parity; expiry cleanup.
- #288: production and market activity counted once, internal transfers
  excluded, player/corporation recipient routing, zero/disabled rates, daily
  idempotency and rounding.
- #264: no tests or implementation because it is deferred.

The checked-in integration suites require a disposable PostgreSQL-backed
server. SQL migration tests cover constraints, uniqueness and expiry indexes.
