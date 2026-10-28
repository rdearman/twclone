# V2 Client Gameplay Backlog Audit

Client-side audit for issues #40, #83, #135, #136, #310, and #312 (2026-09-27).
This document records client work and missing server contracts. It does not
change protocol schemas or server behavior.

## Client improvements

- **#40 Client UI polish:** Godot's field guide now explains shipyard repair,
  hull exchange, jettison, news, notices, and when daily announcements appear.
- **#135 Info/logs:** both clients already expose the news feed and notices.
  Godot now renders news articles and notices as readable entries, including
  notice read state, instead of a generic nested response dump. The Python
  client already formats news articles and has notice presentation coverage.
- **#136 Ship QoL:** Godot exposes `ship.repair` only when the confirmed ship
  state shows a dock at a shipyard. The server remains authoritative for repair
  price and credits. Both clients already have shipyard hull exchange and cargo
  jettison flows; Python now exposes repair from its dock-gated shipyard menu.
- **#310 Daily announcements:** the existing `news.get_feed` view can display
  server-published headline articles. There is no client-side ranking
  calculation or synthetic announcement generation.

## Server/protocol dependencies

- **#83 Alien/trader listing:** no player-facing list command was found. A
  read-only API should return a paginated `items` array and `next_cursor`, with
  stable NPC ID, public type/name, last-seen time, and sector only when the
  player is permitted to know it. Empty pages and stale/unknown locations need
  explicit, non-error semantics. The client must not infer private NPC state
  from encounter events.
- **#135 Daily log:** issue #79 is implemented through cursor-paginated
  `notice.list`; clients expose notice history and read state. Any additional
  combat/action log needs a server-owned read command and a documented item
  schema before either UI should present it as a durable log.
- **#136 Ship operations:** `ship.transfer_cargo` and component `ship.upgrade`
  currently return not-implemented responses. `ship.rename` has a schema /
  handler field mismatch. Keep these actions unavailable until the server
  contract and successful behavior agree. `shipyard.upgrade`, `ship.repair`,
  and `ship.jettison` are existing supported operations.
- **#310 Daily ranking announcements:** issue #310 is marked as a duplicate of
  #301, whose server-side daily news/headlines feature is closed. The news
  client can show published articles, but the producer must actually publish
  trader/fighter ranking entries before the UI can display them. Do not
  calculate authoritative rankings in the client.
- **#312 Turnbank/login bonuses:** no player command or login response fields
  for banked turns, bonus eligibility, or claiming a daily bonus were found.
  The server contract should expose current turns, bank balance/cap, accrual
  timing, and claim eligibility. A claim operation should be idempotent and
  return the awarded amount and next eligibility time. The client can then
  show a status panel and an explicit claim action; it must not calculate or
  grant turns locally.

## Verification scope

Client tests cover Godot news/notice formatting, empty responses, repair dock
gating, request shape, and field-guide text, plus Python news rendering. Server
integration suites and daily announcement/turnbank behavior remain server
owner responsibilities.
