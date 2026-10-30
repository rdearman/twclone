# V3 PostgreSQL insurance dependencies

Migration `124_insurance_settlement.sql` provides transactional `insurance_buy_policy` and `insurance_settle_claim` routines. The server repository must call these routines on the same PostgreSQL connection and must not debit credits or mark claims paid independently.

`insurance_buy_policy` requires holder type/id, subject type/id, premium, payout, fund id, and start/expiry timestamps. The holder must have an active CRD bank account; the routine locks it, inserts the policy, debits the premium through `bank_transactions`, and credits the insurance fund atomically.

`insurance_settle_claim` requires a claim id, event id, and bounded amount. It locks claim, policy, fund, and holder account rows, checks policy activity and fund balance, credits the holder through the bank ledger, and marks the claim paid in one transaction. Repeated settlement of an already-paid claim is idempotent.

The existing V3 handlers in `src/server_loop.c` are placeholders and currently return fabricated success payloads. CODEX 2 must add repository/handler calls and map the returned `db_result`; no server-side debit, fund update, or claim status update should be added outside these routines.
