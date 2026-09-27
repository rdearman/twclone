# 28. Tavern, Noticeboard, & Community

## 1. Bounties

### `bounty.list`
List active bounties.

### `bounty.post`
Post a bounty (funds held in escrow).

## 2. System Notices

### `system.notice` (Event)
Broadcasts for news, maintenance, or game events.
Stored in `system_notice` table with TTL.

### `notice.list` (RPC)
Reads notices newest first, ordered by creation time and then notice ID.
Optional `limit` (1–100) and `include_expired` fields select the page and
whether expired entries are included. Pass `next_cursor` from the response as
`cursor` to read the next page. `next_cursor` is `null` when there are no more
results. `created_at`, `expires_at`, and `seen_at` are returned as timestamp
strings. Requests without pagination fields still return the first page.

## 3. Community RPCs (Proposed)

*   **`notes.*`**: Personal notes (see [20_Player_Commands.md](./20_Player_Commands.md)).
*   **Chat**: (Future expansion for global/sector chat).

## 4. Mail and acknowledgements

`mail.send` takes `recipient_id` and `body`; `subject` and
`idempotency_key` are optional. `to_id`, `to`, and `to_player_name` remain
accepted aliases for older clients. The server returns `mail.sent` with `id`.

`mail.inbox` returns `items`, each with an `id`. `mail.read` accepts
`mail_id` and returns the selected message. `mail.delete` accepts an `ids`
array and returns the deleted count. For compatibility, `mail.read` also
accepts `id`, and `mail.delete` accepts one `mail_id`.

`notice.ack` accepts `notice_id` and returns `notice.acknowledged`. The legacy
`id` field remains accepted.
