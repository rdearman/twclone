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

### `bank.orders.create` / `bank.orders.list` / `bank.orders.cancel`

`bank.orders.create` request fields:

```json
{
  "recipient_player_id": 123,
  "amount": 250,
  "frequency": "daily | weekly | monthly",
  "time_utc": "HH:MM",
  "weekday": 1,
  "day_of_month": 31
}
```

`weekday` is required only for weekly schedules and uses ISO-8601 numbering
(Monday=1 through Sunday=7). `day_of_month` is required only for monthly
schedules (1–31); it is retained as configured even when a month is shorter.
`time_utc` is a 24-hour UTC time with minute precision. `amount` must be a
positive integer number of CRD credits. The recipient must be an existing
player. Creation does not reserve or debit funds.

`bank.orders.list` accepts an optional `page` (1-based, default 1) and `limit`
(default 50); it returns only the authenticated player's orders. `bank.orders.cancel`
requires `order_id` belonging to the authenticated player. A successful create
returns `order_id`, normalized schedule, and next execution time; list returns
the same public order fields; cancel returns `order_id` and `cancelled: true`.

Standing orders support daily, weekly and monthly schedules at the specified
UTC execution time. If funds are insufficient for a due occurrence, skip that
occurrence without debt or later retry; keep the order active for its next
scheduled occurrence. A successful cancellation prevents future payments
immediately, while completed payments remain final in the ledger. Schedule
state is durable, each occurrence settles at most once, and cancellation
serializes with execution. If a monthly scheduled day does not exist, execute
on that month's last day while retaining the configured day for subsequent
months. If the recipient no longer exists, cancel the order without reversing
completed payments. No arbitrary active-order limit is imposed.

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

V3 insurance covers hull replacement only. On qualifying ship destruction the
shared ship-loss transaction selects active player-held ship policies whose
epoch `start_ts`/`expiry_ts` cover the destruction time. It uses each policy's
configured payout, capped in aggregate at shiptype `basecost`, and credits the
player's CRD bank account. Funded insurer payouts use `ADJUSTMENT/CREDIT`; an
insurer shortfall is covered by an explicit `INSURANCE_GUARANTEE/CREDIT`
system-issued ledger entry. Policy claims, any insurer-fund debit, bank credits
and ship destruction are one transaction and are keyed to that destroyed hull. Cargo and installed
equipment are not covered; insurance does not directly replace the ship and
does not alter mandatory escape-pod respawn. Multiple policies apply by policy
ID until the replacement-value cap is reached. A missing or underfunded insurer
reserve does not prevent a qualifying claim from being paid; the guaranteed
system-funded amount is separately labeled. Selling an insured ship ends its
policy without refund.
