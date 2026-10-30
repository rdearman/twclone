# Issue #490 mutation recovery matrix

This matrix records what the AI can safely reconcile after a timeout or
disconnect. It is based on the ordinary protocol handlers and offline tests.
It does not claim live server acceptance.

| Operation family | Authoritative result available | Readback required | Automatic late reconciliation |
| --- | --- | --- | --- |
| `trade.buy` | `trade.buy_receipt_v1` with lines, credits and fees | `player.my_info`, `ship.info` | Supported when the request ID and receipt shape match; otherwise `uncertain_trade` remains set |
| `trade.sell` | `trade.sell_receipt_v1` with lines, credits and fees | `player.my_info`, `ship.info` | Supported under the same strict correlation rules |
| `move.warp` | `move.result` with matching destination | player/ship location readback | Supported when request ID and destination match; otherwise `uncertain_warp` remains set |
| `move.pathfind` | route/path response, no world mutation | fresh location/path before dispatch | No mutation quarantine; discard stale route and recompute |
| `move.autopilot.*` | route/status/stopped responses | `move.autopilot.status`, location | No automatic mutation reconciliation; status must be re-read after reconnect |
| `bank.*`, `planet.*`, `ship.*` mutations | command-specific responses vary | command-specific authoritative reads | Quarantined; no shared safe receipt contract |
| `combat.*`, `hardware.*`, `corp.*`, `stock.*`, assets | command-specific responses vary | target-specific authoritative reads | Quarantined; operator review required after ambiguity |

The controller must never replay an unresolved mutation with a new idempotency
key. A valid correlated trade receipt or matching warp result may clear its
marker; malformed, duplicated, reordered or mismatched responses leave the
marker in place. Missing infrastructure, missing receipts and incomplete
readbacks are unqualified evidence rather than acceptance.
