# 25. Corporations & Stock Market

## 1. Corporation Management

### `corp.balance`
Get corp treasury balance.

### `corp.statement`
Get transaction history.

### `corp.deposit` / `corp.withdraw`
Transfer funds between player and corp.
**Response**: `corp.transaction_receipt`.

### `corp.set_tax`
Set tax rate for members.
**Response**: `corp.tax_rate_updated`.

### `corp.issue_dividend`
Distribute funds to members.
**Response**: `corp.dividend_issued`.

### `corp.join` / `leave`
Manage membership.
**Events**: Emits `player.corp_join.v1`.

## 2. Events

*   **`player.corp_join.v1`**: `{ player_id, corp_id }`
*   **`corp.adjust_funds.v1`**: Engine command for adjustments.
*   **`corp.destroy.v1`**: Engine command for dissolution.
