# Bug Report: Invariant Failure: Command Exceeded Max Retries

## Description
Command 'trade.buy' failed 3 times.

## Trigger
`N/A` in sector 307

## Expected
Invariant 'Command Exceeded Max Retries' to hold true

## Actual
Command 'trade.buy' failed 3 times.

## Reproducer Steps
1. Connect (system.hello)
2. auth.login as ai_qa_bot
3. ... (steps leading to the bug, derived from frames)
N/A

## Frames (Last 3 Request/Response)
```json
[]
```

## Last Commands History
```json
[
  "trade.quote",
  "trade.port_info",
  "trade.buy",
  "trade.buy",
  "trade.buy"
]
```

## Last Responses History
```json
[
  {
    "id": "srv-ok",
    "reply_to": "72606537-d9f8-4b7a-bb4c-14fa49c192dd",
    "ts": "2025-11-14T11:57:47Z",
    "status": "ok",
    "type": "trade.quote",
    "data": {
      "port_id": 198,
      "commodity": "organics",
      "quantity": 1,
      "buy_price": 155.0,
      "sell_price": 135.0,
      "total_buy_price": 155,
      "total_sell_price": 135
    },
    "error": null,
    "meta": {
      "rate_limit": {
        "limit": 60,
        "remaining": 60,
        "reset": 60,
        "reset_at": 1763121527.663698
      }
    }
  },
  {
    "id": "srv-ok",
    "reply_to": "3ff69b37-50a8-4241-a450-9f70cd8a13e4",
    "ts": "2025-11-14T11:57:47Z",
    "status": "ok",
    "type": "trade.port_info",
    "data": {
      "port": {
        "id": 198,
        "number": 197,
        "name": "Batiredfrid",
        "sector": 307,
        "size": 7,
        "techlevel": 3,
        "ore_on_hand": 5000,
        "organics_on_hand": 5000,
        "equipment_on_hand": 2500,
        "petty_cash": 0,
        "credits": 0,
        "type": 3
      }
    },
    "error": null,
    "meta": {
      "rate_limit": {
        "limit": 60,
        "remaining": 60,
        "reset": 60,
        "reset_at": 1763121528.1668751
      }
    }
  },
  {
    "id": "srv-err",
    "reply_to": "c1b428f4-ed10-402c-b779-b86564a060b0",
    "ts": "2025-11-14T11:57:48Z",
    "status": "error",
    "type": "error",
    "error": {
      "code": 400,
      "message": "idempotency_key required."
    },
    "data": null,
    "meta": {
      "rate_limit": {
        "limit": 60,
        "remaining": 60,
        "reset": 60,
        "reset_at": 1763121528.6692913
      }
    }
  },
  {
    "id": "srv-err",
    "reply_to": "ab34b40b-d41a-4a3f-bbbc-1ef68e00fa06",
    "ts": "2025-11-14T11:57:54Z",
    "status": "error",
    "type": "error",
    "error": {
      "code": 400,
      "message": "idempotency_key required."
    },
    "data": null,
    "meta": {
      "rate_limit": {
        "limit": 60,
        "remaining": 60,
        "reset": 60,
        "reset_at": 1763121535.1485806
      }
    }
  },
  {
    "id": "srv-err",
    "reply_to": "8de6adf4-9605-4a41-9463-fd26ec44f0d9",
    "ts": "2025-11-14T11:58:01Z",
    "status": "error",
    "type": "error",
    "error": {
      "code": 400,
      "message": "idempotency_key required."
    },
    "data": null,
    "meta": {
      "rate_limit": {
        "limit": 60,
        "remaining": 60,
        "reset": 60,
        "reset_at": 1763121541.543108
      }
    }
  }
]
```

## Server Capabilities
```json
{}
```

