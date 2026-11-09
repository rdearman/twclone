# 26. Banking & Ledger

Canonical product decisions: [`../V3_GAMEPLAY_DECISIONS.md`](../V3_GAMEPLAY_DECISIONS.md); Exchange settlement: [`../V3_GALACTIC_EXCHANGE_CONTRACT.md`](../V3_GALACTIC_EXCHANGE_CONTRACT.md).

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
Standing orders support daily, weekly and monthly schedules at an explicitly
specified UTC execution time. Recipients may be any existing player. If funds
are insufficient for a due occurrence, skip that occurrence without debt or
later retry; keep the order active for its next scheduled occurrence. A
successful cancellation prevents future payments immediately, while completed
payments remain final in the ledger. Schedule state is durable, each occurrence
settles at most once, and cancellation serializes with execution. If a monthly
scheduled day does not exist, execute on that month's last day while retaining
the configured day for subsequent months. If the recipient no longer exists,
cancel the order without reversing completed payments. No arbitrary active
order limit is imposed.

## 3. Engine Events

*   **`bank.pay_interest.v1`**: Engine command to process interest.
# Insurance commands

`insurance.policies.list` returns the authenticated player's policies.
`insurance.policies.buy` requires `subject_type`, `subject_id`, `duration`,
`premium`, `payout`, and `fund_id`; PostgreSQL debits the holder's CRD account,
credits the fund, and creates the policy atomically. `insurance.claim.file`
settles an existing `claim_id` with `event_id` and `amount`; PostgreSQL locks
the claim, policy, fund and holder account, and repeated settlement is
idempotent. No server-side balance or claim updates are performed outside
those routines.

V3 insurance covers hull replacement only. On qualifying ship destruction it
settles automatically to the player's bank account under existing eligibility,
premium, policy-lifecycle and claim rules. Cargo and installed equipment are
not covered, insurance does not directly replace the ship, and it does not
alter mandatory escape-pod respawn. Selling an insured ship terminates its
policy without refund. Use the existing authoritative insured-hull valuation;
if that valuation cannot be determined from current policy/configuration,
record the exact valuation gap instead of inventing a formula.
