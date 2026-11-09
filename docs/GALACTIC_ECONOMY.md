# Galactic Economy

The authoritative product decisions for V3 economic gameplay are maintained in
[`V3_GAMEPLAY_DECISIONS.md`](V3_GAMEPLAY_DECISIONS.md). The complete inventory,
ownership, virtual transport, matching, settlement, funding and accounting
contract is [`V3_GALACTIC_EXCHANGE_CONTRACT.md`](V3_GALACTIC_EXCHANGE_CONTRACT.md).
Protocol details are in
[`PROTOCOL.v3/22_Trade_and_Port_Commands.md`](PROTOCOL.v3/22_Trade_and_Port_Commands.md)
and standing-order details in
[`PROTOCOL.v3/26_Banking_and_Ledger.md`](PROTOCOL.v3/26_Banking_and_Ledger.md).

This file is an index, not an independent or competing source of gameplay
rules. Older descriptions of port-only physical stock transfers, guaranteed
central-market settlement, invented liquidity, or conservation only between
ships and ports are superseded by the V3 Exchange contract. Market behavior
must be evaluated against the current approved contract and implementation;
the existence of order-generation or settlement functions is not evidence
that goods are fully accounted for.

## Source entry points

- Planet market: `cmd_planet_market_sell`,
  `cmd_planet_market_buy_order`, and
  `cmd_planet_market_buy_order_cancel` in `src/server_planets.c`.
- Market scheduling/matching: `h_planet_market_tick`,
  `h_port_economy_tick`, and `h_daily_market_settlement` in
  `src/server_cron.c`.
- Inventory/order repositories: `src/db/repo/repo_market.c` and
  `src/db/repo/repo_market_dynamic.c`.
- NPC trading: see `docs/PROTOCOL.v3/27_NPC_and_Ferengi_AI.md`; NPCs must use
  authoritative existing inventory and credit constraints.

## Verification status

The source-level Exchange audit is in progress. No statement here should be
read as runtime certification. The decision register tracks each approved
rule's implementation and verification state separately.