## Agent State (Relevant Snippet)
```json
{
  "stage": "exploit",
  "session_token": "72feb890e69de6415311714838221cc22ee036046c12a9019a5b6fc55ad38cab",
  "player_location_sector": 307,
  "last_server_response": {
    "id": "srv-err",
    "reply_to": "8de6adf4-9605-4a41-9463-fd26ec44f0d9",
    "ts": "2025-11-14T11:58:01Z",
    "status": "error",
    "type": "error",
    "error": {
      "code": 400,
      "message": "idempotency_key required."
    },
    "data": null,
    "meta": {
      "rate_limit": {
        "limit": 60,
        "remaining": 60,
        "reset": 60,
        "reset_at": 1763121541.543108
      }
    }
  },
  "last_command_sent_id": "8de6adf4-9605-4a41-9463-fd26ec44f0d9",
  "pending_commands": {},
  "last_commands_history": [
    "trade.quote",
    "trade.port_info",
    "trade.buy",
    "trade.buy",
    "trade.buy"
  ],
  "working_commands": [],
  "broken_commands": [],
  "received_responses": [],
  "visited_sectors": [
    170,
    307,
    156
  ],
  "visited_ports": [
    198
  ],
  "previous_sector_id": null,
  "sectors_with_info": [
    170,
    307,
    156
  ],
  "normalized_commands": {},
  "schemas_to_fetch": [],
  "cached_schemas": {
    "auth.login": {
      "request_schema": {}
    },
    "auth.logout": {
      "request_schema": {}
    },
    "auth.refresh": {
      "request_schema": {}
    },
    "auth.register": {
      "request_schema": {}
    },
    "bank.balance": {
      "request_schema": {}
    },
    "bank.deposit": {
      "request_schema": {}
    },
    "bank.orders.cancel": {
      "request_schema": {}
    },
    "bank.orders.create": {
      "request_schema": {}
    },
    "bank.orders.list": {
      "request_schema": {}
    },
    "bank.statement": {
      "request_schema": {}
    },
    "bank.transfer": {
      "request_schema": {}
    },
    "bank.withdraw": {
      "request_schema": {}
    },
    "bounty.list": {
      "request_schema": {}
    },
    "bounty.post": {
      "request_schema": {}
    },
    "combat.attack": {
      "request_schema": {}
    },
    "combat.deploy_fighters": {
      "request_schema": {}
    },
    "combat.lay_mines": {
      "request_schema": {}
    },
    "corp.balance": {
      "request_schema": {}
    },
    "corp.deposit": {
      "request_schema": {}
    },
    "corp.issue_dividend": {
      "request_schema": {}
    },
    "corp.set_tax": {
      "request_schema": {}
    },
    "corp.statement": {
      "request_schema": {}
    },
    "corp.stock.issue": {
      "request_schema": {}
    },
    "corp.withdraw": {
      "request_schema": {}
    },
    "fine.list": {
      "request_schema": {}
    },
    "fine.pay": {
      "request_schema": {}
    },
    "insurance.claim.file": {
      "request_schema": {}
    },
    "insurance.policies.buy": {
      "request_schema": {}
    },
    "insurance.policies.list": {
      "request_schema": {}
    },
    "loan.accept": {
      "request_schema": {}
    },
    "loan.apply": {
      "request_schema": {}
    },
    "loan.list_active": {
      "request_schema": {}
    },
    "loan.offers.list": {
      "request_schema": {}
    },
    "loan.repay": {
      "request_schema": {}
    },
    "market.contracts.buy": {
      "request_schema": {}
    },
    "market.contracts.list": {
      "request_schema": {}
    },
    "market.contracts.sell": {
      "request_schema": {}
    },
    "market.orders.cancel": {
      "request_schema": {}
    },
    "market.orders.create": {
      "request_schema": {}
    },
    "market.orders.list": {
      "request_schema": {}
    },
    "move.autopilot.start": {
      "request_schema": {}
    },
    "move.autopilot.status": {
      "request_schema": {}
    },
    "move.autopilot.stop": {
      "request_schema": {}
    },
    "move.describe_sector": {
      "request_schema": {}
    },
    "move.pathfind": {
      "request_schema": {}
    },
    "move.transwarp": {
      "request_schema": {}
    },
    "move.warp": {
      "request_schema": {}
    },
    "nav.avoid.add": {
      "request_schema": {}
    },
    "nav.avoid.list": {
      "request_schema": {}
    },
    "nav.avoid.remove": {
      "request_schema": {}
    },
    "nav.bookmark.add": {
      "request_schema": {}
    },
    "nav.bookmark.list": {
      "request_schema": {}
    },
    "nav.bookmark.remove": {
      "request_schema": {}
    },
    "planet.create": {
      "request_schema": {}
    },
    "planet.deposit": {
      "request_schema": {}
    },
    "planet.list_mine": {
      "request_schema": {}
    },
    "planet.withdraw": {
      "request_schema": {}
    },
    "player.get_settings": {
      "request_schema": {}
    },
    "player.list_online": {
      "request_schema": {}
    },
    "player.my_info": {
      "request_schema": {}
    },
    "player.rankings": {
      "request_schema": {}
    },
    "player.set_prefs": {
      "request_schema": {}
    },
    "research.projects.fund": {
      "request_schema": {}
    },
    "research.projects.list": {
      "request_schema": {}
    },
    "ship.info": {
      "request_schema": {}
    },
    "ship.jettison": {
      "request_schema": {}
    },
    "ship.status": {
      "request_schema": {}
    },
    "ship.upgrade": {
      "request_schema": {}
    },
    "stock.exchange.list_stocks": {
      "request_schema": {}
    },
    "stock.exchange.orders.cancel": {
      "request_schema": {}
    },
    "stock.exchange.orders.create": {
      "request_schema": {}
    },
    "stock.portfolio.list": {
      "request_schema": {}
    },
    "system.capabilities": {
      "request_schema": {}
    },
    "system.describe_schema": {
      "request_schema": {}
    },
    "system.hello": {
      "request_schema": {}
    },
    "trade.buy": {
      "request_schema": {}
    },
    "trade.history": {
      "request_schema": {}
    },
    "trade.port_info": {
      "request_schema": {}
    },
    "trade.quote": {
      "request_schema": {}
    },
    "trade.sell": {
      "request_schema": {}
    }
  },
  "sector_info_fetched_for": {
    "156": 1763121465.1499195,
    "170": 1763121466.1553493,
    "307": 1763121467.1610734
  },
  "last_sector_info_request_time": 1763121466.6603038,
  "last_ship_info_request_time": 1763121464.1469655,
  "last_player_info_request_time": 0,
  "port_info_failures_per_sector": {},
  "sector_snapshot_by_id": {},
  "q_table": {
    "stage:explore-sector_class:unknown_class-port_type:no_port": {
      "sector.info": 0.0,
      "move.warp": 0.0
    },
    "stage:survey-sector_class:unknown_class-port_type:port_present": {
      "trade.port_info": 15.0,
      "trade.quote": 0.0
    },
    "stage:exploit-sector_class:unknown_class-port_type:port_present": {
      "trade.buy": 0.0
    }
  },
  "n_table": {
    "stage:explore-sector_class:unknown_class-port_type:no_port": {
      "sector.info": 3,
      "move.warp": 2
    },
    "stage:survey-sector_class:unknown_class-port_type:port_present": {
      "trade.port_info": 1,
      "trade.quote": 1
    },
    "stage:exploit-sector_class:unknown_class-port_type:port_present": {
      "trade.buy": 3
    }
  },
  "last_reward": 0.0,
  "last_context_key": null,
  "last_action": null,
  "last_stage": null,
  "bug_reported_this_tick": true,
  "player_username": "ai_qa_bot",
  "client_version": "AI_QA_Bot/1.1",
  "current_credits": 10000,
  "previous_credits": 10000,
  "last_responses_history": [
    {
      "id": "srv-ok",
      "reply_to": "72606537-d9f8-4b7a-bb4c-14fa49c192dd",
      "ts": "2025-11-14T11:57:47Z",
      "status": "ok",
      "type": "trade.quote",
      "data": {
        "port_id": 198,
        "commodity": "organics",
        "quantity": 1,
        "buy_price": 155.0,
        "sell_price": 135.0,
        "total_buy_price": 155,
        "total_sell_price": 135
      },
      "error": null,
      "meta": {
        "rate_limit": {
          "limit": 60,
          "remaining": 60,
          "reset": 60,
          "reset_at": 1763121527.663698
        }
      }
    },
    {
      "id": "srv-ok",
      "reply_to": "3ff69b37-50a8-4241-a450-9f70cd8a13e4",
      "ts": "2025-11-14T11:57:47Z",
      "status": "ok",
      "type": "trade.port_info",
      "data": {
        "port": {
          "id": 198,
          "number": 197,
          "name": "Batiredfrid",
          "sector": 307,
          "size": 7,
          "techlevel": 3,
          "ore_on_hand": 5000,
          "organics_on_hand": 5000,
          "equipment_on_hand": 2500,
          "petty_cash": 0,
          "credits": 0,
          "type": 3
        }
      },
      "error": null,
      "meta": {
        "rate_limit": {
          "limit": 60,
          "remaining": 60,
          "reset": 60,
          "reset_at": 1763121528.1668751
        }
      }
    },
    {
      "id": "srv-err",
      "reply_to": "c1b428f4-ed10-402c-b779-b86564a060b0",
      "ts": "2025-11-14T11:57:48Z",
      "status": "error",
      "type": "error",
      "error": {
        "code": 400,
        "message": "idempotency_key required."
      },
      "data": null,
      "meta": {
        "rate_limit": {
          "limit": 60,
          "remaining": 60,
          "reset": 60,
          "reset_at": 1763121528.6692913
        }
      }
    },
    {
      "id": "srv-err",
      "reply_to": "ab34b40b-d41a-4a3f-bbbc-1ef68e00fa06",
      "ts": "2025-11-14T11:57:54Z",
      "status": "error",
      "type": "error",
      "error": {
        "code": 400,
        "message": "idempotency_key required."
      },
      "data": null,
      "meta": {
        "rate_limit": {
          "limit": 60,
          "remaining": 60,
          "reset": 60,
          "reset_at": 1763121535.1485806
        }
      }
    },
    {
      "id": "srv-err",
      "reply_to": "8de6adf4-9605-4a41-9463-fd26ec44f0d9",
      "ts": "2025-11-14T11:58:01Z",
      "status": "error",
      "type": "error",
      "error": {
        "code": 400,
        "message": "idempotency_key required."
      },
      "data": null,
      "meta": {
        "rate_limit": {
          "limit": 60,
          "remaining": 60,
          "reset": 60,
          "reset_at": 1763121541.543108
        }
      }
    }
  ],
  "commands_to_try": [],
  "ship_info": {
    "id": 2,
    "number": 2,
    "name": "QA Bot 1",
    "type": {
      "id": 0,
      "name": "25"
    },
    "holds": 250,
    "fighters": 156,
    "location": {
      "sector_id": 156,
      "sector_name": "El Nath"
    },
    "owner": {
      "id": 4,
      "name": "ai_qa_bot"
    },
    "cargo": {
      "ore": 0,
      "organics": 0,
      "equipment": 0,
      "colonists": 0
    }
  },
  "port_info": {
    "port": {
      "id": 198,
      "number": 197,
      "name": "Batiredfrid",
      "sector": 307,
      "size": 7,
      "techlevel": 3,
      "ore_on_hand": 5000,
      "organics_on_hand": 5000,
      "equipment_on_hand": 2500,
      "petty_cash": 0,
      "credits": 0,
      "type": 3
    }
  },
  "last_bank_info_request_time": 0,
  "price_cache": {
    "307": {
      "198": {
        "organics": {
          "buy": 155.0,
          "sell": 135.0,
          "timestamp": 1763121467.6637044
        }
      }
    }
  },
  "next_allowed": {},
  "server_capabilities": {},
  "last_processed_command_name": "trade.buy",
  "sector_data": {
    "156": {
      "sector_id": 156,
      "name": "El Nath",
      "adjacent": [
        92,
        170
      ],
      "adjacent_count": 2,
      "ports": [],
      "has_port": false,
      "players": [],
      "players_count": 0,
      "beacons": [],
      "beacons_count": 0,
      "planets": [],
      "has_planet": false,
      "planets_count": 0,
      "beacon": null,
      "has_beacon": false,
      "ships": [
        {
          "id": 2,
          "name": "QA Bot 1",
          "type": "Scout Marauder",
          "ship_name": "QA Bot 1",
          "ship_type": "Scout Marauder",
          "owner": "ai_qa_bot"
        }
      ],
      "ships_count": 1,
      "ports_count": 0,
      "counts": {
        "fighters": 0,
        "mines_armid": 0,
        "mines_limpet": 0,
        "mines": 0
      }
    },
    "170": {
      "sector_id": 170,
      "name": "Uncharted Space",
      "adjacent": [
        156,
        307
      ],
      "adjacent_count": 2,
      "ports": [],
      "has_port": false,
      "players": [],
      "players_count": 0,
      "beacons": [],
      "beacons_count": 0,
      "planets": [],
      "has_planet": false,
      "planets_count": 0,
      "beacon": null,
      "has_beacon": false,
      "ships": [
        {
          "id": 2,
          "name": "QA Bot 1",
          "type": "Scout Marauder",
          "ship_name": "QA Bot 1",
          "ship_type": "Scout Marauder",
          "owner": "ai_qa_bot"
        }
      ],
      "ships_count": 1,
      "ports_count": 0,
      "counts": {
        "fighters": 0,
        "mines_armid": 0,
        "mines_limpet": 0,
        "mines": 0
      }
    },
    "307": {
      "sector_id": 307,
      "name": "Uncharted Space",
      "adjacent": [
        170,
        326
      ],
      "adjacent_count": 2,
      "ports": [
        {
          "id": 198,
          "name": "Batiredfrid",
          "type": "3"
        }
      ],
      "has_port": true,
      "players": [],
      "players_count": 0,
      "beacons": [],
      "beacons_count": 0,
      "planets": [],
      "has_planet": false,
      "planets_count": 0,
      "beacon": null,
      "has_beacon": false,
      "ships": [
        {
          "id": 2,
          "name": "QA Bot 1",
          "type": "Scout Marauder",
          "ship_name": "QA Bot 1",
          "ship_type": "Scout Marauder",
          "owner": "ai_qa_bot"
        }
      ],
      "ships_count": 1,
      "ports_count": 1,
      "counts": {
        "fighters": 0,
        "mines_armid": 0,
        "mines_limpet": 0,
        "mines": 0
      }
    }
  },
  "new_sector_discovered": false,
  "trade_successful": false,
  "new_port_discovered": false,
  "rate_limit_info": {
    "limit": 60,
    "remaining": 60,
    "reset": 60,
    "reset_at": 1763121541.543108
  },
  "command_retry_info": {
    "trade.buy": {
      "failures": 3,
      "next_retry_time": 0
    }
  },
  "new_sector_visited_this_tick": true,
  "price_cache_updated_this_tick": true
}
```

## Client Version
AI_QA_Bot/1.1

## Configuration & State at Bug
- Random Seed: N/A
- Bandit Epsilon: N/A
- Current Stage: exploit

## Replay Script Snippet (JSON Commands)
```json
[
  {
    "command": "trade.quote"
  },
  {
    "command": "trade.port_info"
  },
  {
    "command": "trade.buy"
  },
  {
    "command": "trade.buy"
  },
  {
    "command": "trade.buy"
  }
]
```
