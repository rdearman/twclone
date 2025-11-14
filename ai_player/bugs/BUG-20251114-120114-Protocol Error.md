# Bug Report: Protocol Error

## Description
Server returned a protocol error: {'code': 400, 'message': 'idempotency_key required.'}

## Trigger
`trade.buy` in sector 1

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
    "data": "sector.info"
  },
  {
    "type": "response",
    "data": {
      "id": "srv-ok",
      "reply_to": "d9f194ef-f57f-49bd-9ecc-e935e0e56c8c",
      "ts": "2025-11-14T12:01:04Z",
      "status": "ok",
      "type": "sector.info",
      "data": {
        "sector_id": 1,
        "name": "Fedspace 1",
        "adjacent": [
          2,
          3,
          4,
          5,
          6,
          7
        ],
        "adjacent_count": 6,
        "ports": [
          {
            "id": 1,
            "name": "Earth Port",
            "type": "1"
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
        "beacon": "The Federation -- Do Not Dump!",
        "has_beacon": true,
        "ships": [
          {
            "id": 1,
            "name": "Bit Banger",
            "type": "Merchant Cruiser",
            "ship_name": "Bit Banger",
            "ship_type": "Merchant Cruiser",
            "owner": "derelict"
          },
          {
            "id": 2,
            "name": "QA Bot 1",
            "type": "Scout Marauder",
            "ship_name": "QA Bot 1",
            "ship_type": "Scout Marauder",
            "owner": "ai_qa_bot"
          }
        ],
        "ships_count": 2,
        "ports_count": 1,
        "counts": {
          "fighters": 0,
          "mines_armid": 1,
          "mines_limpet": 0,
          "mines": 1
        }
      },
      "error": null,
      "meta": {
        "rate_limit": {
          "limit": 60,
          "remaining": 60,
          "reset": 60,
          "reset_at": 1763121725.2324655
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
      "reply_to": "3d0bf1c9-c821-468d-9894-0746fe3c55c7",
      "ts": "2025-11-14T12:01:05Z",
      "status": "ok",
      "type": "trade.port_info",
      "data": {
        "port": {
          "id": 1,
          "number": 1,
          "name": "Earth Port",
          "sector": 1,
          "size": 10,
          "techlevel": 10,
          "ore_on_hand": 10000,
          "organics_on_hand": 10000,
          "equipment_on_hand": 10000,
          "petty_cash": 0,
          "credits": 0,
          "type": 1
        }
      },
      "error": null,
      "meta": {
        "rate_limit": {
          "limit": 60,
          "remaining": 60,
          "reset": 60,
          "reset_at": 1763121725.7350416
        }
      }
    }
  },
  {
    "type": "command",
    "data": "trade.quote"
  },
  {
    "type": "response",
    "data": {
      "id": "srv-ok",
      "reply_to": "6aaad348-c0cd-44cc-82ae-bd09c0eb9c80",
      "ts": "2025-11-14T12:01:05Z",
      "status": "ok",
      "type": "trade.quote",
      "data": {
        "port_id": 1,
        "commodity": "equipment",
        "quantity": 1,
        "buy_price": 254.0,
        "sell_price": 144.0,
        "total_buy_price": 254,
        "total_sell_price": 144
      },
      "error": null,
      "meta": {
        "rate_limit": {
          "limit": 60,
          "remaining": 60,
          "reset": 60,
          "reset_at": 1763121726.2378612
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
      "reply_to": "065c3f5d-cc5e-44d3-af1f-c6a4830df9f5",
      "ts": "2025-11-14T12:01:06Z",
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
          "reset_at": 1763121726.740513
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
      "reply_to": "f0fc9a09-9c27-48a7-ba42-3f3b4371e42f",
      "ts": "2025-11-14T12:01:13Z",
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
          "reset_at": 1763121734.1925113
        }
      }
    }
  },
  {
    "type": "command",
    "data": {
      "id": "f0fc9a09-9c27-48a7-ba42-3f3b4371e42f",
      "ts": "2025-11-14T12:01:13.691940Z",
      "command": "trade.buy",
      "auth": {
        "session": "2dd0de9b1c408bbaa0cbe4afe5e7a294d1830cd0ac7df3d46fe5c7f972a46816"
      },
      "data": {
        "port_id": 1,
        "items": [
          {
            "commodity": "equipment",
            "quantity": 10
          }
        ]
      },
      "meta": {
        "client_version": "AI_QA_Bot/1.1",
        "idempotency_key": "386db2d8-b62c-4dd4-82aa-ed44a445d7e6"
      },
      "idempotency_key": "386db2d8-b62c-4dd4-82aa-ed44a445d7e6"
    }
  },
  {
    "type": "response",
    "data": {
      "id": "srv-err",
      "reply_to": "f0fc9a09-9c27-48a7-ba42-3f3b4371e42f",
      "ts": "2025-11-14T12:01:13Z",
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
          "reset_at": 1763121734.1925113
        }
      }
    }
  }
]
```

## Last Commands History
```json
[
  "sector.info",
  "trade.port_info",
  "trade.quote",
  "trade.buy",
  "trade.buy"
]
```

## Last Responses History
```json
[
  {
    "id": "srv-ok",
    "reply_to": "d9f194ef-f57f-49bd-9ecc-e935e0e56c8c",
    "ts": "2025-11-14T12:01:04Z",
    "status": "ok",
    "type": "sector.info",
    "data": {
      "sector_id": 1,
      "name": "Fedspace 1",
      "adjacent": [
        2,
        3,
        4,
        5,
        6,
        7
      ],
      "adjacent_count": 6,
      "ports": [
        {
          "id": 1,
          "name": "Earth Port",
          "type": "1"
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
      "beacon": "The Federation -- Do Not Dump!",
      "has_beacon": true,
      "ships": [
        {
          "id": 1,
          "name": "Bit Banger",
          "type": "Merchant Cruiser",
          "ship_name": "Bit Banger",
          "ship_type": "Merchant Cruiser",
          "owner": "derelict"
        },
        {
          "id": 2,
          "name": "QA Bot 1",
          "type": "Scout Marauder",
          "ship_name": "QA Bot 1",
          "ship_type": "Scout Marauder",
          "owner": "ai_qa_bot"
        }
      ],
      "ships_count": 2,
      "ports_count": 1,
      "counts": {
        "fighters": 0,
        "mines_armid": 1,
        "mines_limpet": 0,
        "mines": 1
      }
    },
    "error": null,
    "meta": {
      "rate_limit": {
        "limit": 60,
        "remaining": 60,
        "reset": 60,
        "reset_at": 1763121725.2324655
      }
    }
  },
  {
    "id": "srv-ok",
    "reply_to": "3d0bf1c9-c821-468d-9894-0746fe3c55c7",
    "ts": "2025-11-14T12:01:05Z",
    "status": "ok",
    "type": "trade.port_info",
    "data": {
      "port": {
        "id": 1,
        "number": 1,
        "name": "Earth Port",
        "sector": 1,
        "size": 10,
        "techlevel": 10,
        "ore_on_hand": 10000,
        "organics_on_hand": 10000,
        "equipment_on_hand": 10000,
        "petty_cash": 0,
        "credits": 0,
        "type": 1
      }
    },
    "error": null,
    "meta": {
      "rate_limit": {
        "limit": 60,
        "remaining": 60,
        "reset": 60,
        "reset_at": 1763121725.7350416
      }
    }
  },
  {
    "id": "srv-ok",
    "reply_to": "6aaad348-c0cd-44cc-82ae-bd09c0eb9c80",
    "ts": "2025-11-14T12:01:05Z",
    "status": "ok",
    "type": "trade.quote",
    "data": {
      "port_id": 1,
      "commodity": "equipment",
      "quantity": 1,
      "buy_price": 254.0,
      "sell_price": 144.0,
      "total_buy_price": 254,
      "total_sell_price": 144
    },
    "error": null,
    "meta": {
      "rate_limit": {
        "limit": 60,
        "remaining": 60,
        "reset": 60,
        "reset_at": 1763121726.2378612
      }
    }
  },
  {
    "id": "srv-err",
    "reply_to": "065c3f5d-cc5e-44d3-af1f-c6a4830df9f5",
    "ts": "2025-11-14T12:01:06Z",
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
        "reset_at": 1763121726.740513
      }
    }
  },
  {
    "id": "srv-err",
    "reply_to": "f0fc9a09-9c27-48a7-ba42-3f3b4371e42f",
    "ts": "2025-11-14T12:01:13Z",
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
        "reset_at": 1763121734.1925113
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
  "session_token": "2dd0de9b1c408bbaa0cbe4afe5e7a294d1830cd0ac7df3d46fe5c7f972a46816",
  "player_location_sector": 1,
  "last_server_response": {
    "id": "srv-err",
    "reply_to": "f0fc9a09-9c27-48a7-ba42-3f3b4371e42f",
    "ts": "2025-11-14T12:01:13Z",
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
        "reset_at": 1763121734.1925113
      }
    }
  },
  "last_command_sent_id": "f0fc9a09-9c27-48a7-ba42-3f3b4371e42f",
  "pending_commands": {},
  "last_commands_history": [
    "sector.info",
    "trade.port_info",
    "trade.quote",
    "trade.buy",
    "trade.buy"
  ],
  "working_commands": [],
  "broken_commands": [],
  "received_responses": [],
  "visited_sectors": [
    1
  ],
  "visited_ports": [
    1
  ],
  "previous_sector_id": null,
  "sectors_with_info": [
    1
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
    "1": 1763121665.2324708
  },
  "last_sector_info_request_time": 1763121664.731811,
  "last_ship_info_request_time": 1763121664.2296696,
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
      "trade.quote": 1
    },
    "stage:exploit-sector_class:unknown_class-port_type:port_present": {
      "trade.buy": 1
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
      "reply_to": "d9f194ef-f57f-49bd-9ecc-e935e0e56c8c",
      "ts": "2025-11-14T12:01:04Z",
      "status": "ok",
      "type": "sector.info",
      "data": {
        "sector_id": 1,
        "name": "Fedspace 1",
        "adjacent": [
          2,
          3,
          4,
          5,
          6,
          7
        ],
        "adjacent_count": 6,
        "ports": [
          {
            "id": 1,
            "name": "Earth Port",
            "type": "1"
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
        "beacon": "The Federation -- Do Not Dump!",
        "has_beacon": true,
        "ships": [
          {
            "id": 1,
            "name": "Bit Banger",
            "type": "Merchant Cruiser",
            "ship_name": "Bit Banger",
            "ship_type": "Merchant Cruiser",
            "owner": "derelict"
          },
          {
            "id": 2,
            "name": "QA Bot 1",
            "type": "Scout Marauder",
            "ship_name": "QA Bot 1",
            "ship_type": "Scout Marauder",
            "owner": "ai_qa_bot"
          }
        ],
        "ships_count": 2,
        "ports_count": 1,
        "counts": {
          "fighters": 0,
          "mines_armid": 1,
          "mines_limpet": 0,
          "mines": 1
        }
      },
      "error": null,
      "meta": {
        "rate_limit": {
          "limit": 60,
          "remaining": 60,
          "reset": 60,
          "reset_at": 1763121725.2324655
        }
      }
    },
    {
      "id": "srv-ok",
      "reply_to": "3d0bf1c9-c821-468d-9894-0746fe3c55c7",
      "ts": "2025-11-14T12:01:05Z",
      "status": "ok",
      "type": "trade.port_info",
      "data": {
        "port": {
          "id": 1,
          "number": 1,
          "name": "Earth Port",
          "sector": 1,
          "size": 10,
          "techlevel": 10,
          "ore_on_hand": 10000,
          "organics_on_hand": 10000,
          "equipment_on_hand": 10000,
          "petty_cash": 0,
          "credits": 0,
          "type": 1
        }
      },
      "error": null,
      "meta": {
        "rate_limit": {
          "limit": 60,
          "remaining": 60,
          "reset": 60,
          "reset_at": 1763121725.7350416
        }
      }
    },
    {
      "id": "srv-ok",
      "reply_to": "6aaad348-c0cd-44cc-82ae-bd09c0eb9c80",
      "ts": "2025-11-14T12:01:05Z",
      "status": "ok",
      "type": "trade.quote",
      "data": {
        "port_id": 1,
        "commodity": "equipment",
        "quantity": 1,
        "buy_price": 254.0,
        "sell_price": 144.0,
        "total_buy_price": 254,
        "total_sell_price": 144
      },
      "error": null,
      "meta": {
        "rate_limit": {
          "limit": 60,
          "remaining": 60,
          "reset": 60,
          "reset_at": 1763121726.2378612
        }
      }
    },
    {
      "id": "srv-err",
      "reply_to": "065c3f5d-cc5e-44d3-af1f-c6a4830df9f5",
      "ts": "2025-11-14T12:01:06Z",
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
          "reset_at": 1763121726.740513
        }
      }
    },
    {
      "id": "srv-err",
      "reply_to": "f0fc9a09-9c27-48a7-ba42-3f3b4371e42f",
      "ts": "2025-11-14T12:01:13Z",
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
          "reset_at": 1763121734.1925113
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
    "fighters": 1,
    "location": {
      "sector_id": 1,
      "sector_name": "Fedspace 1"
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
      "id": 1,
      "number": 1,
      "name": "Earth Port",
      "sector": 1,
      "size": 10,
      "techlevel": 10,
      "ore_on_hand": 10000,
      "organics_on_hand": 10000,
      "equipment_on_hand": 10000,
      "petty_cash": 0,
      "credits": 0,
      "type": 1
    }
  },
  "last_bank_info_request_time": 0,
  "price_cache": {
    "1": {
      "1": {
        "equipment": {
          "buy": 254.0,
          "sell": 144.0,
          "timestamp": 1763121666.2378662
        }
      }
    }
  },
  "next_allowed": {},
  "server_capabilities": {},
  "last_processed_command_name": "trade.buy",
  "sector_data": {
    "1": {
      "sector_id": 1,
      "name": "Fedspace 1",
      "adjacent": [
        2,
        3,
        4,
        5,
        6,
        7
      ],
      "adjacent_count": 6,
      "ports": [
        {
          "id": 1,
          "name": "Earth Port",
          "type": "1"
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
      "beacon": "The Federation -- Do Not Dump!",
      "has_beacon": true,
      "ships": [
        {
          "id": 1,
          "name": "Bit Banger",
          "type": "Merchant Cruiser",
          "ship_name": "Bit Banger",
          "ship_type": "Merchant Cruiser",
          "owner": "derelict"
        },
        {
          "id": 2,
          "name": "QA Bot 1",
          "type": "Scout Marauder",
          "ship_name": "QA Bot 1",
          "ship_type": "Scout Marauder",
          "owner": "ai_qa_bot"
        }
      ],
      "ships_count": 2,
      "ports_count": 1,
      "counts": {
        "fighters": 0,
        "mines_armid": 1,
        "mines_limpet": 0,
        "mines": 1
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
    "reset_at": 1763121734.1925113
  },
  "command_retry_info": {
    "trade.buy": {
      "failures": 1,
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
      "port_id": 1,
      "items": [
        {
          "commodity": "equipment",
          "quantity": 10
        }
      ]
    },
    "meta": {
      "client_version": "AI_QA_Bot/1.1",
      "idempotency_key": "386db2d8-b62c-4dd4-82aa-ed44a445d7e6"
    },
    "idempotency_key": "386db2d8-b62c-4dd4-82aa-ed44a445d7e6"
  }
]
```
