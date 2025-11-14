# Bug Report: Invariant Failure: Command Exceeded Max Retries

## Description
Command 'trade.buy' failed 3 times.

## Trigger
`N/A` in sector 424

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
  "trade.port_info",
  "trade.buy",
  "trade.buy",
  "trade.buy",
  "trade.buy"
]
```

## Last Responses History
```json
[
  {
    "id": "srv-err",
    "reply_to": "90a08c27-ed2a-4fbb-8e37-35694dca0325",
    "ts": "2025-11-14T11:44:59Z",
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
        "reset_at": 1763120759.9120643
      }
    }
  },
  {
    "id": "srv-ok",
    "reply_to": "534455f2-d5bd-47df-8ff2-c782847002ca",
    "ts": "2025-11-14T11:46:22Z",
    "status": "ok",
    "type": "session.hello",
    "data": {
      "protocol_version": "1.0",
      "server_time_unix": 1763120782,
      "authenticated": false,
      "player_id": null,
      "current_sector": null,
      "server_time": "2025-11-14T11:46:22Z"
    },
    "error": null,
    "meta": {
      "rate_limit": {
        "limit": 60,
        "remaining": 60,
        "reset": 60,
        "reset_at": 1763120842.0301023
      }
    }
  },
  {
    "id": "srv-err",
    "reply_to": "cff74bab-4d2c-4045-abd3-083b73cafe6e",
    "ts": "2025-11-14T11:46:28Z",
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
        "reset_at": 1763120849.4355335
      }
    }
  },
  {
    "id": "srv-err",
    "reply_to": "f2f87f5a-68f9-427b-8a04-595a1e0f6ea7",
    "ts": "2025-11-14T11:46:36Z",
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
        "reset_at": 1763120856.8573272
      }
    }
  },
  {
    "id": "srv-err",
    "reply_to": "934bcb9f-946f-41ce-9dcd-b1d3f76cfc3f",
    "ts": "2025-11-14T11:46:41Z",
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
        "reset_at": 1763120862.2083302
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
  "session_token": "5634301b3597ff9e9515cb8a7cbc72468d7ffdc06103fb071b16c18ee874fb0f",
  "player_location_sector": 424,
  "last_server_response": {
    "id": "srv-err",
    "reply_to": "934bcb9f-946f-41ce-9dcd-b1d3f76cfc3f",
    "ts": "2025-11-14T11:46:41Z",
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
        "reset_at": 1763120862.2083302
      }
    }
  },
  "last_command_sent_id": "934bcb9f-946f-41ce-9dcd-b1d3f76cfc3f",
  "pending_commands": {},
  "last_commands_history": [
    "trade.port_info",
    "trade.buy",
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
    "424": 1763116320.5227215
  },
  "last_sector_info_request_time": 1763116320.0220451,
  "last_ship_info_request_time": 1763116319.5198574,
  "last_player_info_request_time": 0,
  "port_info_failures_per_sector": {},
  "sector_snapshot_by_id": {},
  "q_table": {
    "stage:explore-sector_class:unknown_class-port_type:no_port": {
      "sector.info": 0.0
    },
    "stage:survey-sector_class:unknown_class-port_type:port_present": {
      "trade.quote": 0.0,
      "trade.port_info": 15.0
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
      "trade.quote": 41,
      "trade.port_info": 1
    },
    "stage:exploit-sector_class:unknown_class-port_type:port_present": {
      "trade.buy": 6
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
      "id": "srv-err",
      "reply_to": "90a08c27-ed2a-4fbb-8e37-35694dca0325",
      "ts": "2025-11-14T11:44:59Z",
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
          "reset_at": 1763120759.9120643
        }
      }
    },
    {
      "id": "srv-ok",
      "reply_to": "534455f2-d5bd-47df-8ff2-c782847002ca",
      "ts": "2025-11-14T11:46:22Z",
      "status": "ok",
      "type": "session.hello",
      "data": {
        "protocol_version": "1.0",
        "server_time_unix": 1763120782,
        "authenticated": false,
        "player_id": null,
        "current_sector": null,
        "server_time": "2025-11-14T11:46:22Z"
      },
      "error": null,
      "meta": {
        "rate_limit": {
          "limit": 60,
          "remaining": 60,
          "reset": 60,
          "reset_at": 1763120842.0301023
        }
      }
    },
    {
      "id": "srv-err",
      "reply_to": "cff74bab-4d2c-4045-abd3-083b73cafe6e",
      "ts": "2025-11-14T11:46:28Z",
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
          "reset_at": 1763120849.4355335
        }
      }
    },
    {
      "id": "srv-err",
      "reply_to": "f2f87f5a-68f9-427b-8a04-595a1e0f6ea7",
      "ts": "2025-11-14T11:46:36Z",
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
          "reset_at": 1763120856.8573272
        }
      }
    },
    {
      "id": "srv-err",
      "reply_to": "934bcb9f-946f-41ce-9dcd-b1d3f76cfc3f",
      "ts": "2025-11-14T11:46:41Z",
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
          "reset_at": 1763120862.2083302
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
          "timestamp": 1763116339.6294694
        },
        "ore": {
          "buy": 112.0,
          "sell": 86.0,
          "timestamp": 1763116340.1332195
        },
        "equipment": {
          "buy": 220.0,
          "sell": 169.0,
          "timestamp": 1763120686.4867215
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
    "reset_at": 1763120862.2083302
  },
  "command_retry_info": {
    "trade.buy": {
      "failures": 3,
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
  },
  {
    "command": "trade.buy"
  }
]
```
