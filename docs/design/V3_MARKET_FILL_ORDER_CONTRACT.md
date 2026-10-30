# V3 `market_fill_order` settlement contract

The database routine must settle one already-validated market order on the
same transaction connection used by the server command. The request identifies
the order, buyer and seller accounts, and the commodity quantity; the routine
must derive authoritative prices and inventory rows rather than trusting
client totals.

Required inputs:

- `order_id` and `idempotency_key`;
- buyer and seller owner type/id plus their CRD account IDs;
- commodity code and requested quantity;
- source inventory/stock entity type/id and destination cargo ship ID;
- a server-generated receipt/request correlation ID.

The result must return a stable `receipt_id`, `order_id`, `trade_id`, buyer and
seller account IDs, commodity, quantity, unit price, gross amount, fees,
buyer/seller balance-after values, source inventory-after quantity, destination
cargo-after quantity, and the idempotency replay flag. Settlement must lock the
order, both accounts, source inventory and destination cargo; validate
ownership, availability and capacity; debit/credit the ledger; decrement
source inventory; increment destination cargo; and persist the receipt
atomically. Replaying the same idempotency key must return the original receipt
without applying any mutation twice.

The server must map routine refusal codes without performing compensating
balance, inventory or cargo writes itself.
