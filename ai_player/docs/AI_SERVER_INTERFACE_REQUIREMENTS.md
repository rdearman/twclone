# AI server interface requirements

**Status:** requirements for future server/protocol work. This document does not
define or change a wire contract. Server/API ownership must review and publish
the canonical command and event schemas before AI implementation begins.

The AI should remain a client of authoritative game state. It must not invent
encounters, production, inventory, or outcomes locally. Proposed field names
below describe the information needed and are examples, not commitments to a
particular schema.

## 1. Random encounters — issue #439

The engine/server must create the encounter and decide its authoritative
outcome. The AI can only choose among actions the server explicitly offers.

### Incoming event

Deliver a versioned event on the authenticated player's stream with:

- Stable `event_id`, `event_type`, schema version, and server timestamp.
- Stable `encounter_id`, recipient player/ship, sector, and encounter category.
- Whether a response is required, its deadline or expiry, and whether a
  default action will occur on expiry.
- A concise, structured description of the situation and the offered choices.
- Any visible costs, risks, rewards, turn costs, and eligibility conditions
  needed to compare those choices.

The event must distinguish an encounter notification from a completed outcome.
Retries must retain the same event and encounter identifiers so the AI can
deduplicate delivery.

### AI-visible state

The AI needs an authoritative encounter view containing the encounter's
current status (`pending`, `resolved`, `expired`, or equivalent), its legal
actions and validation constraints, and the relevant current player/ship
state. At minimum, decisions may depend on sector, shields/hull, credits,
cargo/holds, fighters, weapons, and turn availability. The server should omit
hidden opponent or encounter information that the game does not reveal to a
human player.

If state can change after the event arrives, the action response must identify
which state version or precondition was used. The AI must be able to refresh
the pending encounter before acting.

### Available actions

Provide a documented response command or action interface tied to the
`encounter_id`. The server must enumerate or validate choices such as accept,
decline, evade, negotiate, or fight only when that choice is legal for that
encounter. The AI must not infer action names from the encounter category.

Actions must support an idempotency key. A repeated request with the same key
must not charge credits, consume turns, or apply combat effects twice. The
interface must specify whether an action is final or whether more player
decisions can follow.

### Action response and outcome

Return a correlated response that reports:

- Whether the request was accepted and whether the encounter is now resolved.
- The authoritative result and any follow-up encounter identifier or state.
- Actual changes to credits, cargo, ship condition, fighters, turns, and
  location that the client can reconcile.
- The event/encounter identifiers and server state version associated with the
  result.

Publish a corresponding completion event when asynchronous engine work is
involved. The response and event must be safely correlatable and deduplicable.

### Failure handling

Return machine-readable errors for unknown encounters, expired encounters,
illegal choices, failed preconditions, insufficient resources, and temporary
server failures. Include whether the encounter remains actionable and whether
the client should refresh state. An expired or already-resolved encounter must
not partially apply the requested action.

If the action response times out, the AI must not blindly replay a mutation.
Provide a way to query encounter status by `encounter_id` or idempotency key.
The result of expiry/default behavior must also be observable to the player and
AI.

## 2. Colony planning

The AI can plan colony work only from server-confirmed planet state. Current
repository documentation describes planet and colonist commands, but the AI
does not yet maintain a complete, normalized colony snapshot. A usable
authoritative view must expose the following data together or through clearly
correlated responses.

### Required planet and population data

- Planet ID, sector, owner ID/type, class, and whether the active ship is
  currently landed there.
- Total population and unassigned colonists.
- Current workers by assignment: ore, organics, equipment, military, and any
  other supported role.
- Capacity for each worker role and the planet's population cap.
- Population growth state: last growth tick, next expected tick or cadence,
  and the server's current estimated growth where available.

Population totals must make clear whether assigned workers are included.
Worker assignment changes must conserve population except for explicit growth,
transfer, death, or another reported game action.

### Required production and stock data

- Current production rate by commodity and workforce, including class
  multipliers and any limiting factors.
- Last production tick and the next tick/cadence, or a clearly labelled
  estimate if the exact time is unavailable.
- Current stock by commodity, including colonists where they are transferable.
- Capacity, reserved quantity, and available quantity for each stock entry.
- The authoritative storage source for every commodity, consistent across
  planet views, production, transfers, construction, and market actions.

