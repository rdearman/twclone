# AI QA Bot Summary Report - 20251115-195340
**Total Steps:** 55
**Final Credits:** 19500.00
**Final Bank Balance:** 19500.00

---
## Command Coverage
- **Total Documented Commands:** 80
- **Commands Successfully Executed:** 7
- **Commands Resulting in Error:** 3
- **Commands Never Tried:** 71

### Details: Commands Never Tried
- `auth.logout`
- `auth.refresh`
- `auth.register`
- `bank.orders.cancel`
- `bank.orders.create`
- `bank.orders.list`
- `bank.statement`
- `bank.transfer`
- `bank.withdraw`
- `bounty.list`
- `bounty.post`
- `combat.attack`
- `combat.deploy_fighters`
- `combat.lay_mines`
- `corp.balance`
- `corp.deposit`
- `corp.issue_dividend`
- `corp.set_tax`
- `corp.statement`
- `corp.stock.issue`
- `corp.withdraw`
- `fine.list`
- `fine.pay`
- `insurance.claim.file`
- `insurance.policies.buy`
- `insurance.policies.list`
- `loan.accept`
- `loan.apply`
- `loan.list_active`
- `loan.offers.list`
- `loan.repay`
- `market.contracts.buy`
- `market.contracts.list`
- `market.contracts.sell`
- `market.orders.cancel`
- `market.orders.create`
- `market.orders.list`
- `move.autopilot.start`
- `move.autopilot.status`
- `move.autopilot.stop`
- `move.describe_sector`
- `move.pathfind`
- `move.transwarp`
- `nav.avoid.add`
- `nav.avoid.list`
- `nav.avoid.remove`
- `nav.bookmark.add`
- `nav.bookmark.list`
- `nav.bookmark.remove`
- `planet.create`
- `planet.deposit`
- `planet.list_mine`
- `planet.withdraw`
- `player.get_settings`
- `player.list_online`
- `player.my_info`
- `player.rankings`
- `player.set_prefs`
- `research.projects.fund`
- `research.projects.list`
- `ship.jettison`
- `ship.status`
- `ship.upgrade`
- `stock.exchange.list_stocks`
- `stock.exchange.orders.cancel`
- `stock.exchange.orders.create`
- `stock.portfolio.list`
- `system.capabilities`
- `system.describe_schema`
- `trade.history`
- `trade.sell`

---
## Error Code Coverage
- **Unique Error Codes Discovered:** 3

### Details: Error Codes Seen
- `1306`
- `1402`
- `1702`

---
## Scenario Coverage (QA Metrics)
- ✅ Covered `seen_1402_error`
- ❌ Not Covered `seen_1403_error`
- ❌ Not Covered `seen_1307_error`
- ❌ Not Covered `seen_1405_sell_rejection`
- ❌ Not Covered `tested_zero_holds`
- ❌ Not Covered `tested_full_holds`
- ❌ Not Covered `tested_empty_credits`
- ❌ Not Covered `tested_high_credits`

---
## Deduplicated Bugs Reported
- **Total Unique Bugs:** 1

### Details: Bugs Reported
- **Invariant Failure: player_location_sector_positive** (Count: 2)
  - Description: Player location sector is invalid: 0
  - First Seen: 20251115-180112
  - Latest Seen: 20251115-180123
  - File: `bugs/BUG-20251115-180112-Invariant Failure: player_location_sector_positive-d343bd91.md`

---
## Command Call Statistics
| Command | Total Calls | Successful | Failed | Error Codes |
|---------|-------------|------------|--------|-------------|
| `auth.login` | 1 | 1 | 0 |  |
| `bank.balance` | 11 | 11 | 0 |  |
| `bank.deposit` | 12 | 0 | 12 | 1702 |
| `move.warp` | 3 | 0 | 3 | 1306 |
| `sector.info` | 1 | 1 | 0 |  |
| `ship.info` | 4 | 4 | 0 |  |
| `system.hello` | 11 | 11 | 0 |  |
| `trade.buy` | 3 | 0 | 3 | 1402 |
| `trade.port_info` | 1 | 1 | 0 |  |
| `trade.quote` | 6 | 6 | 0 |  |
