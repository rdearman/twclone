# 26. Banking & Ledger

## 1. Personal Banking

Commands typically available at Stardock or banking ports.

### `bank.balance`
Get current balance.

### `bank.statement`
Paginated transaction history.

### `bank.deposit` / `bank.withdraw`
Transfer between Petty Cash (Ship) and Bank.
**Note**: Fees may apply.

### `bank.transfer`
Transfer to another player.
**Args**: `{ "to_player_id": 123, "amount": 500, "memo": "Thanks" }`

### `bank.history`
Advanced filtered history.

### `bank.leaderboard`
List wealthiest players.

## 2. Standing Orders

### `bank.orders.list` / `create` / `cancel`
Manage recurring payments (e.g., salaries).

## 3. Engine Events

*   **`bank.pay_interest.v1`**: Engine command to process interest.