The client must be able to distinguish observed stock from estimated future
production. A UI or AI must never treat a production estimate as spendable
inventory.

### Required economic state

For decisions involving trade or construction, provide the state that
determines affordability and value:

- Player/planet treasury or account balance and the account charged.
- Current buy/sell prices or order-book terms, quantity limits, and market
  freshness/version.
- Construction costs, reserved resources, completion state, and cancellation
  or failure results where applicable.
- Relevant legality, ownership, class restrictions, taxes, and fees.

If market prices are not available for planet stock, the AI should still be
able to plan production from explicit shortages, storage limits, or player
goals, without fabricating a commodity valuation.

### Assignment and transfer lifecycle

The server must accept bounded worker targets, validate ownership and landed
state, and return the complete resulting assignment and unassigned population.
The response must say whether it is a target or a completed mutation. The AI
should refresh planet state after assignment, transfer, construction, and
production events. Retries of a timed-out assignment must be idempotent or
reconciled by reading the authoritative assignments first.

## 3. Event subscriptions

The current AI can process events that reach its stream, but reliable topic
subscription depends on the server aligning the subscription request field
(`topic` versus `event_type`) and defining delivery semantics.

### Topics the AI needs

The server should support explicit, documented subscriptions for the relevant
categories, scoped to the authenticated player's visibility:

- `sector.*`: current-sector changes and relevant sector contents/ownership.
- `combat.*`: attacks against the player's ship, combat state changes, and
  completed combat results.
- `encounter.*`: incoming, updated, expired, and resolved encounters.
- `ship.*` and `player.*`: authoritative changes affecting the active player
  or ship, including resource and status changes.
- `trade.*` and `port.*`: completed trades, price/stock changes, and relevant
  market results.
- `planet.*`: landed-planet assignment, population, production, stock, and
  construction changes for owned or explicitly accessible planets.
- `move.*`: authoritative movement and persisted autopilot state changes.

The server may publish more specific topic names. It should document which
events require a full state refresh and which carry a complete state delta.

### Filtering and authorization

Subscriptions should permit server-side filters such as player ID, active ship
ID, sector ID, encounter ID, planet ID, and corporation ID where authorized.
Filters must be applied after authentication and authorization; a client must
not gain access to another player's private state by choosing a filter.
Subscription scope should allow “my player and active ship” as a simple,
least-privilege default and avoid sending unrelated universe-wide traffic.

The request contract must use one canonical topic field and validate topic and
filter names. Invalid topics/filters should return a correlated, actionable
error rather than silently subscribing to a different stream.

### Delivery guarantees

For gameplay-relevant events, provide at-least-once delivery with stable event
IDs, server timestamps, and a sequence or resume cursor. Document ordering
scope (global, player, or entity) and retention. Duplicate delivery must be
expected and safe. If events can be dropped, the server must expose a cursor
gap or missed-event signal that tells clients to refresh authoritative state.

Event payloads should identify their schema version and whether they are a
complete snapshot or a partial delta. Deltas need an entity revision/version
so the AI can reject stale updates and request a refresh after a gap.

### Reconnect behavior

After reconnect and reauthentication, the client must be able to restore its
subscriptions and resume from its last acknowledged cursor. If the cursor has
expired, the server should explicitly report that fact and provide a snapshot
or require the client to refresh the affected player, ship, sector, encounter,
or planet state before resuming incremental events.

The server should define acknowledgement behavior and cursor persistence.
Client-side event deduplication must use stable event IDs across reconnects;
replayed events must not repeat mutations. Pending commands and events must
remain distinguishable, and an event must not be mistaken for a reply to a
command.

## Ownership and implementation dependencies

- Encounter generation, event delivery, outcomes, and action validation belong
  to engine/server ownership. Issue #439 is blocked until that interface exists.
- Planet and colonist commands/state are server-owned. AI colony planning can
  begin after the authoritative fields and deployed command behavior above are
  verified and the AI state manager can consume them without assuming fields.
- Subscription field alignment, topic filtering, replay, and delivery
  guarantees belong to server/protocol ownership. AI reconnect/resume work
  depends on that contract.
- AI implementation should consume these interfaces only; it must not add
  parallel encounter or colony simulation systems.
