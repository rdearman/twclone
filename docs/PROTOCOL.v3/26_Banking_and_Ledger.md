# 26. Banking & Ledger

## 1. Personal Banking

Commands typically available at Stardock or banking ports.

### `bank.balance`
Get current balance.

### `bank.statement`
Paginated transaction history.

### `bank.deposit` / `bank.withdraw`
Transfer between Petty Cash (Ship) and Bank.
**Args**: `{ "amount": 500 }` (positive integer).
**Response**: `bank.deposit.confirmed` returns `player_id` and `new_balance`;
`bank.withdraw.confirmed` returns `new_balance`.

### `bank.transfer`
Transfer to another player.
**Args**: `{ "to_player_id": 123, "amount": 500 }`. `recipient_id` is also
accepted as a compatibility alias for `to_player_id`. The legacy
`recipient_type` and `memo` fields remain accepted but are not used by the
current handler.
**Response**: `bank.transfer.confirmed` returns `from_player_id`,
`to_player_id`, `from_balance`, and `to_balance`.

### `bank.history`
Advanced filtered history.

### `bank.leaderboard`
List wealthiest players.

## 2. Standing Orders

### `bank.orders.list` / `create` / `cancel`
Manage recurring payments (e.g., salaries).

## 3. Engine Events

*   **`bank.pay_interest.v1`**: Engine command to process interest.
