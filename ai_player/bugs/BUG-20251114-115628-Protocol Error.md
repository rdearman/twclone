# Bug Report: Protocol Error

## Description
Server returned a protocol error: {'code': 400, 'message': 'idempotency_key required.'}

## Trigger
`trade.buy` in sector 424

## Expected
Valid response for trade.buy

## Actual
Error: {'code': 400, 'message': 'idempotency_key required.'}

## Reproducer Steps
1. Connect (system.hello)
2. auth.login as ai_qa_bot
3. ... (steps leading to the bug, derived from frames)
N/A

## Frames (Last 3 Request/Response)
```json
[
  {
    "type": "command",
    "data": "trade.quote"
  },
  {
    "type": "response",
    "data": {
      "id": "srv-ok",
      "reply_to": "796ccf0c-1ffe-4794-93e1-92aefbce8d91",
      "ts": "2025-11-14T11:56:14Z",
      "status": "ok",
      "type": "trade.quote",
      "data": {
        "port_id": 68,
        "commodity": "organics",
        "quantity": 1,
        "buy_price": 212.0,
        "sell_price": 142.0,
        "total_buy_price": 212,
        "total_sell_price": 142
      },
      "error": null,
      "meta": {
        "rate_limit": {
          "limit": 60,
          "remaining": 60,
          "reset": 60,
          "reset_at": 1763121434.685371
        }
      }
    }
  },
  {
    "type": "command",
    "data": "trade.port_info"
  },
  {
    "type": "response",
    "data": {
      "id": "srv-ok",
      "reply_to": "7011f45b-eda5-419e-ae8e-3c66153fee55",
      "ts": "2025-11-14T11:56:14Z",
      "status": "ok",
      "type": "trade.port_info",
      "data": {
        "port": {
          "id": 68,
          "number": 67,
          "name": "Piawan",
          "sector": 424,
          "size": 6,
          "techlevel": 5,
          "ore_on_hand": 5000,
          "organics_on_hand": 2500,
          "equipment_on_hand": 5000,
          "petty_cash": 0,
          "credits": 0,
          "type": 7
        }
      },
      "error": null,
      "meta": {
        "rate_limit": {
          "limit": 60,
          "remaining": 60,
          "reset": 60,
          "reset_at": 1763121435.1882486
        }
      }
    }
  },
  {
    "type": "command",
    "data": "trade.buy"
  },
  {
    "type": "response",
    "data": {
      "id": "srv-err",
      "reply_to": "e67d4fd5-5563-4f06-b107-1f029a8448c8",
      "ts": "2025-11-14T11:56:15Z",
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
          "reset_at": 1763121435.6911814
        }
      }
    }
  },
  {
    "type": "command",
    "data": "trade.buy"
  },
  {
    "type": "response",
    "data": {
      "id": "srv-err",
      "reply_to": "602d1951-664c-44a2-a82e-6c50df2de5f4",
      "ts": "2025-11-14T11:56:20Z",
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
          "reset_at": 1763121441.1756387
        }
      }
    }
  },
  {
    "type": "command",
    "data": "trade.buy"
  },
  {
    "type": "response",
    "data": {
      "id": "srv-err",
      "reply_to": "81f179c2-5b20-48be-9b29-415389ec8700",
      "ts": "2025-11-14T11:56:28Z",
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
          "reset_at": 1763121448.5280447
        }
      }
    }
  },
  {
    "type": "command",
    "data": {
      "id": "81f179c2-5b20-48be-9b29-415389ec8700",
      "ts": "2025-11-14T11:56:28.027481Z",
      "command": "trade.buy",
      "auth": {
        "session": "2afbf66dfbd9ea0493171145f5b16e42f1bc5d5102ba648d6871364f3234f61d"
      },
      "data": {
        "port_id": 68,
        "items": [
          {
            "commodity": "organics",
            "quantity": 10
          }
        ]
      },
      "meta": {
        "client_version": "AI_QA_Bot/1.1",
        "idempotency_key": "2320812a-8084-4642-9b8b-30b2e936cfb2"
      },
      "idempotency_key": "2320812a-8084-4642-9b8b-30b2e936cfb2"
    }
  },
  {
    "type": "response",
    "data": {
      "id": "srv-err",
      "reply_to": "81f179c2-5b20-48be-9b29-415389ec8700",
      "ts": "2025-11-14T11:56:28Z",
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
          "reset_at": 1763121448.5280447
        }
      }
    }
  }
]
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
    "reply_to": "796ccf0c-1ffe-4794-93e1-92aefbce8d91",
    "ts": "2025-11-14T11:56:14Z",
    "status": "ok",
    "type": "trade.quote",
    "data": {
      "port_id": 68,
      "commodity": "organics",
      "quantity": 1,
      "buy_price": 212.0,
      "sell_price": 142.0,
      "total_buy_price": 212,
      "total_sell_price": 142
    },
    "error": null,
    "meta": {
      "rate_limit": {
        "limit": 60,
        "remaining": 60,
        "reset": 60,
        "reset_at": 1763121434.685371
      }
    }
  },
  {
    "id": "srv-ok",
    "reply_to": "7011f45b-eda5-419e-ae8e-3c66153fee55",
    "ts": "2025-11-14T11:56:14Z",
    "status": "ok",
    "type": "trade.port_info",
    "data": {
      "port": {
        "id": 68,
        "number": 67,
        "name": "Piawan",
        "sector": 424,
        "size": 6,
        "techlevel": 5,
        "ore_on_hand": 5000,
        "organics_on_hand": 2500,
        "equipment_on_hand": 5000,
        "petty_cash": 0,
        "credits": 0,
        "type": 7
      }
    },
    "error": null,
    "meta": {
      "rate_limit": {
        "limit": 60,
        "remaining": 60,
        "reset": 60,
        "reset_at": 1763121435.1882486
      }
    }
  },
  {
    "id": "srv-err",
    "reply_to": "e67d4fd5-5563-4f06-b107-1f029a8448c8",
    "ts": "2025-11-14T11:56:15Z",
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
        "reset_at": 1763121435.6911814
      }
    }
  },
  {
    "id": "srv-err",
    "reply_to": "602d1951-664c-44a2-a82e-6c50df2de5f4",
    "ts": "2025-11-14T11:56:20Z",
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
        "reset_at": 1763121441.1756387
      }
    }
  },
  {
    "id": "srv-err",
    "reply_to": "81f179c2-5b20-48be-9b29-415389ec8700",
    "ts": "2025-11-14T11:56:28Z",
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
        "reset_at": 1763121448.5280447
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
  "session_token": "2afbf66dfbd9ea0493171145f5b16e42f1bc5d5102ba648d6871364f3234f61d",
  "player_location_sector": 424,
  "last_server_response": {
    "id": "srv-err",
    "reply_to": "81f179c2-5b20-48be-9b29-415389ec8700",
    "ts": "2025-11-14T11:56:28Z",
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
        "reset_at": 1763121448.5280447
      }
    }
  },
  "last_command_sent_id": "81f179c2-5b20-48be-9b29-415389ec8700",
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
    424
  ],
  "visited_ports": [
    68
  ],
  "previous_sector_id": null,
  "sectors_with_info": [
    424
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
    "424": 1763121373.1772974
  },
  "last_sector_info_request_time": 1763121372.676633,
  "last_ship_info_request_time": 1763121372.174605,
  "last_player_info_request_time": 0,
  "port_info_failures_per_sector": {},
  "sector_snapshot_by_id": {},
  "q_table": {
    "stage:explore-sector_class:unknown_class-port_type:no_port": {
      "sector.info": 0.0
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
      "sector.info": 1
    },
    "stage:survey-sector_class:unknown_class-port_type:port_present": {
      "trade.port_info": 1,
      "trade.quote": 3
    },
    "stage:exploit-sector_class:unknown_class-port_type:port_present": {
      "trade.buy": 2
    }
  },
  "last_reward": 0.0,
  "last_context_key": "stage:exploit-sector_class:unknown_class-port_type:port_present",
  "last_action": "trade.buy",
  "last_stage": "exploit",
  "bug_reported_this_tick": true,
  "player_username": "ai_qa_bot",
  "client_version": "AI_QA_Bot/1.1",
  "current_credits": 10000,
  "previous_credits": 10000,
  "last_responses_history": [
    {
      "id": "srv-ok",
      "reply_to": "796ccf0c-1ffe-4794-93e1-92aefbce8d91",
      "ts": "2025-11-14T11:56:14Z",
      "status": "ok",
      "type": "trade.quote",
      "data": {
        "port_id": 68,
        "commodity": "organics",
        "quantity": 1,
        "buy_price": 212.0,
        "sell_price": 142.0,
        "total_buy_price": 212,
        "total_sell_price": 142
      },
      "error": null,
      "meta": {
        "rate_limit": {
          "limit": 60,
          "remaining": 60,
          "reset": 60,
          "reset_at": 1763121434.685371
        }
      }
    },
    {
      "id": "srv-ok",
      "reply_to": "7011f45b-eda5-419e-ae8e-3c66153fee55",
      "ts": "2025-11-14T11:56:14Z",
      "status": "ok",
      "type": "trade.port_info",
      "data": {
        "port": {
          "id": 68,
          "number": 67,
          "name": "Piawan",
          "sector": 424,
          "size": 6,
          "techlevel": 5,
          "ore_on_hand": 5000,
          "organics_on_hand": 2500,
          "equipment_on_hand": 5000,
          "petty_cash": 0,
          "credits": 0,
          "type": 7
        }
      },
      "error": null,
      "meta": {
        "rate_limit": {
          "limit": 60,
          "remaining": 60,
          "reset": 60,
          "reset_at": 1763121435.1882486
        }
      }
    },
    {
      "id": "srv-err",
      "reply_to": "e67d4fd5-5563-4f06-b107-1f029a8448c8",
      "ts": "2025-11-14T11:56:15Z",
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
          "reset_at": 1763121435.6911814
        }
      }
    },
    {
      "id": "srv-err",
      "reply_to": "602d1951-664c-44a2-a82e-6c50df2de5f4",
      "ts": "2025-11-14T11:56:20Z",
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
          "reset_at": 1763121441.1756387
        }
      }
    },
    {
      "id": "srv-err",
      "reply_to": "81f179c2-5b20-48be-9b29-415389ec8700",
      "ts": "2025-11-14T11:56:28Z",
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
          "reset_at": 1763121448.5280447
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
    "fighters": 424,
    "location": {
      "sector_id": 424,
      "sector_name": "Uncharted Space"
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
      "id": 68,
      "number": 67,
      "name": "Piawan",
      "sector": 424,
      "size": 6,
      "techlevel": 5,
      "ore_on_hand": 5000,
      "organics_on_hand": 2500,
      "equipment_on_hand": 5000,
      "petty_cash": 0,
      "credits": 0,
      "type": 7
    }
  },
  "last_bank_info_request_time": 0,
  "price_cache": {
    "424": {
      "68": {
        "organics": {
          "buy": 212.0,
          "sell": 142.0,
          "timestamp": 1763121374.6853771
        },
        "ore": {
          "buy": 112.0,
          "sell": 86.0,
          "timestamp": 1763121374.1825309
        }
      }
    }
  },
  "next_allowed": {},
  "server_capabilities": {},
  "last_processed_command_name": "trade.buy",
  "sector_data": {
    "424": {
      "sector_id": 424,
      "name": "Uncharted Space",
      "adjacent": [
        39,
        230
      ],
      "adjacent_count": 2,
      "ports": [
        {
          "id": 68,
          "name": "Piawan",
          "type": "7"
        },
        {
          "id": 157,
          "name": "Zaobard",
          "type": "4"
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
      "ports_count": 2,
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
    "reset_at": 1763121448.5280447
  },
  "command_retry_info": {
    "trade.buy": {
      "failures": 2,
      "next_retry_time": 0
    }
  },
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
  {},
  {},
  {},
  {},
  {},
  {
    "command": "trade.buy",
    "data": {
      "port_id": 68,
      "items": [
        {
          "commodity": "organics",
          "quantity": 10
        }
      ]
    },
    "meta": {
      "client_version": "AI_QA_Bot/1.1",
      "idempotency_key": "2320812a-8084-4642-9b8b-30b2e936cfb2"
    },
    "idempotency_key": "2320812a-8084-4642-9b8b-30b2e936cfb2"
  }
]
```
