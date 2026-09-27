# 25. Corporations & Stock Market

## 1. Corporation Management

### `corp.create`
Create a corporation. **Args**: `{ "name": "Example Corp" }`.
Returns `corp_id` and `name` in `corp.create.success`.

### `corp.list`
List corporations. Takes no arguments; returns `corporations` in
`corp.list.success`.

### `corp.status`
Get the caller's corporation and membership status. Takes no arguments;
returns the corporation details and `your_role` in `corp.status.success`.

### `corp.invite` / `corp.join`
Invite a player with `{ "target_player_id": 123 }`; accept an invitation with
`{ "corp_id": 456 }`. Responses are `corp.invite.success` and
`corp.join.success`.

### `corp.roster` / `corp.kick`
Get a roster with `{ "corp_id": 456 }`; remove a member with
`{ "target_player_id": 123 }`. Responses are `corp.roster.success` and
`corp.kick.success`.

### `corp.leave` / `corp.dissolve`
Leave the caller's corporation or dissolve it as its CEO. Both commands take
no arguments. Leaving can return `corp.leave.success` or
`corp.leave.dissolved` when the last member leaves.

### `corp.transfer_ceo`
Transfer the CEO role with `{ "target_player_id": 123 }`.
Returns `corp.transfer_ceo.success`.

### `corp.balance`
Get the caller's corporation treasury balance. Takes no arguments; returns
`corp_id` and `balance` in `corp.balance.success`.

### `corp.statement`
Get transaction history. Optional args: `{ "limit": 20 }`; limit must be a
positive integer. Returns `corp_id` and `transactions` in
`corp.statement.success`.

### `corp.deposit` / `corp.withdraw`
Transfer funds between the caller's player account and corporation treasury.
Args: `{ "amount": 500 }` (positive integer). Responses are
`corp.deposit.success` and `corp.withdraw.success`; withdrawals require the
Leader or Officer role.

### `corp.set_tax`
Set tax rate for members.
Not currently registered as a server RPC.

### `corp.issue_dividend`
Distribute funds to members.
Not currently registered as a server RPC.

### `corp.join` / `leave`
Manage membership.
These commands currently respond directly and do not emit
`player.corp_join.v1`.

## 2. Events

*   **`player.corp_join.v1`** is documented in the engine event catalog but is
    not currently emitted by the corporation handlers.
*   **`corp.adjust_funds.v1`**: Engine command for adjustments.
*   **`corp.destroy.v1`**: Engine command for dissolution.
