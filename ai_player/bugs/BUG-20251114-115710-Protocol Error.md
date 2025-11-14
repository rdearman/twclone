# Bug Report: Protocol Error

## Description
Server returned a protocol error: {'code': 1402, 'message': 'No warp link', 'data': {'from': 1, 'to': 170, 'reason': 'no_warp_link'}}

## Trigger
`move.warp` in sector 156

## Expected
Valid response for move.warp

## Actual
Error: {'code': 1402, 'message': 'No warp link', 'data': {'from': 1, 'to': 170, 'reason': 'no_warp_link'}}

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
    "data": "move.warp"
  },
  {
    "type": "response",
    "data": {
      "id": "srv-ok",
      "reply_to": "334c3fbf-e62a-4d3c-8218-0f7a44dee855",
      "ts": "2025-11-14T11:57:07Z",
      "status": "ok",
      "type": "session.hello",
      "data": {
        "protocol_version": "1.0",
        "server_time_unix": 1763121427,
        "authenticated": false,
        "player_id": null,
        "current_sector": null,
        "server_time": "2025-11-14T11:57:07Z"
      },
      "error": null,
      "meta": {
        "rate_limit": {
          "limit": 60,
          "remaining": 60,
          "reset": 60,
          "reset_at": 1763121488.487689
        }
      }
    }
  },
  {
    "type": "command",
    "data": "sector.info"
  },
  {
    "type": "response",
    "data": {
      "id": "srv-ok",
      "reply_to": "ed7366d0-fe3e-48c7-b609-ecdb6f0730dd",
      "ts": "2025-11-14T11:57:08Z",
      "status": "ok",
      "type": "sector.info",
      "data": {
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
      "error": null,
      "meta": {
        "rate_limit": {
          "limit": 60,
          "remaining": 60,
          "reset": 60,
          "reset_at": 1763121488.9897652
        }
      }
    }
  },
  {
    "type": "command",
    "data": "move.warp"
  },
  {
    "type": "response",
    "data": {
      "id": "srv-refuse",
      "reply_to": "f151c86c-c802-4c52-b552-df08ac41f083",
      "ts": "2025-11-14T11:57:08Z",
      "status": "refused",
      "type": "error",
      "error": {
        "code": 1402,
        "message": "No warp link",
        "data": {
          "from": 1,
          "to": 170,
          "reason": "no_warp_link"
        }
      },
      "data": null,
      "meta": {
        "rate_limit": {
          "limit": 60,
          "remaining": 60,
          "reset": 60,
          "reset_at": 1763121489.4923584
        }
      }
    }
  },
  {
    "type": "command",
    "data": "move.warp"
  },
  {
    "type": "response",
    "data": {
      "id": "srv-refuse",
      "reply_to": "3376ab9e-e617-434f-96da-c4e6d835b4d7",
      "ts": "2025-11-14T11:57:09Z",
      "status": "refused",
      "type": "error",
      "error": {
        "code": 1402,
        "message": "No warp link",
        "data": {
          "from": 1,
          "to": 170,
          "reason": "no_warp_link"
        }
      },
      "data": null,
      "meta": {
        "rate_limit": {
          "limit": 60,
          "remaining": 60,
          "reset": 60,
          "reset_at": 1763121489.998623
        }
      }
    }
  },
  {
    "type": "command",
    "data": "move.warp"
  },
  {
    "type": "response",
    "data": {
      "id": "srv-refuse",
      "reply_to": "037f358e-80de-4611-b28b-43055ce1172f",
      "ts": "2025-11-14T11:57:10Z",
      "status": "refused",
      "type": "error",
      "error": {
        "code": 1402,
        "message": "No warp link",
        "data": {
          "from": 1,
          "to": 170,
          "reason": "no_warp_link"
        }
      },
      "data": null,
      "meta": {
        "rate_limit": {
          "limit": 60,
          "remaining": 60,
          "reset": 60,
          "reset_at": 1763121490.5055797
        }
      }
    }
  },
  {
    "type": "command",
    "data": {
      "id": "037f358e-80de-4611-b28b-43055ce1172f",
      "ts": "2025-11-14T11:57:10.004891Z",
      "command": "move.warp",
      "auth": {
        "session": "2afbf66dfbd9ea0493171145f5b16e42f1bc5d5102ba648d6871364f3234f61d"
      },
      "data": {
        "to_sector_id": 170,
        "ship_id": 2
      },
      "meta": {
        "client_version": "AI_QA_Bot/1.1",
        "idempotency_key": "ffcb5765-3086-49a4-904f-122784eeaf98"
      },
      "idempotency_key": "ffcb5765-3086-49a4-904f-122784eeaf98"
    }
  },
  {
    "type": "response",
    "data": {
      "id": "srv-refuse",
      "reply_to": "037f358e-80de-4611-b28b-43055ce1172f",
      "ts": "2025-11-14T11:57:10Z",
      "status": "refused",
      "type": "error",
      "error": {
        "code": 1402,
        "message": "No warp link",
        "data": {
          "from": 1,
          "to": 170,
          "reason": "no_warp_link"
        }
      },
      "data": null,
      "meta": {
        "rate_limit": {
          "limit": 60,
          "remaining": 60,
          "reset": 60,
          "reset_at": 1763121490.5055797
        }
      }
    }
  }
]
```

## Last Commands History
```json
[
  "move.warp",
  "sector.info",
  "move.warp",
  "move.warp",
  "move.warp"
]
```

## Last Responses History
```json
[
  {
    "id": "srv-ok",
    "reply_to": "334c3fbf-e62a-4d3c-8218-0f7a44dee855",
    "ts": "2025-11-14T11:57:07Z",
    "status": "ok",
    "type": "session.hello",
    "data": {
      "protocol_version": "1.0",
      "server_time_unix": 1763121427,
      "authenticated": false,
      "player_id": null,
      "current_sector": null,
      "server_time": "2025-11-14T11:57:07Z"
    },
    "error": null,
    "meta": {
      "rate_limit": {
        "limit": 60,
        "remaining": 60,
        "reset": 60,
        "reset_at": 1763121488.487689
      }
    }
  },
  {
    "id": "srv-ok",
    "reply_to": "ed7366d0-fe3e-48c7-b609-ecdb6f0730dd",
    "ts": "2025-11-14T11:57:08Z",
    "status": "ok",
    "type": "sector.info",
    "data": {
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
    "error": null,
    "meta": {
      "rate_limit": {
        "limit": 60,
        "remaining": 60,
        "reset": 60,
        "reset_at": 1763121488.9897652
      }
    }
  },
  {
    "id": "srv-refuse",
    "reply_to": "f151c86c-c802-4c52-b552-df08ac41f083",
    "ts": "2025-11-14T11:57:08Z",
    "status": "refused",
    "type": "error",
    "error": {
      "code": 1402,
      "message": "No warp link",
      "data": {
        "from": 1,
        "to": 170,
        "reason": "no_warp_link"
      }
    },
    "data": null,
    "meta": {
      "rate_limit": {
        "limit": 60,
        "remaining": 60,
        "reset": 60,
        "reset_at": 1763121489.4923584
      }
    }
  },
  {
    "id": "srv-refuse",
    "reply_to": "3376ab9e-e617-434f-96da-c4e6d835b4d7",
    "ts": "2025-11-14T11:57:09Z",
    "status": "refused",
    "type": "error",
    "error": {
      "code": 1402,
      "message": "No warp link",
      "data": {
        "from": 1,
        "to": 170,
        "reason": "no_warp_link"
      }
    },
    "data": null,
    "meta": {
      "rate_limit": {
        "limit": 60,
        "remaining": 60,
        "reset": 60,
        "reset_at": 1763121489.998623
      }
    }
  },
  {
    "id": "srv-refuse",
    "reply_to": "037f358e-80de-4611-b28b-43055ce1172f",
    "ts": "2025-11-14T11:57:10Z",
    "status": "refused",
    "type": "error",
    "error": {
      "code": 1402,
      "message": "No warp link",
      "data": {
        "from": 1,
        "to": 170,
        "reason": "no_warp_link"
      }
    },
    "data": null,
    "meta": {
      "rate_limit": {
        "limit": 60,
        "remaining": 60,
        "reset": 60,
        "reset_at": 1763121490.5055797
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
  "stage": "explore",
  "session_token": "2afbf66dfbd9ea0493171145f5b16e42f1bc5d5102ba648d6871364f3234f61d",
  "player_location_sector": 156,
  "last_server_response": {
    "id": "srv-refuse",
    "reply_to": "037f358e-80de-4611-b28b-43055ce1172f",
    "ts": "2025-11-14T11:57:10Z",
    "status": "refused",
    "type": "error",
    "error": {
      "code": 1402,
      "message": "No warp link",
      "data": {
        "from": 1,
        "to": 170,
        "reason": "no_warp_link"
      }
    },
    "data": null,
    "meta": {
      "rate_limit": {
        "limit": 60,
        "remaining": 60,
        "reset": 60,
        "reset_at": 1763121490.5055797
      }
    }
  },
  "last_command_sent_id": "037f358e-80de-4611-b28b-43055ce1172f",
  "pending_commands": {},
  "last_commands_history": [
    "move.warp",
    "sector.info",
    "move.warp",
    "move.warp",
    "move.warp"
  ],
  "working_commands": [],
  "broken_commands": [
    {
      "command": "trade.buy",
      "error": "Exceeded max retries"
    }
  ],
  "received_responses": [],
  "visited_sectors": [
    230,
    39,
    424,
    79,
    92,
    51,
    156
  ],
  "visited_ports": [
    89,
    68,
    54
  ],
  "previous_sector_id": null,
  "sectors_with_info": [
    230,
    39,
    424,
    79,
    156,
    51,
    92
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
    "424": 1763121373.1772974,
    "230": 1763121399.7435575,
    "39": 1763121401.753802,
    "51": 1763121402.7591653,
    "79": 1763121404.7699583,
    "92": 1763121405.7792227,
    "156": 1763121428.989772
  },
  "last_sector_info_request_time": 1763121428.4890792,
  "last_ship_info_request_time": 1763121372.174605,
  "last_player_info_request_time": 0,
  "port_info_failures_per_sector": {},
  "sector_snapshot_by_id": {},
  "q_table": {
    "stage:explore-sector_class:unknown_class-port_type:no_port": {
      "sector.info": 0.0,
      "move.warp": 0.0
    },
    "stage:survey-sector_class:unknown_class-port_type:port_present": {
      "trade.port_info": 11.25,
      "trade.quote": 0.0
    },
    "stage:exploit-sector_class:unknown_class-port_type:port_present": {
      "trade.buy": 0.0,
      "move.warp": 0.0
    },
    "stage:explore-sector_class:unknown_class-port_type:port_present": {
      "move.warp": 0.0
    }
  },
  "n_table": {
    "stage:explore-sector_class:unknown_class-port_type:no_port": {
      "sector.info": 8,
      "move.warp": 5
    },
    "stage:survey-sector_class:unknown_class-port_type:port_present": {
      "trade.port_info": 4,
      "trade.quote": 5
    },
    "stage:exploit-sector_class:unknown_class-port_type:port_present": {
      "trade.buy": 3,
      "move.warp": 1
    },
    "stage:explore-sector_class:unknown_class-port_type:port_present": {
      "move.warp": 3
    }
  },
  "last_reward": 0.0,
  "last_context_key": "stage:explore-sector_class:unknown_class-port_type:no_port",
  "last_action": "move.warp",
  "last_stage": "explore",
  "bug_reported_this_tick": true,
  "player_username": "ai_qa_bot",
  "client_version": "AI_QA_Bot/1.1",
  "current_credits": 10000,
  "previous_credits": 10000,
  "last_responses_history": [
    {
      "id": "srv-ok",
      "reply_to": "334c3fbf-e62a-4d3c-8218-0f7a44dee855",
      "ts": "2025-11-14T11:57:07Z",
      "status": "ok",
      "type": "session.hello",
      "data": {
        "protocol_version": "1.0",
        "server_time_unix": 1763121427,
        "authenticated": false,
        "player_id": null,
        "current_sector": null,
        "server_time": "2025-11-14T11:57:07Z"
      },
      "error": null,
      "meta": {
        "rate_limit": {
          "limit": 60,
          "remaining": 60,
          "reset": 60,
          "reset_at": 1763121488.487689
        }
      }
    },
    {
      "id": "srv-ok",
      "reply_to": "ed7366d0-fe3e-48c7-b609-ecdb6f0730dd",
      "ts": "2025-11-14T11:57:08Z",
      "status": "ok",
      "type": "sector.info",
      "data": {
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
      "error": null,
      "meta": {
        "rate_limit": {
          "limit": 60,
          "remaining": 60,
          "reset": 60,
          "reset_at": 1763121488.9897652
        }
      }
    },
    {
      "id": "srv-refuse",
      "reply_to": "f151c86c-c802-4c52-b552-df08ac41f083",
      "ts": "2025-11-14T11:57:08Z",
      "status": "refused",
      "type": "error",
      "error": {
        "code": 1402,
        "message": "No warp link",
        "data": {
          "from": 1,
          "to": 170,
          "reason": "no_warp_link"
        }
      },
      "data": null,
      "meta": {
        "rate_limit": {
          "limit": 60,
          "remaining": 60,
          "reset": 60,
          "reset_at": 1763121489.4923584
        }
      }
    },
    {
      "id": "srv-refuse",
      "reply_to": "3376ab9e-e617-434f-96da-c4e6d835b4d7",
      "ts": "2025-11-14T11:57:09Z",
      "status": "refused",
      "type": "error",
      "error": {
        "code": 1402,
        "message": "No warp link",
        "data": {
          "from": 1,
          "to": 170,
          "reason": "no_warp_link"
        }
      },
      "data": null,
      "meta": {
        "rate_limit": {
          "limit": 60,
          "remaining": 60,
          "reset": 60,
          "reset_at": 1763121489.998623
        }
      }
    },
    {
      "id": "srv-refuse",
      "reply_to": "037f358e-80de-4611-b28b-43055ce1172f",
      "ts": "2025-11-14T11:57:10Z",
      "status": "refused",
      "type": "error",
      "error": {
        "code": 1402,
        "message": "No warp link",
        "data": {
          "from": 1,
          "to": 170,
          "reason": "no_warp_link"
        }
      },
      "data": null,
      "meta": {
        "rate_limit": {
          "limit": 60,
          "remaining": 60,
          "reset": 60,
          "reset_at": 1763121490.5055797
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
  "port_info": null,
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
    },
    "51": {
      "89": {
        "equipment": {
          "buy": 237.0,
          "sell": 182.0,
          "timestamp": 1763121403.7645843
        }
      }
    },
    "92": {
      "54": {
        "ore": {
          "buy": 114.0,
          "sell": 93.0,
          "timestamp": 1763121406.7846596
        }
      }
    }
  },
  "next_allowed": {},
  "server_capabilities": {},
  "last_processed_command_name": "move.warp",
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
    },
    "230": {
      "sector_id": 230,
      "name": "Epsilon UMa",
      "adjacent": [
        424
      ],
      "adjacent_count": 1,
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
    "39": {
      "sector_id": 39,
      "name": "Uncharted Space",
      "adjacent": [
        51,
        424
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
    "51": {
      "sector_id": 51,
      "name": "Zeta Cet",
      "adjacent": [
        39,
        79
      ],
      "adjacent_count": 2,
      "ports": [
        {
          "id": 89,
          "name": "Viradric",
          "type": "6"
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
    },
    "79": {
      "sector_id": 79,
      "name": "Mothallah",
      "adjacent": [
        51,
        92
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
    "92": {
      "sector_id": 92,
      "name": "Uncharted Space",
      "adjacent": [
        79,
        156
      ],
      "adjacent_count": 2,
      "ports": [
        {
          "id": 54,
          "name": "Lothaletam",
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
    },
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
    }
  },
  "new_sector_discovered": false,
  "trade_successful": false,
  "new_port_discovered": false,
  "rate_limit_info": {
    "limit": 60,
    "remaining": 60,
    "reset": 60,
    "reset_at": 1763121490.5055797
  },
  "command_retry_info": {
    "trade.buy": {
      "failures": 3,
      "next_retry_time": 0
    },
    "move.warp": {
      "failures": 2,
      "next_retry_time": 0
    }
  },
  "price_cache_updated_this_tick": true,
  "new_sector_visited_this_tick": true
}
```

## Client Version
AI_QA_Bot/1.1

## Configuration & State at Bug
- Random Seed: N/A
- Bandit Epsilon: N/A
- Current Stage: explore

## Replay Script Snippet (JSON Commands)
```json
[
  {},
  {},
  {},
  {},
  {},
  {
    "command": "move.warp",
    "data": {
      "to_sector_id": 170,
      "ship_id": 2
    },
    "meta": {
      "client_version": "AI_QA_Bot/1.1",
      "idempotency_key": "ffcb5765-3086-49a4-904f-122784eeaf98"
    },
    "idempotency_key": "ffcb5765-3086-49a4-904f-122784eeaf98"
  }
]
```
