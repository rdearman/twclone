# AI QA Bot Summary Report - 20251115-225859
**Total Steps:** 4
**Final Credits:** 10000.00
**Final Bank Balance:** 0.00

---
## Command Coverage
- **Total Documented Commands:** 80
- **Commands Successfully Executed:** 3
- **Commands Resulting in Error:** 0
- **Commands Never Tried:** 77

### Details: Commands Never Tried
- `auth.logout`
- `auth.refresh`
- `auth.register`
- `bank.balance`
- `bank.deposit`
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
- `move.warp`
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
- `trade.buy`
- `trade.history`
- `trade.port_info`
- `trade.quote`
- `trade.sell`

---
## Error Code Coverage
- **Unique Error Codes Discovered:** 0

---
## Scenario Coverage (QA Metrics)
- ❌ Not Covered `seen_1402_error`
- ❌ Not Covered `seen_1403_error`
- ❌ Not Covered `seen_1307_error`
- ❌ Not Covered `seen_1405_sell_rejection`
- ❌ Not Covered `tested_zero_holds`
- ❌ Not Covered `tested_full_holds`
- ❌ Not Covered `tested_empty_credits`
- ❌ Not Covered `tested_high_credits`

---
## Deduplicated Bugs Reported
- **Total Unique Bugs:** 0

---
## Command Call Statistics
| Command | Total Calls | Successful | Failed | Error Codes |
|---------|-------------|------------|--------|-------------|
| `auth.login` | 1 | 1 | 0 |  |
| `ship.info` | 1 | 1 | 0 |  |
| `system.hello` | 1 | 1 | 0 |  |
