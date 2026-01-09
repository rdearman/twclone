# Test Coverage Report

| Command | Positive | Negative | Status | Rationale |
| :--- | :---: | :---: | :--- | :--- |
| admin.notice | Y | Y | COVERED |  |
| admin.shutdown_warning | N | Y | MISSING POSITIVE | Negative exists, Positive missing |
| auth.login | Y | Y | COVERED |  |
| auth.logout | Y | N | MISSING NEGATIVE | Positive exists, Negative missing |
| auth.mfa.totp.verify | N | N | MISSING | High priority gap |
| auth.refresh | Y | Y | COVERED |  |
| auth.register | Y | Y | COVERED |  |
| bank.balance | Y | Y | COVERED |  |
| bank.deposit | Y | Y | COVERED |  |
| bank.history | Y | Y | COVERED |  |
| bank.leaderboard | Y | Y | COVERED |  |
| bank.transfer | Y | Y | COVERED |  |
| bank.withdraw | Y | Y | COVERED |  |
| bounty.list | N | N | MISSING | High priority gap |
| bounty.post_federation | N | N | MISSING | High priority gap |
| bounty.post_hitlist | N | N | MISSING | High priority gap |
| bulk.execute | N | N | MISSING | High priority gap |
| chat.broadcast | Y | Y | COVERED |  |
| chat.history | Y | Y | COVERED |  |
| chat.send | Y | Y | COVERED |  |
| citadel.build | Y | N | MISSING NEGATIVE | Positive exists, Negative missing |
| citadel.upgrade | Y | N | MISSING NEGATIVE | Positive exists, Negative missing |
| combat.attack | Y | Y | COVERED |  |
| combat.attack_planet | Y | Y | COVERED |  |
| combat.deploy_fighters | Y | Y | COVERED |  |
| combat.deploy_mines | Y | Y | COVERED |  |
| combat.lay_mines | Y | Y | COVERED |  |
| combat.status | Y | Y | COVERED |  |
| combat.sweep_mines | Y | Y | COVERED |  |
| corp.balance | N | N | MISSING | High priority gap |
| corp.create | Y | Y | COVERED |  |
| corp.deposit | Y | Y | COVERED |  |
| corp.dissolve | Y | Y | COVERED |  |
| corp.invite | Y | Y | COVERED |  |
| corp.join | Y | Y | COVERED |  |
| corp.kick | Y | Y | COVERED |  |
| corp.leave | Y | Y | COVERED |  |
| corp.list | Y | N | MISSING NEGATIVE | Positive exists, Negative missing |
| corp.roster | Y | N | MISSING NEGATIVE | Positive exists, Negative missing |
| corp.statement | N | N | MISSING | High priority gap |
| corp.status | Y | Y | COVERED |  |
| corp.transfer_ceo | N | Y | MISSING POSITIVE | Negative exists, Positive missing |
| corp.withdraw | Y | Y | COVERED |  |
| deploy.fighters.list | Y | N | MISSING NEGATIVE | Positive exists, Negative missing |
| deploy.mines.list | Y | N | MISSING NEGATIVE | Positive exists, Negative missing |
| dock.status | Y | N | MISSING NEGATIVE | Positive exists, Negative missing |
| fighters.recall | Y | Y | COVERED |  |
| fine.list | N | N | MISSING | High priority gap |
| fine.pay | Y | Y | COVERED |  |
| hardware.buy | Y | Y | COVERED |  |
| hardware.list | Y | Y | COVERED |  |
| insurance.claim.file | Y | Y | COVERED |  |
| insurance.policies.buy | Y | Y | COVERED |  |
| insurance.policies.list | N | N | MISSING | High priority gap |
| mail.delete | Y | Y | COVERED |  |
| mail.inbox | Y | Y | COVERED |  |
| mail.read | Y | Y | COVERED |  |
| mail.send | Y | Y | COVERED |  |
| mines.recall | Y | Y | COVERED |  |
| move.autopilot.start | Y | Y | COVERED |  |
| move.autopilot.status | Y | Y | COVERED |  |
| move.autopilot.stop | Y | Y | COVERED |  |
| move.describe_sector | Y | Y | COVERED |  |
| move.pathfind | Y | Y | COVERED |  |
| move.scan | Y | Y | COVERED |  |
| move.transwarp | Y | Y | COVERED |  |
| move.warp | Y | Y | COVERED |  |
| nav.avoid.add | Y | Y | COVERED |  |
| nav.avoid.list | Y | Y | COVERED |  |
| nav.avoid.remove | Y | Y | COVERED |  |
| nav.avoid.set | N | N | MISSING | High priority gap |
| nav.bookmark.add | Y | Y | COVERED |  |
| nav.bookmark.list | Y | Y | COVERED |  |
| nav.bookmark.remove | Y | Y | COVERED |  |
| nav.bookmark.set | N | N | MISSING | High priority gap |
| news.get_feed | Y | Y | COVERED |  |
| news.mark_feed_read | N | N | MISSING | High priority gap |
| news.read | Y | Y | COVERED |  |
| notes.list | Y | N | MISSING NEGATIVE | Positive exists, Negative missing |
| notice.ack | Y | Y | COVERED |  |
| notice.list | Y | N | MISSING NEGATIVE | Positive exists, Negative missing |
| planet.create | Y | Y | EXCLUDED | Alias of planet.genesis_create |
| planet.deposit | Y | Y | COVERED |  |
| planet.genesis | Y | Y | EXCLUDED | Alias of planet.genesis_create |
| planet.genesis_create | Y | Y | COVERED |  |
| planet.harvest | Y | Y | COVERED |  |
| planet.info | Y | N | MISSING NEGATIVE | Positive exists, Negative missing |
| planet.land | Y | Y | COVERED |  |
| planet.launch | Y | Y | COVERED |  |
| planet.market.buy_order | N | N | MISSING | High priority gap |
| planet.market.sell | N | N | MISSING | High priority gap |
| planet.rename | N | N | MISSING | High priority gap |
| planet.transfer_ownership | Y | Y | COVERED |  |
| planet.withdraw | N | N | MISSING | High priority gap |
| player.get_avoids | N | N | MISSING | High priority gap |
| player.get_bookmarks | N | N | MISSING | High priority gap |
| player.get_notes | N | N | MISSING | High priority gap |
| player.get_prefs | Y | Y | COVERED |  |
| player.get_settings | N | N | MISSING | High priority gap |
| player.get_subscriptions | N | N | MISSING | High priority gap |
| player.get_topics | N | N | MISSING | High priority gap |
| player.list_online | Y | Y | COVERED |  |
| player.my_info | Y | Y | COVERED |  |
| player.ping | N | N | MISSING | High priority gap |
| player.rankings | N | N | MISSING | High priority gap |
| player.set_avoids | N | N | MISSING | High priority gap |
| player.set_bookmarks | N | N | MISSING | High priority gap |
| player.set_prefs | Y | Y | COVERED |  |
| player.set_settings | N | N | MISSING | High priority gap |
| player.set_subscriptions | N | N | MISSING | High priority gap |
| player.set_topics | N | N | MISSING | High priority gap |
| player.set_trade_account_preference | N | N | MISSING | High priority gap |
| port.describe | Y | Y | COVERED |  |
| port.info | N | N | MISSING | High priority gap |
| port.rob | Y | Y | COVERED |  |
| port.status | Y | Y | COVERED |  |
| sector.info | Y | N | MISSING NEGATIVE | Positive exists, Negative missing |
| sector.scan | N | N | MISSING | High priority gap |
| sector.scan.density | Y | N | MISSING NEGATIVE | Positive exists, Negative missing |
| sector.search | N | N | MISSING | High priority gap |
| sector.set_beacon | Y | Y | COVERED |  |
| session.disconnect | N | N | MISSING | High priority gap |
| session.hello | Y | N | MISSING NEGATIVE | Positive exists, Negative missing |
| session.ping | Y | N | MISSING NEGATIVE | Positive exists, Negative missing |
| ship.claim | N | N | MISSING | High priority gap |
| ship.info | N | N | MISSING | High priority gap |
| ship.inspect | Y | Y | COVERED |  |
| ship.jettison | N | N | MISSING | High priority gap |
| ship.rename | Y | Y | COVERED |  |
| ship.repair | Y | Y | COVERED |  |
| ship.reregister | Y | Y | EXCLUDED | Alias of ship.rename |
| ship.self_destruct | N | N | MISSING | High priority gap |
| ship.status | Y | Y | COVERED |  |
| ship.transfer_cargo | N | N | MISSING | High priority gap |
| ship.upgrade | N | N | MISSING | High priority gap |
| shipyard.list | Y | Y | COVERED |  |
| shipyard.upgrade | Y | Y | COVERED |  |
| stock.buy | Y | Y | COVERED |  |
| stock.dividend.set | N | N | MISSING | High priority gap |
| stock.exchange.list_stocks | N | N | MISSING | High priority gap |
| stock.exchange.orders.cancel | N | N | MISSING | High priority gap |
| stock.exchange.orders.create | N | N | MISSING | High priority gap |
| stock.ipo.register | Y | Y | COVERED |  |
| stock.portfolio.list | N | N | MISSING | High priority gap |
| subscribe.add | N | N | MISSING | High priority gap |
| subscribe.catalog | N | N | MISSING | High priority gap |
| subscribe.list | N | N | MISSING | High priority gap |
| subscribe.remove | N | N | MISSING | High priority gap |
| sys.notice.create | N | Y | MISSING POSITIVE | Negative exists, Positive missing |
| system.capabilities | Y | N | MISSING NEGATIVE | Positive exists, Negative missing |
| system.cmd_list | Y | N | MISSING NEGATIVE | Positive exists, Negative missing |
| system.describe_schema | N | N | MISSING | High priority gap |
| system.disconnect | N | N | MISSING | High priority gap |
| system.hello | Y | N | MISSING NEGATIVE | Positive exists, Negative missing |
| system.schema_list | N | N | MISSING | High priority gap |
| tavern.barcharts.get_prices_summary | Y | Y | COVERED |  |
| tavern.deadpool.place_bet | N | N | MISSING | High priority gap |
| tavern.dice.play | Y | Y | COVERED |  |
| tavern.graffiti.post | N | N | MISSING | High priority gap |
| tavern.highstakes.play | N | N | MISSING | High priority gap |
| tavern.loan.pay | N | N | MISSING | High priority gap |
| tavern.loan.take | Y | Y | COVERED |  |
| tavern.lottery.buy_ticket | N | N | MISSING | High priority gap |
| tavern.lottery.status | N | N | MISSING | High priority gap |
| tavern.raffle.buy_ticket | N | N | MISSING | High priority gap |
| tavern.round.buy | Y | Y | COVERED |  |
| tavern.rumour.get_hint | N | N | MISSING | High priority gap |
| tavern.trader.buy_password | N | N | MISSING | High priority gap |
| trade.accept | N | N | MISSING | High priority gap |
| trade.buy | N | Y | MISSING POSITIVE | Negative exists, Positive missing |
| trade.cancel | N | N | MISSING | High priority gap |
| trade.history | N | N | MISSING | High priority gap |
| trade.jettison | Y | Y | COVERED |  |
| trade.offer | N | N | MISSING | High priority gap |
| trade.port_info | N | N | MISSING | High priority gap |
| trade.quote | Y | Y | COVERED |  |
| trade.sell | N | N | MISSING | High priority gap |
