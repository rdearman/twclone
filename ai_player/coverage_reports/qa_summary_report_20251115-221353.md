# AI QA Bot Summary Report - 20251115-221353
**Total Steps:** 874
**Final Credits:** 19500.00
**Final Bank Balance:** 19500.00

---
## Command Coverage
- **Total Documented Commands:** 80
- **Commands Successfully Executed:** 8
- **Commands Resulting in Error:** 3
- **Commands Never Tried:** 70

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
- **Total Unique Bugs:** 3

### Details: Bugs Reported
- **Invariant Failure: player_location_sector_positive** (Count: 3)
  - Description: Player location sector is invalid: 0
  - First Seen: 20251115-195542
  - Latest Seen: 20251115-195557
  - File: `bugs/BUG-20251115-195542-Invariant Failure: player_location_sector_positive-d343bd91.md`
- **Protocol Error** (Count: 1)
  - Description: Server returned a protocol error: Commands seen live but not found in documentation: {'sector.info'}
  - First Seen: 20251115-203644
  - Latest Seen: 20251115-203644
  - File: `bugs/BUG-20251115-203644-Protocol Error-6d9fd9cd.md`
- **Invariant Failure: Command Exceeded Max Retries** (Count: 2)
  - Description: Command 'bank.deposit' failed 3 times and exceeded max retries.
  - First Seen: 20251115-221326
  - Latest Seen: 20251115-221352
  - File: `bugs/BUG-20251115-221326-Invariant Failure: Command Exceeded Max Retries-5838d197.md`

---
## Command Call Statistics
| Command | Total Calls | Successful | Failed | Error Codes |
|---------|-------------|------------|--------|-------------|
| `auth.login` | 1 | 1 | 0 |  |
| `bank.balance` | 16 | 16 | 0 |  |
| `bank.deposit` | 3 | 0 | 3 | 1702 |
| `move.warp` | 3 | 0 | 3 | 1306 |
| `player.my_info` | 1 | 1 | 0 |  |
| `sector.info` | 14 | 14 | 0 |  |
| `ship.info` | 4 | 4 | 0 |  |
| `system.hello` | 4 | 4 | 0 |  |
| `trade.buy` | 3 | 0 | 3 | 1402 |
| `trade.port_info` | 1 | 1 | 0 |  |
| `trade.quote` | 3 | 3 | 0 |  |
