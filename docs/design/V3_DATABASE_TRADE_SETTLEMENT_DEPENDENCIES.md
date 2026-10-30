# V3 PostgreSQL trade settlement dependencies

`market_fill_order(order_id, fill_qty, actor_id)` currently updates only order counters. Atomic credit, cargo, inventory, and receipt settlement requires the caller to provide:

- the buyer and seller actor types and IDs (or two order IDs for matched orders);
- buyer and seller location types and IDs;
- commodity ID/code, quantity, and agreed unit price;
- buyer and seller bank-account IDs or owner keys;
- buyer ship/planet cargo target and seller inventory source;
- an idempotency/settlement group key.

The database can then lock both orders, both account rows, and both inventory/cargo rows in a deterministic order, write ledger transactions, write one `commodity_trades` receipt, and update order fills in one transaction. CODEX 2 must supply this contract before `market_fill_order` can be expanded without inventing matching semantics.

Migration `125_trade_settlement_invariants.sql` already prevents new order overfills, requires settlement receipt IDs to reference bank transactions, and prevents reuse of either receipt ID.
