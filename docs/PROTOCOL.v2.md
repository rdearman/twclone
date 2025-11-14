# twclone — JSON Protocol (v2.0)

**Version:** 2.0  
**Status:** DRAFT

This document describes the version 2.0 JSON protocol for twclone. It consolidates and extends the v1.0 protocol, incorporating the features outlined in the Galactic Economy blueprint and discoveries from test suites. This version introduces comprehensive support for advanced economic and social systems.

---

## 0. Transport, Framing, & Limits

*   **Encoding**: UTF-8.
*   **Framing**: One JSON object per line (NDJSON) or one object per WebSocket message.
*   **Max frame**: 64 KiB default (server MAY advertise higher via `hello.capabilities.limits`).
*   **Compression**: Per-transport (e.g., permessage-deflate on WebSocket).
*   **Clocks**: Timestamps are RFC 3339 UTC (`YYYY-MM-DDTHH:mm:ss.sssZ`).
*   **Numbers**: Use integers for IDs and quantities. Use string-encoded decimals for currency to avoid floating-point errors (e.g., `"12345.67"`).

---

## 1. Message Envelopes

### 1.1. Client Request Envelope

Every client-to-server message is a single JSON object.

```json
{
  "id": "a6f1b8a0-5c8d-4b89-9d82-bb2f0a7b8d57",   // UUIDv4 client-generated unless noted
  "ts": "2025-09-17T19:45:12.345Z",
  "command": "move.warp",                         // The command to execute
  "auth": { "session": "eyJhbGciOi..." },         // Session token
  "data": { /* command-specific payload */ },
  "meta": {
    "idempotency_key": "c4e0e1a9-...",         // optional, client-provided for safe retries
    "client_version": "tw-client/2.0.0",
    "locale": "en-GB"
  }
}
```

### 1.2. Server Response Envelope

The server replies with a corresponding envelope.

```json
{
  "id": "3c1b...server",          // server message id
  "ts": "2025-09-17T19:45:12.410Z",
  "reply_to": "a6f1b8a0-5c8d-4b89-9d82-bb2f0a7b8d57", // Correlates to request `id`
  "status": "ok",                  // "ok" | "error" | "refused" | "partial"
  "type": "sector.info",           // domain-qualified response type
  "data": { /* result payload */ },
  "error": null,                   // or see §8 Error Model
  "meta": {
    "rate_limit": { "limit": 200, "remaining": 152, "reset": "2025-09-17T20:00:00Z" },
    "trace": "srv-az1/fe2:rx394"
  }
}
```

### 1.3. Server-Sent Event Envelope (Broadcasts)

For asynchronous events pushed to the client (not in response to a specific command).

```json
{
  "id": "evt-12345",
  "ts": "2025-09-17T19:47:01Z",
  "event": "sector.player_entered", // The event name
  "data": { /* event-specific payload */ }
}
```
**Key distinction**: Broadcasts use the `event` key and have no `reply_to` field.

---

## 2. Authentication & Session

*   `auth.register`: Creates a new player account.

    *Example Client Request:*
    ```json
    {
      "id": "c1d2e3f4-a5b6-c7d8-e9f0-a1b2c3d4e5f6",
      "ts": "2025-11-07T10:15:00.000Z",
      "command": "auth.register",
      "auth": null,
      "data": {
        "username": "new_player",
        "password": "a_strong_password",
        "ship_name": "My First Ship", // Optional
        "ui_locale": "en_US",         // Optional
        "ui_timezone": "America/New_York" // Optional
      }
    }
    ```

    *Example Server Response:*
    ```json
    {
      "id": "sc1d2e3f4-a5b6-c7d8-e9f0-a1b2c3d4e5f6",
      "ts": "2025-11-07T10:15:00.100Z",
      "reply_to": "c1d2e3f4-a5b6-c7d8-e9f0-a1b2c3d4e5f6",
      "status": "ok",
      "type": "auth.session",
      "data": {
        "player_id": 12345,
        "session_token": "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9..."
      }
    }
    ```

*   `auth.login` (aliases: `login`): Authenticates a user and returns a session token.

    *Example Client Request:*
    ```json
    {
      "id": "d1e2f3a4-b5c6-d7e8-f9a0-b1c2d3e4f5a6",
      "ts": "2025-11-07T10:20:00.000Z",
      "command": "auth.login",
      "auth": null,
      "data": {
        "username": "existing_player",
        "password": "a_strong_password"
      }
    }
    ```

    *Example Server Response:*
    ```json
    {
      "id": "sd1e2f3a4-b5c6-d7e8-f9a0-b1c2d3e4f5a6",
      "ts": "2025-11-07T10:20:00.100Z",
      "reply_to": "d1e2f3a4-b5c6-d7e8-f9a0-b1c2d3e4f5a6",
      "status": "ok",
      "type": "auth.session",
      "data": {
        "player_id": 12345,
        "current_sector": 1,
        "unread_news_count": 0,
        "session_token": "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9..."
      }
    }
    ```

*   `auth.logout`: Invalidates the current session.

    *Example Client Request:*
    ```json
    {
      "id": "e1f2a3b4-c5d6-e7f8-a9b0-c1d2e3f4a5b6",
      "ts": "2025-11-07T10:25:00.000Z",
      "command": "auth.logout",
      "auth": { "session": "eyJhbGciOi..." }, // Optional
      "data": {}
    }
    ```

    *Example Server Response:*
    ```json
    {
      "id": "se1f2a3b4-c5d6-e7f8-a9b0-c1d2e3f4a5b6",
      "ts": "2025-11-07T10:25:00.100Z",
      "reply_to": "e1f2a3b4-c5d6-e7f8-a9b0-c1d2e3f4a5b6",
      "status": "ok",
      "type": "auth.logged_out"
    }
    ```

*   `auth.refresh`: Requests a new session token (if the current one is close to expiry).

    *Example Client Request:*
    ```json
    {
      "id": "f1a2b3c4-d5e6-f7a8-b9c0-d1e2f3a4b5c6",
      "ts": "2025-11-08T10:00:00.000Z",
      "auth": { "session": "eyJhbGciOi..." },
      "data": {
        "session_token": "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9..." // Optional, can also be in meta
      }
    }
    ```

    *Example Server Response:*
    ```json
    {
      "id": "sf1a2b3c4-d5e6-f7a8-b9c0-d1e2f3a4b5c6",
      "ts": "2025-11-08T10:00:00.100Z",
      "reply_to": "f1a2b3c4-d5e6-f7a8-b9c0-d1e2f3a4b5c6",
      "status": "ok",
      "type": "auth.session",
      "data": {
        "player_id": 12345,
        "session_token": "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9... (a new token)"
      }
    }
    ```

*   `auth.mfa.totp.verify`: Verify a TOTP code for Multi-Factor Authentication. (NIY - Not Yet Implemented)

    *Example Client Request:*
    ```json
    {
      "id": "g1h2i3j4-k5l6-m7n8-o9p0-q1r2s3t4u5v6",
      "ts": "2025-11-07T10:30:00.000Z",
      "command": "auth.mfa.totp.verify",
      "auth": { "session": "eyJhbGciOi..." },
      "data": {
        "totp_code": "123456"
      }
    }
    ```

    *Example Server Response (Error):*
    ```json
    {
      "id": "sg1h2i3j4-k5l6-m7n8-o9p0-q1r2s3t4u5v6",
      "ts": "2025-11-07T10:30:00.100Z",
      "reply_to": "g1h2i3j4-k5l6-m7n8-o9p0-q1r2s3t4u5v6",
      "status": "error",
      "type": "auth.mfa.totp.verify",
      "error": {
        "code": 1101,
    }
    ```

*   `user.create`: Creates a new player account. (DEPRECATED: Use `auth.register` instead)

    *Example Client Request:*
    ```json
    {
      "id": "h1i2j3k4-l5m6-n7o8-p9q0-r1s2t3u4v5w6",
      "ts": "2025-11-07T10:35:00.000Z",
      "command": "user.create",
      "auth": null,
      "data": {
        "username": "new_player_deprecated",
        "password": "a_strong_password"
      }
    }
    ```

    *Example Server Response:*
    ```json
    {
      "id": "sh1i2j3k4-l5m6-n7o8-p9q0-r1s2t3u4v5w6",
      "ts": "2025-11-07T10:35:00.100Z",
      "reply_to": "h1i2j3k4-l5m6-n7o8-p9q0-r1s2t3u4v5w6",
      "status": "ok",
      "type": "auth.session",
      "data": {
        "player_id": 12346,
        "session_token": "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9..."
      }
    }
    ```

*   `user.create`: Creates a new player account. (DEPRECATED: Use `auth.register` instead)

    *Example Client Request:*
    ```json
    {
      "id": "h1i2j3k4-l5m6-n7o8-p9q0-r1s2t3u4v5w6",
      "ts": "2025-11-07T10:35:00.000Z",
      "command": "user.create",
      "auth": null,
      "data": {
        "username": "new_player_deprecated",
        "password": "a_strong_password"
      }
    }
    ```

    *Example Server Response:*
    ```json
    {
      "id": "sh1i2j3k4-l5m6-n7o8-p9q0-r1s2t3u4v5w6",
      "ts": "2025-11-07T10:35:00.100Z",
      "reply_to": "h1i2j3k4-l5m6-n7o8-p9q0-r1s2t3u4v5w6",
      "status": "ok",
      "type": "auth.session",
      "data": {
        "player_id": 12346,
        "session_token": "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9..."
      }
    }
    ```

---

## 3. Capability Discovery & Schema

*   **`system.hello`**: Sent by the client upon connection to announce its version and receive server capabilities.

    *Example Client Request:*
    ```json
    {
      "id": "f1a2b3c4-d5e6-f7a8-b9c0-d1e2f3a4b5c6",
      "ts": "2025-11-07T10:00:00.000Z",
      "command": "system.hello",
      "auth": null,
      "data": {
        "client_version": "tw-client/2.1.0",
        "capabilities": ["websockets", "compression.deflate"],
        "auth": {
          "last_session": "eyJhbGciOi..."
        }
      }
    }
    ```

    *Example Server Response:*
    ```json
    {
      "id": "s1a2b3c4-d5e6-f7a8-b9c0-d1e2f3a4b5c6",
      "ts": "2025-11-07T10:00:00.100Z",
      "reply_to": "f1a2b3c4-d5e6-f7a8-b9c0-d1e2f3a4b5c6",
      "status": "ok",
      "type": "system.welcome",
      "data": {
        "server_version": "tw-server/2.0.0",
        "capabilities": {
          "namespaces": ["auth", "bank", "market", "combat"],
          "limits": {
            "max_frame_size": 131072,
            "max_req_per_min": 200
          },
          "auth_methods": ["session", "token"]
        }
      }
    }
    ```

*   **`system.capabilities`**: Provides a list of supported server namespaces, features, and limits.

    *Example Client Request:*
    ```json
    {
      "id": "a1b2c3d4-e5f6-a7b8-c9d0-e1f2a3b4c5d6",
      "ts": "2025-11-07T10:05:00.000Z",
      "command": "system.capabilities",
      "auth": { "session": "eyJhbGciOi..." },
      "data": {}
    }
    ```

    *Example Server Response:*
    ```json
    {
      "id": "s1b2c3d4-e5f6-a7b8-c9d0-e1f2a3b4c5d6",
      "ts": "2025-11-07T10:05:00.100Z",
      "reply_to": "a1b2c3d4-e5f6-a7b8-c9d0-e1f2a3b4c5d6",
      "status": "ok",
      "type": "system.capabilities",
      "data": {
        "server_version": "tw-server/2.0.0",
        "namespaces": ["auth", "bank", "market", "combat", "ship", "player"],
        "limits": {
          "max_frame_size": 131072,
          "max_req_per_min": 200,
          "max_open_orders": 100
        },
        "auth_methods": ["session", "token"],
        "features": ["websockets", "compression.deflate", "idempotency_key"]
      }
    }
    ```
    *   **`system.describe_schema`**: Returns a JSON Schema for a given command or event, useful for client-side validation and code generation.
    
        *Example Client Request:*
        ```json
        {
          "id": "b1c2d3e4-f5a6-b7c8-d9e0-f1a2b3c4d5e6",
          "ts": "2025-11-07T10:10:00.000Z",
          "command": "system.describe_schema",
          "auth": { "session": "eyJhbGciOi..." },
          "data": {
            "type": "command",
            "name": "bank.transfer"
          }
        }
        ```
    
        *Example Server Response:*
        ```json
        {
          "id": "sb1c2d3e4-f5a6-b7c8-d9e0-f1a2b3c4d5e6",
          "ts": "2025-11-07T10:10:00.100Z",
          "reply_to": "b1c2d3e4-f5a6-b7c8-d9e0-f1a2b3c4d5e6",
          "status": "ok",
          "type": "system.schema",
          "data": {
            "name": "bank.transfer",
            "type": "command",
            "schema": {
              "type": "object",
              "properties": {
                "to_player_name": { "type": "string", "description": "The name of the player to transfer credits to." },
                "amount": { "type": "string", "pattern": "^[0-9]+(\.[0-9]{2})?$", "description": "The amount of credits to transfer." },
                "memo": { "type": "string", "maxLength": 100, "description": "A brief memo for the transaction." }
              },
              "required": ["to_player_name", "amount"]
            }
          }
        }
        ```
    
    ---

## 4. Client Commands (RPCs)

### 4.1. Player & Session
*   `player.my_info`: Get current player, ship, and location data.

    *Example Client Request:*
    ```json
    {
      "id": "a1b2c3d4-e5f6-a7b8-c9d0-e1f2a3b4c5d6",
      "ts": "2025-11-07T11:00:00.000Z",
      "command": "player.my_info",
      "auth": { "session": "eyJhbGciOi..." },
      "data": {}
    }
    ```

    *Example Server Response:*
    ```json
    {
      "id": "sa1b2c3d4-e5f6-a7b8-c9d0-e1f2a3b4c5d6",
      "ts": "2025-11-07T11:00:00.100Z",
      "reply_to": "a1b2c3d4-e5f6-a7b8-c9d0-e1f2a3b4c5d6",
      "status": "ok",
      "type": "player.info",
      "data": {
        "player": {
          "id": 12345,
          "username": "player1",
          "credits": "10000.00",
          "experience": 5000,
          "corporation": {
            "id": 101,
            "name": "StarTraders Inc."
          }
        },
        "ship": {
          "id": 54321,
          "name": "Stardust Cruiser",
          "class": "Freighter",
          "holds": {
            "capacity": 100,
            "current": 50,
            "cargo": [
              { "commodity": "ore", "quantity": 50 }
            ]
          }
        },
        "location": {
          "sector": 1,
          "name": "Sol"
        }
      }
    }
    ```

*   `player.list_online`: List online players (paginated).

    *Example Client Request:*
    ```json
    {
      "id": "b1c2d3e4-f5a6-b7c8-d9e0-f1a2b3c4d5e6",
      "ts": "2025-11-07T11:05:00.000Z",
      "command": "player.list_online",
      "auth": { "session": "eyJhbGciOi..." },
      "data": {
        "page": 1,
        "limit": 50
      }
    }
    ```

    *Example Server Response:*
    ```json
    {
      "id": "sb1c2d3e4-f5a6-b7c8-d9e0-f1a2b3c4d5e6",
      "ts": "2025-11-07T11:05:00.100Z",
      "reply_to": "b1c2d3e4-f5a6-b7c8-d9e0-f1a2b3c4d5e6",
      "status": "ok",
      "type": "player.online_list",
      "data": {
        "players": [
          { "id": 12345, "username": "player1", "corporation_name": "StarTraders Inc." },
          { "id": 54321, "username": "player2", "corporation_name": null }
        ],
        "pagination": {
          "total_players": 2,
          "total_pages": 1,
          "current_page": 1
        }
      }
    }
    ```

*   `player.rankings`: Get player rankings (e.g., net worth, experience).

    *Example Client Request:*
    ```json
    {
      "id": "c1d2e3f4-a5b6-c7d8-e9f0-a1b2c3d4e5f6",
      "ts": "2025-11-07T11:10:00.000Z",
      "command": "player.rankings",
      "auth": { "session": "eyJhbGciOi..." },
      "data": {
        "by": "net_worth",
        "limit": 10
      }
    }
    ```

    *Example Server Response:*
    ```json
    {
      "id": "sc1d2e3f4-a5b6-c7d8-e9f0-a1b2c3d4e5f6",
      "ts": "2025-11-07T11:10:00.100Z",
      "reply_to": "c1d2e3f4-a5b6-c7d8-e9f0-a1b2c3d4e5f6",
      "status": "ok",
      "type": "player.rankings",
      "data": {
        "rankings": [
          { "rank": 1, "username": "richest_player", "net_worth": "100000000.00" },
          { "rank": 2, "username": "player1", "net_worth": "50000000.00" }
        ]
      }
    }
    ```

*   `player.get_settings`: Retrieve a bundle of player preferences, bookmarks, avoid lists, etc.

    *Example Client Request:*
    ```json
    {
      "id": "d1e2f3a4-b5c6-d7e8-f9a0-b1c2d3e4f5a6",
      "ts": "2025-11-07T11:15:00.000Z",
      "command": "player.get_settings",
      "auth": { "session": "eyJhbGciOi..." },
      "data": {}
    }
    ```

    *Example Server Response:*
    ```json
    {
      "id": "sd1e2f3a4-b5c6-d7e8-f9a0-b1c2d3e4f5a6",
      "ts": "2025-11-07T11:15:00.100Z",
      "reply_to": "d1e2f3a4-b5c6-d7e8-f9a0-b1c2d3e4f5a6",
      "status": "ok",
      "type": "player.settings",
      "data": {
        "preferences": {
          "ui_theme": "dark",
          "notifications": {
            "chat": true,
            "market": false
          }
        },
        "bookmarks": [
          { "sector": 101, "name": "Trading Hub" }
        ],
        "avoid_list": [ 666 ]
      }
    }
    ```

*   `player.set_prefs`: Update player preferences (e.g., UI, notifications).

    *Example Client Request:*
    ```json
    {
      "id": "e1f2a3b4-c5d6-e7f8-a9b0-c1d2e3f4a5b6",
      "ts": "2025-11-07T11:20:00.000Z",
      "command": "player.set_prefs",
      "auth": { "session": "eyJhbGciOi..." },
      "data": {
        "preferences": {
          "ui_theme": "light"
        }
      }
    }
    ```

    *Example Server Response:*
    ```json
    {
      "id": "se1f2a3b4-c5d6-e7f8-a9b0-c1d2e3f4a5b6",
      "ts": "2025-11-07T11:20:00.100Z",
      "reply_to": "e1f2a3b4-c5d6-e7f8-a9b0-c1d2e3f4a5b6",
      "status": "ok",
      "type": "player.prefs_updated",
      "data": {
        "preferences": {
          "ui_theme": "light",
          "notifications": {
            "chat": true,
            "market": false
          }
        }
      }
    }
    ```

### 4.2. Core Finance (Player & Corp Banking)
Commands for interacting with the ledger-based economy. Most are only available at designated banking locations (e.g., Stardock).

*   `bank.balance`: Get current player bank balance.

    *Example Client Request:*
    ```json
    {
      "id": "a1b2c3d4-e5f6-a7b8-c9d0-e1f2a3b4c5d6",
      "ts": "2025-11-07T12:00:00.000Z",
      "command": "bank.balance",
      "auth": { "session": "eyJhbGciOi..." },
      "data": {}
    }
    ```

    *Example Server Response:*
    ```json
    {
      "id": "sa1b2c3d4-e5f6-a7b8-c9d0-e1f2a3b4c5d6",
      "ts": "2025-11-07T12:00:00.100Z",
      "reply_to": "a1b2c3d4-e5f6-a7b8-c9d0-e1f2a3b4c5d6",
      "status": "ok",
      "type": "bank.balance",
      "data": {
        "balance": "123456.78"
      }
    }
    ```

*   `bank.statement`: Get a paginated history of transactions.

    *Example Client Request:*
    ```json
    {
      "id": "b1c2d3e4-f5a6-b7c8-d9e0-f1a2b3c4d5e6",
      "ts": "2025-11-07T12:05:00.000Z",
      "command": "bank.statement",
      "auth": { "session": "eyJhbGciOi..." },
      "data": {
        "page": 1,
        "limit": 10
      }
    }
    ```

    *Example Server Response:*
    ```json
    {
      "id": "sb1c2d3e4-f5a6-b7c8-d9e0-f1a2b3c4d5e6",
      "ts": "2025-11-07T12:05:00.100Z",
      "reply_to": "b1c2d3e4-f5a6-b7c8-d9e0-f1a2b3c4d5e6",
      "status": "ok",
      "type": "bank.statement",
      "data": {
        "transactions": [
          { "id": "txn_1", "ts": "2025-11-07T11:50:00.000Z", "type": "deposit", "amount": "1000.00", "balance": "123456.78" },
          { "id": "txn_2", "ts": "2025-11-07T11:45:00.000Z", "type": "withdraw", "amount": "500.00", "balance": "122456.78" }
        ],
        "pagination": {
          "total_transactions": 2,
          "total_pages": 1,
          "current_page": 1
        }
      }
    }
    ```

*   `bank.deposit`: Deposit credits from hand to bank account.

    *Example Client Request:*
    ```json
    {
      "id": "c1d2e3f4-a5b6-c7d8-e9f0-a1b2c3d4e5f6",
      "ts": "2025-11-07T12:10:00.000Z",
      "command": "bank.deposit",
      "auth": { "session": "eyJhbGciOi..." },
      "data": { "amount": "1000.00" }
    }
    ```

    *Example Server Response:*
    ```json
    {
      "id": "sc1d2e3f4-a5b6-c7d8-e9f0-a1b2c3d4e5f6",
      "ts": "2025-11-07T12:10:00.100Z",
      "reply_to": "c1d2e3f4-a5b6-c7d8-e9f0-a1b2c3d4e5f6",
      "status": "ok",
      "type": "bank.transaction_receipt",
      "data": {
        "transaction_id": "txn_3",
        "type": "deposit",
        "amount": "1000.00",
        "new_balance": "124456.78"
      }
    }
    ```

*   `bank.withdraw`: Withdraw credits from bank to hand.

    *Example Client Request:*
    ```json
    {
      "id": "d1e2f3a4-b5c6-d7e8-f9a0-b1c2d3e4f5a6",
      "ts": "2025-11-07T12:15:00.000Z",
      "command": "bank.withdraw",
      "auth": { "session": "eyJhbGciOi..." },
      "data": { "amount": "500.00" }
    }
    ```

    *Example Server Response:*
    ```json
    {
      "id": "sd1e2f3a4-b5c6-d7e8-f9a0-b1c2d3e4f5a6",
      "ts": "2025-11-07T12:15:00.100Z",
      "reply_to": "d1e2f3a4-b5c6-d7e8-f9a0-b1c2d3e4f5a6",
      "status": "ok",
      "type": "bank.transaction_receipt",
      "data": {
        "transaction_id": "txn_4",
        "type": "withdraw",
        "amount": "500.00",
        "new_balance": "123956.78"
      }
    }
    ```

*   `bank.transfer`: Transfer credits to another player.

    *Example Client Request:*
    ```json
    {
      "id": "e1f2a3b4-c5d6-e7f8-a9b0-c1d2e3f4a5b6",
      "ts": "2025-11-07T12:20:00.000Z",
      "command": "bank.transfer",
      "auth": { "session": "eyJhbGciOi..." },
      "data": {
        "to_player_name": "Jane",
        "amount": "500.00",
        "memo": "For the ore"
      }
    }
    ```

    *Example Server Response:*
    ```json
    {
      "id": "se1f2a3b4-c5d6-e7f8-a9b0-c1d2e3f4a5b6",
      "ts": "2025-11-07T12:20:00.100Z",
      "reply_to": "e1f2a3b4-c5d6-e7f8-a9b0-c1d2e3f4a5b6",
      "status": "ok",
      "type": "bank.transaction_receipt",
      "data": {
        "transaction_id": "txn_5",
        "type": "transfer_out",
        "amount": "500.00",
        "new_balance": "123456.78",
        "recipient": "Jane"
      }
    }
    ```

*   `bank.orders.list`: List recurring/standing payment orders.

    *Example Client Request:*
    ```json
    {
      "id": "a1b2c3d4-e5f6-a7b8-c9d0-e1f2a3b4c5d6",
      "ts": "2025-11-07T12:25:00.000Z",
      "command": "bank.orders.list",
      "auth": { "session": "eyJhbGciOi..." },
      "data": {}
    }
    ```

    *Example Server Response:*
    ```json
    {
      "id": "sa1b2c3d4-e5f6-a7b8-c9d0-e1f2a3b4c5d6",
      "ts": "2025-11-07T12:25:00.100Z",
      "reply_to": "a1b2c3d4-e5f6-a7b8-c9d0-e1f2a3b4c5d6",
      "status": "ok",
      "type": "bank.orders_list",
      "data": {
        "orders": [
          { "order_id": "ord_1", "to_player_name": "employee1", "amount": "1000.00", "frequency": "weekly", "next_payment_ts": "2025-11-14T12:00:00.000Z" }
        ]
      }
    }
    ```

*   `bank.orders.create`: Create a new recurring payment.

    *Example Client Request:*
    ```json
    {
      "id": "b1c2d3e4-f5a6-b7c8-d9e0-f1a2b3c4d5e6",
      "ts": "2025-11-07T12:30:00.000Z",
      "command": "bank.orders.create",
      "auth": { "session": "eyJhbGciOi..." },
      "data": {
        "to_player_name": "employee2",
        "amount": "500.00",
        "frequency": "monthly",
        "memo": "Monthly salary"
      }
    }
    ```

    *Example Server Response:*
    ```json
    {
      "id": "sb1c2d3e4-f5a6-b7c8-d9e0-f1a2b3c4d5e6",
      "ts": "2025-11-07T12:30:00.100Z",
      "reply_to": "b1c2d3e4-f5a6-b7c8-d9e0-f1a2b3c4d5e6",
      "status": "ok",
      "type": "bank.order_created",
      "data": {
        "order_id": "ord_2",
        "to_player_name": "employee2",
        "amount": "500.00",
        "frequency": "monthly",
        "next_payment_ts": "2025-12-07T12:00:00.000Z"
      }
    }
    ```

*   `bank.orders.cancel`: Cancel a recurring payment.

    *Example Client Request:*
    ```json
    {
      "id": "c1d2e3f4-a5b6-c7d8-e9f0-a1b2c3d4e5f6",
      "ts": "2025-11-07T12:35:00.000Z",
      "command": "bank.orders.cancel",
      "auth": { "session": "eyJhbGciOi..." },
      "data": {
        "order_id": "ord_1"
      }
    }
    ```

    *Example Server Response:*
    ```json
    {
      "id": "sc1d2e3f4-a5b6-c7d8-e9f0-a1b2c3d4e5f6",
      "ts": "2025-11-07T12:35:00.100Z",
      "reply_to": "c1d2e3f4-a5b6-c7d8-e9f0-a1b2c3d4e5f6",
      "status": "ok",
      "type": "bank.order_cancelled",
      "data": {
        "order_id": "ord_1"
      }
    }
    ```

*   `corp.balance`: Get the balance of the player's corporation.

    *Example Client Request:*
    ```json
    {
      "id": "a1b2c3d4-e5f6-a7b8-c9d0-e1f2a3b4c5d6",
      "ts": "2025-11-07T13:00:00.000Z",
      "command": "corp.balance",
      "auth": { "session": "eyJhbGciOi..." },
      "data": {}
    }
    ```

    *Example Server Response:*
    ```json
    {
      "id": "sa1b2c3d4-e5f6-a7b8-c9d0-e1f2a3b4c5d6",
      "ts": "2025-11-07T13:00:00.100Z",
      "reply_to": "a1b2c3d4-e5f6-a7b8-c9d0-e1f2a3b4c5d6",
      "status": "ok",
      "type": "corp.balance",
      "data": {
        "balance": "12345678.90"
      }
    }
    ```

*   `corp.statement`: Get the corporation's transaction history.

    *Example Client Request:*
    ```json
    {
      "id": "b1c2d3e4-f5a6-b7c8-d9e0-f1a2b3c4d5e6",
      "ts": "2025-11-07T13:05:00.000Z",
      "command": "corp.statement",
      "auth": { "session": "eyJhbGciOi..." },
      "data": {
        "page": 1,
        "limit": 10
      }
    }
    ```

    *Example Server Response:*
    ```json
    {
      "id": "sb1c2d3e4-f5a6-b7c8-d9e0-f1a2b3c4d5e6",
      "ts": "2025-11-07T13:05:00.100Z",
      "reply_to": "b1c2d3e4-f5a6-b7c8-d9e0-f1a2b3c4d5e6",
      "status": "ok",
      "type": "corp.statement",
      "data": {
        "transactions": [
          { "id": "txn_corp_1", "ts": "2025-11-07T12:50:00.000Z", "type": "deposit", "amount": "10000.00", "player": "player1", "balance": "12345678.90" },
          { "id": "txn_corp_2", "ts": "2025-11-07T12:45:00.000Z", "type": "dividend", "amount": "50000.00", "balance": "12335678.90" }
        ],
        "pagination": {
          "total_transactions": 2,
          "total_pages": 1,
          "current_page": 1
        }
      }
    }
    ```

*   `corp.deposit`: Deposit credits into the corporate treasury.

    *Example Client Request:*
    ```json
    {
      "id": "c1d2e3f4-a5b6-c7d8-e9f0-a1b2c3d4e5f6",
      "ts": "2025-11-07T13:10:00.000Z",
      "command": "corp.deposit",
      "auth": { "session": "eyJhbGciOi..." },
      "data": { "amount": "10000.00" }
    }
    ```

    *Example Server Response:*
    ```json
    {
      "id": "sc1d2e3f4-a5b6-c7d8-e9f0-a1b2c3d4e5f6",
      "ts": "2025-11-07T13:10:00.100Z",
      "reply_to": "c1d2e3f4-a5b6-c7d8-e9f0-a1b2c3d4e5f6",
      "status": "ok",
      "type": "corp.transaction_receipt",
      "data": {
        "transaction_id": "txn_corp_3",
        "type": "deposit",
        "amount": "10000.00",
        "new_balance": "12355678.90"
      }
    }
    ```

*   `corp.withdraw`: Withdraw credits (requires permissions).

    *Example Client Request:*
    ```json
    {
      "id": "d1e2f3a4-b5c6-d7e8-f9a0-b1c2d3e4f5a6",
      "ts": "2025-11-07T13:15:00.000Z",
      "command": "corp.withdraw",
      "auth": { "session": "eyJhbGciOi..." },
      "data": { "amount": "20000.00" }
    }
    ```

    *Example Server Response:*
    ```json
    {
      "id": "sd1e2f3a4-b5c6-d7e8-f9a0-b1c2d3e4f5a6",
      "ts": "2025-11-07T13:15:00.100Z",
      "reply_to": "d1e2f3a4-b5c6-d7e8-f9a0-b1c2d3e4f5a6",
      "status": "ok",
      "type": "corp.transaction_receipt",
      "data": {
        "transaction_id": "txn_corp_4",
        "type": "withdraw",
        "amount": "20000.00",
        "new_balance": "12335678.90"
      }
    }
    ```

*   `corp.set_tax`: Set the automated tax rate for corp members.

    *Example Client Request:*
    ```json
    {
      "id": "e1f2a3b4-c5d6-e7f8-a9b0-c1d2e3f4a5b6",
      "ts": "2025-11-07T13:20:00.000Z",
      "command": "corp.set_tax",
      "auth": { "session": "eyJhbGciOi..." },
      "data": {
        "rate": 5.5
      }
    }
    ```

    *Example Server Response:*
    ```json
    {
      "id": "se1f2a3b4-c5d6-e7f8-a9b0-c1d2e3f4a5b6",
      "ts": "2025-11-07T13:20:00.100Z",
      "reply_to": "e1f2a3b4-c5d6-e7f8-a9b0-c1d2e3f4a5b6",
      "status": "ok",
      "type": "corp.tax_rate_updated",
      "data": {
        "new_rate": 5.5
      }
    }
    ```

*   `corp.issue_dividend`: Issue a dividend to all corp members from the treasury.

    *Example Client Request:*
    ```json
    {
      "id": "f1a2b3c4-d5e6-f7a8-b9c0-d1e2f3a4b5c6",
      "ts": "2025-11-07T13:25:00.000Z",
      "command": "corp.issue_dividend",
      "auth": { "session": "eyJhbGciOi..." },
      "data": {
        "total_amount": "1000000.00"
      }
    }
    ```

    *Example Server Response:*
    ```json
    {
      "id": "sf1a2b3c4-d5e6-f7a8-b9c0-d1e2f3a4b5c6",
      "ts": "2025-11-07T13:25:00.100Z",
      "reply_to": "f1a2b3c4-d5e6-f7a8-b9c0-d1e2f3a4b5c6",
      "status": "ok",
      "type": "corp.dividend_issued",
      "data": {
        "total_amount": "1000000.00",
        "members_paid": 50,
        "amount_per_member": "20000.00"
      }
    }
    ```

### 4.3. Government, Law & Crime
*   `bounty.list`: List active bounties on players.

    *Example Client Request:*
    ```json
    {
      "id": "a1b2c3d4-e5f6-a7b8-c9d0-e1f2a3b4c5d6",
      "ts": "2025-11-07T14:00:00.000Z",
      "command": "bounty.list",
      "auth": { "session": "eyJhbGciOi..." },
      "data": {
        "page": 1,
        "limit": 10
      }
    }
    ```

    *Example Server Response:*
    ```json
    {
      "id": "sa1b2c3d4-e5f6-a7b8-c9d0-e1f2a3b4c5d6",
      "ts": "2025-11-07T14:00:00.100Z",
      "reply_to": "a1b2c3d4-e5f6-a7b8-c9d0-e1f2a3b4c5d6",
      "status": "ok",
      "type": "bounty.list",
      "data": {
        "bounties": [
          { "target_player_name": "BadGuy", "reward": "100000.00", "poster_player_name": "GoodGuy" }
        ],
        "pagination": {
          "total_bounties": 1,
          "total_pages": 1,
          "current_page": 1
        }
      }
    }
    ```

*   `bounty.post`: Post a new bounty on a player. Funds are held in escrow.

    *Example Client Request:*
    ```json
    {
      "id": "b1c2d3e4-f5a6-b7c8-d9e0-f1a2b3c4d5e6",
      "ts": "2025-11-07T14:05:00.000Z",
      "command": "bounty.post",
      "auth": { "session": "eyJhbGciOi..." },
      "data": {
        "target_player_name": "AnotherBadGuy",
        "reward": "50000.00"
      }
    }
    ```

    *Example Server Response:*
    ```json
    {
      "id": "sb1c2d3e4-f5a6-b7c8-d9e0-f1a2b3c4d5e6",
      "ts": "2025-11-07T14:05:00.100Z",
      "reply_to": "b1c2d3e4-f5a6-b7c8-d9e0-f1a2b3c4d5e6",
      "status": "ok",
      "type": "bounty.posted",
      "data": {
        "bounty_id": "bounty_123",
        "target_player_name": "AnotherBadGuy",
        "reward": "50000.00"
      }
    }
    ```

*   `fine.list`: List outstanding fines for the current player.

    *Example Client Request:*
    ```json
    {
      "id": "c1d2e3f4-a5b6-c7d8-e9f0-a1b2c3d4e5f6",
      "ts": "2025-11-07T14:10:00.000Z",
      "command": "fine.list",
      "auth": { "session": "eyJhbGciOi..." },
      "data": {}
    }
    ```

    *Example Server Response:*
    ```json
    {
      "id": "sc1d2e3f4-a5b6-c7d8-e9f0-a1b2c3d4e5f6",
      "ts": "2025-11-07T14:10:00.100Z",
      "reply_to": "c1d2e3f4-a5b6-c7d8-e9f0-a1b2c3d4e5f6",
      "status": "ok",
      "type": "fine.list",
      "data": {
        "fines": [
          { "fine_id": "fine_1", "reason": "Illegal parking", "amount": "100.00" }
        ]
      }
    }
    ```

*   `fine.pay`: Pay an outstanding fine.

    *Example Client Request:*
    ```json
    {
      "id": "d1e2f3a4-b5c6-d7e8-f9a0-b1c2d3e4f5a6",
      "ts": "2025-11-07T14:15:00.000Z",
      "command": "fine.pay",
      "auth": { "session": "eyJhbGciOi..." },
      "data": {
        "fine_id": "fine_1"
      }
    }
    ```

    *Example Server Response:*
    ```json
    {
      "id": "sd1e2f3a4-b5c6-d7e8-f9a0-b1c2d3e4f5a6",
      "ts": "2025-11-07T14:15:00.100Z",
      "reply_to": "d1e2f3a4-b5c6-d7e8-f9a0-b1c2d3e4f5a6",
      "status": "ok",
      "type": "fine.paid",
      "data": {
        "fine_id": "fine_1",
        "remaining_fines": 0
      }
    }
    ```

### 4.4. Movement & Navigation
*   `move.describe_sector`: (ALIAS for `sector.info`) Get detailed information about a sector. See `sector.info` for details.

*   `sector.info`: Get current sector information.

    *Example Client Request:*
    ```json
    {
      "id": "a1b2c3d4-e5f6-a7b8-c9d0-e1f2a3b4c5d6",
      "ts": "2025-11-07T15:00:00.000Z",
      "command": "sector.info",
      "auth": { "session": "eyJhbGciOi..." },
      "data": {
        "sector_id": 1 // Optional: If omitted, current sector info is returned
      }
    }
    ```

    *Example Server Response:*
    ```json
    {
      "id": "sa1b2c3d4-e5f6-a7b8-c9d0-e1f2a3b4c5d6",
      "ts": "2025-11-07T15:00:00.100Z",
      "reply_to": "a1b2c3d4-e5f6-a7b8-c9d0-e1f2a3b4c5d6",
      "status": "ok",
      "type": "sector.info",
      "data": {
        "sector_id": 1,
        "name": "Sol",
        "description": "The home system of humanity.",
        "coordinates": { "x": 0, "y": 0, "z": 0 },
        "adjacent_sectors": [
          { "id": 2, "name": "Alpha Centauri" },
          { "id": 3, "name": "Barnard's Star" }
        ],
        "celestial_objects": [
          { "type": "planet", "id": 101, "name": "Earth" },
          { "type": "star", "id": 102, "name": "Sol" }
        ],
        "ports": [
          { "id": 1, "name": "Earth Port", "type": "Stardock", "faction": "Federation" }
        ],
        "ships_present": [
          { "id": 54321, "owner_id": 12345, "owner_name": "player1", "type": "Freighter" }
        ],
        "players_present": [
          { "id": 12345, "username": "player1" }
        ],
        "beacons": [
          { "id": 201, "text": "Welcome to Sol!" }
        ]
      }
    }
    ```

*   `move.warp`: Warp to an adjacent sector.

    *Example Client Request:*
    ```json
    {
      "id": "b1c2d3e4-f5a6-b7c8-d9e0-f1a2b3c4d5e6",
      "ts": "2025-11-07T15:05:00.000Z",
      "command": "move.warp",
      "auth": { "session": "eyJhbGciOi..." },
      "data": {
        "to_sector_id": 2
      }
    }
    ```

    *Example Server Response:*
    ```json
    {
      "id": "sb1c2d3e4-f5a6-b7c8-d9e0-f1a2b3c4d5e6",
      "ts": "2025-11-07T15:05:00.100Z",
      "reply_to": "b1c2d3e4-f5a6-b7c8-d9e0-f1a2b3c4d5e6",
      "status": "ok",
      "type": "move.result",
      "data": {
        "player_id": 12345,
        "from_sector_id": 1,
        "to_sector_id": 2,
        "current_sector": 2
      }
    }
    ```
    *Note: This command also broadcasts `sector.player_left` to the `from_sector` and `sector.player_entered` to the `to_sector`.*

*   `move.scan`: Get a summary scan of the current sector.

    *Example Client Request:*
    ```json
    {
      "id": "h1i2j3k4-l5m6-n7o8-p9q0-r1s2t3u4v5w6",
      "ts": "2025-11-07T15:06:00.000Z",
      "command": "move.scan",
      "auth": { "session": "eyJhbGciOi..." },
      "data": {}
    }
    ```

    *Example Server Response:*
    ```json
    {
      "id": "sh1i2j3k4-l5m6-n7o8-p9q0-r1s2t3u4v5w6",
      "ts": "2025-11-07T15:06:00.100Z",
      "reply_to": "h1i2j3k4-l5m6-n7o8-p9q0-r1s2t3u4v5w6",
      "status": "ok",
      "type": "sector.scan_v1",
      "data": {
        "sector_id": 1,
        "name": "Sol",
        "ships_count": 5,
        "players_count": 2,
        "ports_count": 1,
        "planets_count": 3
      }
    }
    ```

*   `sector.scan`: Get a detailed scan of the current sector.

    *Example Client Request:*
    ```json
    {
      "id": "i1j2k3l4-m5n6-o7p8-q9r0-s1t2u3v4w5x6",
      "ts": "2025-11-07T15:07:00.000Z",
      "command": "sector.scan",
      "auth": { "session": "eyJhbGciOi..." },
      "data": {}
    }
    ```

    *Example Server Response:*
    ```json
    {
      "id": "si1j2k3l4-m5n6-o7p8-q9r0-s1t2u3v4w5x6",
      "ts": "2025-11-07T15:07:00.100Z",
      "reply_to": "i1j2k3l4-m5n6-o7p8-q9r0-s1t2u3v4w5x6",
      "status": "ok",
      "type": "sector.scan",
      "data": {
        "sector_id": 1,
        "name": "Sol",
        "celestial_objects": [
          { "type": "planet", "id": 101, "name": "Earth", "resources": ["water", "minerals"] }
        ],
        "ships_present": [
          { "id": 54321, "owner_name": "player1", "type": "Freighter", "cargo": [{"commodity": "ore", "quantity": 50}] }
        ],
        "anomalies": [
          { "type": "wormhole", "destination_sector": 10 }
        ]
      }
    }
    ```

*   `sector.scan.density`: Get density scan data for surrounding sectors.

    *Example Client Request:*
    ```json
    {
      "id": "j1k2l3m4-n5o6-p7q8-r9s0-t1u2v3w4x5y6",
      "ts": "2025-11-07T15:08:00.000Z",
      "command": "sector.scan.density",
      "auth": { "session": "eyJhbGciOi..." },
      "data": {
        "sector_id": 1 // Optional: If omitted, current sector's density is returned
      }
    }
    ```

    *Example Server Response:*
    ```json
    {
      "id": "sj1k2l3m4-n5o6-p7q8-r9s0-t1u2v3w4x5y6",
      "ts": "2025-11-07T15:08:00.100Z",
      "reply_to": "j1k2l3m4-n5o6-p7q8-r9s0-t1u2v3w4x5y6",
      "status": "ok",
      "type": "sector.density.scan",
      "data": [
        { "sector_id": 1, "density": 0.85 },
        { "sector_id": 2, "density": 0.60 },
        { "sector_id": 3, "density": 0.92 }
    }
    ```

*   `sector.search`: Search for objects or players within sectors.

    *Example Client Request:*
    ```json
    {
      "id": "k1l2m3n4-o5p6-q7r8-s9t0-u1v2w3x4y5z6",
      "ts": "2025-11-07T15:09:00.000Z",
      "command": "sector.search",
      "auth": { "session": "eyJhbGciOi..." },
      "data": {
        "q": "player1",
        "type": "player", // Optional: "player", "ship", "planet", "port", "beacon"
        "limit": 10,      // Optional: Default 20
        "cursor": 0       // Optional: For pagination
      }
    }
    ```

    *Example Server Response:*
    ```json
    {
      "id": "sk1l2m3n4-o5p6-q7r8-s9t0-u1v2w3x4y5z6",
      "ts": "2025-11-07T15:09:00.100Z",
      "reply_to": "k1l2m3n4-o5p6-q7r8-s9t0-u1v2w3x4y5z6",
      "status": "ok",
      "type": "sector.search_results_v1",
      "data": {
        "items": [
          { "type": "player", "id": 12345, "name": "player1", "sector_id": 1 },
          { "type": "ship", "id": 54321, "name": "Stardust Cruiser", "sector_id": 1 }
        ],
        "next_cursor": 10 // Or null if no more results
      }
    }
    ```

*   `sector.set_beacon`: Set a custom beacon message in a sector.

    *Example Client Request:*
    ```json
    {
      "id": "l1m2n3o4-p5q6-r7s8-t9u0-v1w2x3y4z5a6",
      "ts": "2025-11-07T15:10:00.000Z",
      "command": "sector.set_beacon",
      "auth": { "session": "eyJhbGciOi..." },
      "data": {
        "sector_id": 1,
        "text": "Beware of pirates!"
      }
    }
    ```

    *Example Server Response:*
    ```json
    {
      "id": "sl1m2n3o4-p5q6-r7s8-t9u0-v1w2x3y4z5a6",
      "ts": "2025-11-07T15:10:00.100Z",
      "reply_to": "l1m2n3o4-p5q6-r7s8-t9u0-v1w2x3y4z5a6",
      "status": "ok",
      "type": "sector.set_beacon",
      "data": null,
      "meta": {
        "message": "Beacon set successfully."
      }
    }
    ```
    *Note: Setting a beacon will typically be followed by a `sector.info` event broadcast to all clients in the sector.*



*   `move.pathfind`: Find the shortest path between two sectors.

    *Example Client Request:*
    ```json
    {
      "id": "c1d2e3f4-a5b6-c7d8-e9f0-a1b2c3d4e5f6",
      "ts": "2025-11-07T15:10:00.000Z",
      "command": "move.pathfind",
      "auth": { "session": "eyJhbGciOi..." },
      "data": {
        "from": 1, // Optional: If omitted, current sector is used as start
        "to": 10,
        "avoid": [666, 777] // Optional: Sectors to avoid
      }
    }
    ```

    *Example Server Response:*
    ```json
    {
      "id": "sc1d2e3f4-a5b6-c7d8-e9f0-a1b2c3d4e5f6",
      "ts": "2025-11-07T15:10:00.100Z",
      "reply_to": "c1d2e3f4-a5b6-c7d8-e9f0-a1b2c3d4e5f6",
      "status": "ok",
      "type": "move.path_v1",
      "data": {
        "steps": [1, 2, 5, 8, 10],
        "total_cost": 4
      }
    }
    ```

*   `move.transwarp`: Execute a long-range jump (requires special drive).

    *Example Client Request:*
    ```json
    {
      "id": "d1e2f3a4-b5c6-d7e8-f9a0-b1c2d3e4f5a6",
      "ts": "2025-11-07T15:15:00.000Z",
      "command": "move.transwarp",
      "auth": { "session": "eyJhbGciOi..." },
      "data": {
        "sector_id": 50
      }
    }
    ```

    *Example Server Response:*
    ```json
    {
      "id": "sd1e2f3a4-b5c6-d7e8-f9a0-b1c2d3e4f5a6",
      "ts": "2025-11-07T15:15:00.100Z",
      "reply_to": "d1e2f3a4-b5c6-d7e8-f9a0-b1c2d3e4f5a6",
      "status": "ok",
      "type": "move.transwarp_complete",
      "data": {
        "new_sector_id": 50,
        "new_sector_name": "Sirius"
      }
    }
    ```

*   `move.autopilot.start`: (ALIAS for `move.pathfind`) Start server-side autopilot.

    *Example Client Request:*
    ```json
    {
      "id": "e1f2a3b4-c5d6-e7f8-a9b0-c1d2e3f4a5b6",
      "ts": "2025-11-07T15:20:00.000Z",
      "command": "move.autopilot.start",
      "auth": { "session": "eyJhbGciOi..." },
      "data": {
        "from": 1, // Optional: If omitted, current sector is used as start
        "to": 10,
        "avoid": [666, 777] // Optional: Sectors to avoid
      }
    }
    ```

    *Example Server Response:*
    ```json
    {
      "id": "se1f2a3b4-c5d6-e7f8-a9b0-c1d2e3f4a5b6",
      "ts": "2025-11-07T15:20:00.100Z",
      "reply_to": "e1f2a3b4-c5d6-e7f8-a9b0-c1d2e3f4a5b6",
      "status": "ok",
      "type": "move.path_v1",
      "data": {
        "steps": [1, 2, 5, 8, 10],
        "total_cost": 4
      }
    }
    ```

*   `move.autopilot.stop`: Stop server-side autopilot. (NIY - Not Yet Implemented)

    *Example Client Request:*
    ```json
    {
      "id": "f1a2b3c4-d5e6-f7a8-b9c0-d1e2f3a4b5c6",
      "ts": "2025-11-07T15:21:00.000Z",
      "command": "move.autopilot.stop",
      "auth": { "session": "eyJhbGciOi..." },
      "data": {}
    }
    ```

    *Example Server Response (Error):*
    ```json
    {
      "id": "sf1a2b3c4-d5e6-f7a8-b9c0-d1e2f3a4b5c6",
      "ts": "2025-11-07T15:21:00.100Z",
      "reply_to": "f1a2b3c4-d5e6-f7a8-b9c0-d1e2f3a4b5c6",
      "status": "error",
      "type": "move.autopilot.stop",
      "error": {
        "code": 1101,
        "message": "Not Implemented"
      }
    }
    ```

*   `move.autopilot.status`: Get current server-side autopilot status. (NIY - Not Yet Implemented)

    *Example Client Request:*
    ```json
    {
      "id": "g1h2i3j4-k5l6-m7n8-o9p0-q1r2s3t4u5v6",
      "ts": "2025-11-07T15:22:00.000Z",
      "command": "move.autopilot.status",
      "auth": { "session": "eyJhbGciOi..." },
      "data": {}
    }
    ```

    *Example Server Response (Error):*
    ```json
    {
      "id": "sg1h2i3j4-k5l6-m7n8-o9p0-q1r2s3t4u5v6",
      "ts": "2025-11-07T15:22:00.100Z",
      "reply_to": "g1h2i3j4-k5l6-m7n8-o9p0-q1r2s3t4u5v6",
      "status": "error",
      "type": "move.autopilot.status",
      "error": {
        "code": 1101,
        "message": "Not Implemented"
      }
    }
    ```

*   `nav.avoid.add`: Add a sector to the player's avoid list.

    *Example Client Request:*
    ```json
    {
      "id": "f1a2b3c4-d5e6-f7a8-b9c0-d1e2f3a4b5c6",
      "ts": "2025-11-07T15:25:00.000Z",
      "command": "nav.avoid.add",
      "auth": { "session": "eyJhbGciOi..." },
      "data": {
        "sector_id": 666
      }
    }
    ```

    *Example Server Response:*
    ```json
    {
      "id": "sf1a2b3c4-d5e6-f7a8-b9c0-d1e2f3a4b5c6",
      "ts": "2025-11-07T15:25:00.100Z",
      "reply_to": "f1a2b3c4-d5e6-f7a8-b9c0-d1e2f3a4b5c6",
      "status": "ok",
      "type": "nav.avoid.added",
      "data": {
        "avoid_list": [666]
      }
    }
    ```

*   `nav.avoid.remove`: Remove a sector from the player's avoid list.

    *Example Client Request:*
    ```json
    {
      "id": "f1a2b3c4-d5e6-f7a8-b9c0-d1e2f3a4b5c6",
      "ts": "2025-11-07T15:26:00.000Z",
      "command": "nav.avoid.remove",
      "auth": { "session": "eyJhbGciOi..." },
      "data": {
        "sector_id": 666
      }
    }
    ```

    *Example Server Response:*
    ```json
    {
      "id": "sf1a2b3c4-d5e6-f7a8-b9c0-d1e2f3a4b5c6",
      "ts": "2025-11-07T15:26:00.100Z",
      "reply_to": "f1a2b3c4-d5e6-f7a8-b9c0-d1e2f3a4b5c6",
      "status": "ok",
      "type": "nav.avoid.removed",
      "data": {
        "avoid_list": []
      }
    }
    ```

*   `nav.avoid.list`: Get the player's current avoid list.

    *Example Client Request:*
    ```json
    {
      "id": "f1a2b3c4-d5e6-f7a8-b9c0-d1e2f3a4b5c6",
      "ts": "2025-11-07T15:27:00.000Z",
      "command": "nav.avoid.list",
      "auth": { "session": "eyJhbGciOi..." },
      "data": {}
    }
    ```

    *Example Server Response:*
    ```json
    {
      "id": "sf1a2b3c4-d5e6-f7a8-b9c0-d1e2f3a4b5c6",
      "ts": "2025-11-07T15:27:00.100Z",
      "reply_to": "f1a2b3c4-d5e6-f7a8-b9c0-d1e2f3a4b5c6",
      "status": "ok",
      "type": "nav.avoid.list",
      "data": {
        "avoid_list": [666, 777]
      }
    }
    ```

*   `nav.bookmark.add`: Add a location to the player's bookmarks.

    *Example Client Request:*
    ```json
    {
      "id": "g1b2c3d4-e5f6-g7b8-c9d0-e1f2a3b4c5d6",
      "ts": "2025-11-07T15:30:00.000Z",
      "command": "nav.bookmark.add",
      "auth": { "session": "eyJhbGciOi..." },
      "data": {
        "sector_id": 101,
        "name": "My Home Base"
      }
    }
    ```

    *Example Server Response:*
    ```json
    {
      "id": "sg1b2c3d4-e5f6-g7b8-c9d0-e1f2a3b4c5d6",
      "ts": "2025-11-07T15:30:00.100Z",
      "reply_to": "g1b2c3d4-e5f6-g7b8-c9d0-e1f2a3b4c5d6",
      "status": "ok",
      "type": "nav.bookmark.added",
      "data": {
        "bookmarks": [
          { "sector_id": 101, "name": "My Home Base" }
        ]
      }
    }
    ```

*   `nav.bookmark.remove`: Remove a location from the player's bookmarks.

    *Example Client Request:*
    ```json
    {
      "id": "g1b2c3d4-e5f6-g7b8-c9d0-e1f2a3b4c5d6",
      "ts": "2025-11-07T15:31:00.000Z",
      "command": "nav.bookmark.remove",
      "auth": { "session": "eyJhbGciOi..." },
      "data": {
        "sector_id": 101
      }
    }
    ```

    *Example Server Response:*
    ```json
    {
      "id": "sg1b2c3d4-e5f6-g7b8-c9d0-e1f2a3b4c5d6",
      "ts": "2025-11-07T15:31:00.100Z",
      "reply_to": "g1b2c3d4-e5f6-g7b8-c9d0-e1f2a3b4c5d6",
      "status": "ok",
      "type": "nav.bookmark.removed",
      "data": {
        "bookmarks": []
      }
    }
    ```

*   `nav.bookmark.list`: Get the player's current bookmarks.

    *Example Client Request:*
    ```json
    {
      "id": "g1b2c3d4-e5f6-g7b8-c9d0-e1f2a3b4c5d6",
      "ts": "2025-11-07T15:32:00.000Z",
      "command": "nav.bookmark.list",
      "auth": { "session": "eyJhbGciOi..." },
      "data": {}
    }
    ```

    *Example Server Response:*
    ```json
    {
      "id": "sg1b2c3d4-e5f6-g7b8-c9d0-e1f2a3b4c5d6",
      "ts": "2025-11-07T15:32:00.100Z",
      "reply_to": "g1b2c3d4-e5f6-g7b8-c9d0-e1f2a3b4c5d6",
      "status": "ok",
      "type": "nav.bookmark.list",
      "data": {
        "bookmarks": [
          { "sector_id": 101, "name": "My Home Base" },
          { "sector_id": 202, "name": "Mining Colony" }
        ]
      }
    }
    ```

### 4.5. Ports & Trade (Commodity Exchange)
### 4.5.1. Player Trade Preferences

*   `player.set_trade_account_preference`: Set the player's preferred account for trade transactions.

    *Example Client Request:*
    ```json
    {
      "id": "a1b2c3d4-e5f6-a7b8-c9d0-e1f2a3b4c5d6",
      "ts": "2025-11-07T16:00:00.000Z",
      "command": "player.set_trade_account_preference",
      "auth": { "session": "eyJhbGciOi..." },
      "data": {
        "preference": 1 // 0 = Petty Cash (ship holds), 1 = Bank Account
      }
    }
    ```

    *Example Server Response:*
    ```json
    {
      "id": "sa1b2c3d4-e5f6-a7b8-c9d0-e1f2a3b4c5d6",
      "ts": "2025-11-07T16:00:00.100Z",
      "reply_to": "a1b2c3d4-e5f6-a7b8-c9d0-e1f2a3b4c5d6",
      "status": "ok",
      "type": "player.set_trade_account_preference",
      "data": {
        "message": "Preference updated successfully."
      }
    }
    ```

*   `trade.port_info`: Get info on a port's statically sold goods.

    *Example Client Request:*
    ```json
    {
      "id": "a1b2c3d4-e5f6-a7b8-c9d0-e1f2a3b4c5d6",
      "ts": "2025-11-07T16:00:00.000Z",
      "command": "trade.port_info",
      "auth": { "session": "eyJhbGciOi..." },
      "data": {
        "port_id": 1,    // Optional: Either port_id or sector_id must be provided
        "sector_id": 1   // Optional: Either port_id or sector_id must be provided
      }
    }
    ```

    *Example Server Response:*
    ```json
    {
      "id": "sa1b2c3d4-e5f6-a7b8-c9d0-e1f2a3b4c5d6",
      "ts": "2025-11-07T16:00:00.100Z",
      "reply_to": "a1b2c3d4-e5f6-a7b8-c9d0-e1f2a3b4c5d6",
      "status": "ok",
      "type": "trade.port_info",
      "data": {
        "port": {
          "id": 1,
          "number": 1,
          "name": "Earth Trading Post",
          "sector": 1,
          "size": 5,
          "techlevel": 8,
          "ore_on_hand": 10000,
          "organics_on_hand": 20000,
          "equipment_on_hand": 5000,
          "petty_cash": 123456, // Note: This is from the same DB column as 'credits'
          "credits": 123456,    // Note: This is from the same DB column as 'petty_cash'
          "type": 1 // e.g., 1 for Stardock
        }
      }
    }
    ```


*   `trade.buy`: Buy commodities from a port.

    *Example Client Request:*
    ```json
    {
      "id": "b1c2d3e4-f5a6-b7c8-d9e0-f1a2b3c4d5e6",
      "ts": "2025-11-07T16:05:00.000Z",
      "command": "trade.buy",
      "auth": { "session": "eyJhbGciOi..." },
      "data": {
        "account": 1, // Optional: 0 = Petty Cash (ship holds), 1 = Bank Account. Default is Petty Cash.
        "sector_id": 1,
        "port_id": 1, // Optional: If omitted, port in current sector is used.
        "items": [
          { "commodity": "ore", "quantity": 100 },
          { "commodity": "equipment", "quantity": 50 }
        ],
        "idempotency_key": "buy_txn_12345" // Unique key for idempotent requests
      }
    }
    ```

    *Example Server Response:*
    ```json
    {
      "id": "sb1c2d3e4-f5a6-b7c8-d9e0-f1a2b3c4d5e6",
      "ts": "2025-11-07T16:05:00.100Z",
      "reply_to": "b1c2d3e4-f5a6-b7c8-d9e0-f1a2b3c4d5e6",
      "status": "ok",
      "type": "trade.buy_receipt_v1",
      "data": {
        "sector_id": 1,
        "port_id": 1,
        "player_id": 12345,
        "lines": [
          { "commodity": "ore", "quantity": 100, "unit_price": 55, "value": 5500 },
          { "commodity": "equipment", "quantity": 50, "unit_price": 120, "value": 6000 }
        ],
        "credits_remaining": 988500,
        "total_cost": 11500
      }
    }
    ```


*   `trade.sell`: Sell commodities to a port.

    *Example Client Request:*
    ```json
    {
      "id": "c1d2e3f4-a5b6-c7d8-e9f0-a1b2c3d4e5f6",
      "ts": "2025-11-07T16:06:00.000Z",
      "command": "trade.sell",
      "auth": { "session": "eyJhbGciOi..." },
      "data": {
        "account": 1, // Optional: 0 = Petty Cash (ship holds), 1 = Bank Account. Default is Petty Cash.
        "sector_id": 1,
        "items": [
          { "commodity": "ore", "quantity": 50 },
          { "commodity": "organics", "quantity": 20 }
        ],
        "idempotency_key": "sell_txn_12345" // Unique key for idempotent requests
      }
    }
    ```

    *Example Server Response:*
    ```json
    {
      "id": "sc1d2e3f4-a5b6-c7d8-e9f0-a1b2c3d4e5f6",
      "ts": "2025-11-07T16:06:00.100Z",
      "reply_to": "c1d2e3f4-a5b6-c7d8-e9f0-a1b2c3d4e5f6",
      "status": "ok",
      "type": "trade.sell_receipt_v1",
      "data": {
        "sector_id": 1,
        "port_id": 1,
        "player_id": 12345,
        "lines": [
          { "commodity": "ore", "quantity": 50, "unit_price": 50, "value": 2500 },
          { "commodity": "organics", "quantity": 20, "unit_price": 10, "value": 200 }
        ],
        "credits_remaining": 990000,
        "total_credits": 2700
      }
    }
    ```

*   `trade.jettison`: Jettison cargo from the player's ship.

    *Example Client Request:*
    ```json
    {
      "id": "d1e2f3a4-b5c6-d7e8-f9a0-b1c2d3e4f5a6",
      "ts": "2025-11-07T16:07:00.000Z",
      "command": "trade.jettison",
      "auth": { "session": "eyJhbGciOi..." },
      "data": {
        "commodity": "ore",
        "quantity": 10
      }
    }
    ```

    *Example Server Response:*
    ```json
    {
      "id": "sd1e2f3a4-b5c6-d7e8-f9a0-b1c2d3e4f5a6",
      "ts": "2025-11-07T16:07:00.100Z",
      "reply_to": "d1e2f3a4-b5c6-d7e8-f9a0-b1c2d3e4f5a6",
      "status": "ok",
      "type": "ship.jettisoned",
      "data": {
        "remaining_cargo": [
          { "commodity": "ore", "quantity": 90 },
          { "commodity": "organics", "quantity": 20 }
        ]
      }
    }
    ```

*   `trade.history`: Get player's trade history.

    *Example Client Request:*
    ```json
    {
      "id": "c1d2e3f4-a5b6-c7d8-e9f0-a1b2c3d4e5f6",
      "ts": "2025-11-07T16:10:00.000Z",
      "command": "trade.history",
      "auth": { "session": "eyJhbGciOi..." },
      "data": {
        "cursor": "2025-11-07T16:05:00.000Z_trade_1", // Optional: For pagination, format "timestamp_id"
        "limit": 10 // Optional: Default 20, max 50
      }
    }
    ```

    *Example Server Response:*
    ```json
    {
      "id": "sc1d2e3f4-a5b6-c7d8-e9f0-a1b2c3d4e5f6",
      "ts": "2025-11-07T16:10:00.100Z",
      "reply_to": "c1d2e3f4-a5b6-c7d8-e9f0-a1b2c3d4e5f6",
      "status": "ok",
      "type": "trade.history",
      "data": {
        "history": [
          { "timestamp": 1678886700000, "id": 1, "port_id": 1, "commodity": "ore", "units": 100, "price_per_unit": 55.00, "action": "buy" },
          { "timestamp": 1678886600000, "id": 2, "port_id": 1, "commodity": "equipment", "units": 50, "price_per_unit": 120.00, "action": "sell" }
        ],
        "next_cursor": "2025-11-07T16:05:00.000Z_trade_2" // Or null if no more results
      }
    }
    ```


*   `trade.quote`: Get a price quote for a commodity without trading.

    *Example Client Request:*
    ```json
    {
      "id": "d1e2f3a4-b5c6-d7e8-f9a0-b1c2d3e4f5a6",
      "ts": "2025-11-07T16:15:00.000Z",
      "command": "trade.quote",
      "auth": { "session": "eyJhbGciOi..." },
      "data": {
        "port_id": 1,
        "commodity": "ore", // Supported: "ore", "organics", "equipment", "SLV", "WPN", "DRG"
        "quantity": 100
      }
    }
    ```

    *Example Server Response:*
    ```json
    {
      "id": "sd1e2f3a4-b5c6-d7e8-f9a0-b1c2d3e4f5a6",
      "ts": "2025-11-07T16:15:00.100Z",
      "reply_to": "d1e2f3d4-b5c6-d7e8-f9a0-b1c2d3e4f5a6",
      "status": "ok",
      "type": "trade.quote",
      "data": {
        "port_id": 1,
        "commodity": "ore",
        "quantity": 100,
        "buy_price": 55.00, // float
        "sell_price": 50.00, // float
        "total_buy_price": 5500, // integer
        "total_sell_price": 5000 // integer
      }
    }
    ```


*   `trade.offer`: Create a trade offer to another player. (NIY - Not Yet Implemented)

    *Example Client Request:*
    ```json
    {
      "id": "e1f2a3b4-c5d6-e7f8-a9b0-c1d2e3f4a5b6",
      "ts": "2025-11-07T16:16:00.000Z",
      "command": "trade.offer",
      "auth": { "session": "eyJhbGciOi..." },
      "data": {
        "to_player_id": 123,
        "offered_items": [
          { "commodity": "ore", "quantity": 100 }
        ],
        "requested_items": [
          { "commodity": "organics", "quantity": 50 }
        ]
      }
    }
    ```

    *Example Server Response (Error):*
    ```json
    {
      "id": "se1f2a3b4-c5d6-e7f8-a9b0-c1d2e3f4a5b6",
      "ts": "2025-11-07T16:16:00.100Z",
      "reply_to": "e1f2a3b4-c5d6-e7f8-a9b0-c1d2e3f4a5b6",
      "status": "error",
      "type": "trade.offer",
      "error": {
        "code": 1101,
        "message": "Not Implemented"
      }
    }
    ```

*   `trade.accept`: Accept a trade offer. (NIY - Not Yet Implemented)

    *Example Client Request:*
    ```json
    {
      "id": "f1a2b3c4-d5e6-f7a8-b9c0-d1e2f3a4b5c6",
      "ts": "2025-11-07T16:17:00.000Z",
      "command": "trade.accept",
      "auth": { "session": "eyJhbGciOi..." },
      "data": {
        "offer_id": "offer_123"
      }
    }
    ```

    *Example Server Response (Error):*
    ```json
    {
      "id": "sf1a2b3c4-d5e6-f7a8-b9c0-d1e2f3a4b5c6",
      "ts": "2025-11-07T16:17:00.100Z",
      "reply_to": "f1a2b3c4-d5e6-f7a8-b9c0-d1e2f3a4b5c6",
      "status": "error",
      "type": "trade.accept",
      "error": {
        "code": 1101,
        "message": "Not Implemented"
      }
    }
    ```

*   `trade.cancel`: Cancel a trade offer. (NIY - Not Yet Implemented)

    *Example Client Request:*
    ```json
    {
      "id": "g1h2i3j4-k5l6-m7n8-o9p0-q1r2s3t4u5v6",
      "ts": "2025-11-07T16:18:00.000Z",
      "command": "trade.cancel",
      "auth": { "session": "eyJhbGciOi..." },
      "data": {
        "offer_id": "offer_123"
      }
    }
    ```

    *Example Server Response (Error):*
    ```json
    {
      "id": "sg1h2i3j4-k5l6-m7n8-o9p0-q1r2s3t4u5v6",
      "ts": "2025-11-07T16:18:00.100Z",
      "reply_to": "g1h2i3j4-k5l6-m7n8-o9p0-q1r2s3t4u5v6",
      "status": "error",
      "type": "trade.cancel",
      "error": {
        "code": 1101,
        "message": "Not Implemented"
      }
    }
    ```

**Dynamic Market Commands:**
*   `market.orders.list`: List open buy/sell orders on the galactic commodity exchange. (NIY - Not Yet Implemented)

    *Example Client Request:*
    ```json
    {
      "id": "e1f2a3b4-c5d6-e7f8-a9b0-c1d2e3f4a5b6",
      "ts": "2025-11-07T16:20:00.000Z",
      "command": "market.orders.list",
      "auth": { "session": "eyJhbGciOi..." },
      "data": {
        "commodity": "ore",
        "side": "buy"
      }
    }
    ```

    *Example Server Response (Error):*
    ```json
    {
      "id": "se1f2a3b4-c5d6-e7f8-a9b0-c1d2e3f4a5b6",
      "ts": "2025-11-07T16:20:00.100Z",
      "reply_to": "e1f2a3b4-c5d6-e7f8-a9b0-c1d2e3f4a5b6",
      "status": "error",
      "type": "market.orders.list",
      "error": {
        "code": 1101,
        "message": "Not Implemented"
      }
    }
    ```

*   `market.orders.create`: Place a new buy or sell order on the exchange. (NIY - Not Yet Implemented)

    *Example Client Request:*
    ```json
    {
      "id": "f1a2b3c4-d5e6-f7a8-b9c0-d1e2f3a4b5c6",
      "ts": "2025-11-07T16:25:00.000Z",
      "command": "market.orders.create",
      "auth": { "session": "eyJhbGciOi..." },
      "data": {
        "commodity": "ore",
        "side": "sell",
        "quantity": 500,
        "price": "56.00"
      }
    }
    ```

    *Example Server Response (Error):*
    ```json
    {
      "id": "sf1a2b3c4-d5e6-f7a8-b9c0-d1e2f3a4b5c6",
      "ts": "2025-11-07T16:25:00.100Z",
      "reply_to": "f1a2b3c4-d5e6-f7a8-b9c0-d1e2f3a4b5c6",
      "status": "error",
      "type": "market.orders.create",
      "error": {
        "code": 1101,
        "message": "Not Implemented"
      }
    }
    ```

*   `market.orders.cancel`: Cancel an open order. (NIY - Not Yet Implemented)

    *Example Client Request:*
    ```json
    {
      "id": "g1b2c3d4-e5f6-g7b8-c9d0-e1f2a3b4c5d6",
      "ts": "2025-11-07T16:30:00.000Z",
      "command": "market.orders.cancel",
      "auth": { "session": "eyJhbGciOi..." },
      "data": {
        "order_id": "ord_sell_1"
      }
    }
    ```

    *Example Server Response (Error):*
    ```json
    {
      "id": "sg1b2c3d4-e5f6-g7b8-c9d0-e1f2a3b4c5d6",
      "ts": "2025-11-07T16:30:00.100Z",
      "reply_to": "g1b2c3d4-e5f6-g7b8-c9d0-e1f2a3b4c5d6",
      "status": "error",
      "type": "market.orders.cancel",
      "error": {
        "code": 1101,
        "message": "Not Implemented"
      }
    }
    ```

*   `market.contracts.list`: List futures contracts held by the player on the stock exchange. (NIY - Not Yet Implemented)

    *Example Client Request:*
    ```json
    {
      "id": "h1b2c3d4-e5f6-h7b8-c9d0-e1f2a3b4c5d6",
      "ts": "2025-11-07T16:35:00.000Z",
      "command": "market.contracts.list",
      "auth": { "session": "eyJhbGciOi..." },
      "data": {}
    }
    ```

    *Example Server Response (Error):*
    ```json
    {
      "id": "sh1b2c3d4-e5f6-h7b8-c9d0-e1f2a3b4c5d6",
      "ts": "2025-11-07T16:35:00.100Z",
      "reply_to": "h1b2c3d4-e5f6-h7b8-c9d0-e1f2a3b4c5d6",
      "status": "error",
      "type": "market.contracts.list",
      "error": {
        "code": 1101,
        "message": "Not Implemented"
      }
    }
    ```

*   `market.contracts.buy`/`market.contracts.sell`: Trade commodity futures contracts on the stock exchange. (NIY - Not Yet Implemented)

    *Example Client Request (buy):*
    ```json
    {
      "id": "i1b2c3d4-e5f6-i7b8-c9d0-e1f2a3b4c5d6",
      "ts": "2025-11-07T16:40:00.000Z",
      "command": "market.contracts.buy",
      "auth": { "session": "eyJhbGciOi..." },
      "data": {
        "commodity": "ore",
        "quantity": 50,
        "expiry_ts": "2026-01-01T00:00:00.000Z"
      }
    }
    ```

    *Example Server Response (Error):*
    ```json
    {
      "id": "si1b2c3d4-e5f6-i7b8-c9d0-e1f2a3b4c5d6",
      "ts": "2025-11-07T16:40:00.100Z",
      "reply_to": "i1b2c3d4-e5f6-i7b8-c9d0-e1f2a3b4c5d6",
      "status": "error",
      "type": "market.contracts.buy",
      "error": {
        "code": 1101,
        "message": "Not Implemented"
      }
    }
    ```


### 4.6. Financial Instruments (Loans & Insurance)
*   `loan.offers.list`: List available loan offers from banks. (NIY - Not Yet Implemented)

    *Example Client Request:*
    ```json
    {
      "id": "a1b2c3d4-e5f6-a7b8-c9d0-e1f2a3b4c5d6",
      "ts": "2025-11-07T17:00:00.000Z",
      "command": "loan.offers.list",
      "auth": { "session": "eyJhbGciOi..." },
      "data": {}
    }
    ```

    *Example Server Response (Error):*
    ```json
    {
      "id": "sa1b2c3d4-e5f6-a7b8-c9d0-e1f2a3b4c5d6",
      "ts": "2025-11-07T17:00:00.100Z",
      "reply_to": "a1b2c3d4-e5f6-a7b8-c9d0-e1f2a3b4c5d6",
      "status": "error",
      "type": "loan.offers_list",
      "error": {
        "code": 1101,
        "message": "Not Implemented"
      }
    }
    ```

*   `loan.apply`: Apply for a loan. (NIY - Not Yet Implemented)

    *Example Client Request:*
    ```json
    {
      "id": "b1c2d3e4-f5a6-b7c8-d9e0-f1a2b3c4d5e6",
      "ts": "2025-11-07T17:05:00.000Z",
      "command": "loan.apply",
      "auth": { "session": "eyJhbGciOi..." },
      "data": {
        "offer_id": "loan_offer_1"
      }
    }
    ```

    *Example Server Response (Error):*
    ```json
    {
      "id": "sb1c2d3e4-f5a6-b7c8-d9e0-f1a2b3c4d5e6",
      "ts": "2025-11-07T17:05:00.100Z",
      "reply_to": "b1c2d3e4-f5a6-b7c8-d9e0-f1a2b3c4d5e6",
      "status": "error",
      "type": "loan.apply",
      "error": {
        "code": 1101,
        "message": "Not Implemented"
      }
    }
    ```

*   `loan.accept`: Accept a loan offer. (NIY - Not Yet Implemented)

    *Example Client Request:*
    ```json
    {
      "id": "c1d2e3f4-a5b6-c7d8-e9f0-a1b2c3d4e5f6",
      "ts": "2025-11-07T17:10:00.000Z",
      "command": "loan.accept",
      "auth": { "session": "eyJhbGciOi..." },
      "data": {
        "application_id": "loan_app_1"
      }
    }
    ```

    *Example Server Response (Error):*
    ```json
    {
      "id": "sc1d2e3f4-a5b6-c7d8-e9f0-a1b2c3d4e5f6",
      "ts": "2025-11-07T17:10:00.100Z",
      "reply_to": "c1d2e3f4-a5b6-c7d8-e9f0-a1b2c3d4e5f6",
      "status": "error",
      "type": "loan.accept",
      "error": {
        "code": 1101,
        "message": "Not Implemented"
      }
    }
    ```

*   `loan.repay`: Make a payment on an active loan. (NIY - Not Yet Implemented)

    *Example Client Request:*
    ```json
    {
      "id": "d1e2f3a4-b5c6-d7e8-f9a0-b1c2d3e4f5a6",
      "ts": "2025-11-07T17:15:00.000Z",
      "command": "loan.repay",
      "auth": { "session": "eyJhbGciOi..." },
      "data": {
        "loan_id": "loan_1",
        "amount": "1000.00"
      }
    }
    ```

    *Example Server Response (Error):*
    ```json
    {
      "id": "sd1e2f3a4-b5c6-d7e8-f9a0-b1c2d3e4f5a6",
      "ts": "2025-11-07T17:15:00.100Z",
      "reply_to": "d1e2f3a4-b5c6-d7e8-f9a0-b1c2d3e4f5a6",
      "status": "error",
      "type": "loan.repayment_receipt",
      "error": {
        "code": 1101,
        "message": "Not Implemented"
      }
    }
    ```

*   `loan.list_active`: List player's active loans. (NIY - Not Yet Implemented)

    *Example Client Request:*
    ```json
    {
      "id": "e1f2a3b4-c5d6-e7f8-a9b0-c1d2e3f4a5b6",
      "ts": "2025-11-07T17:20:00.000Z",
      "command": "loan.list_active",
      "auth": { "session": "eyJhbGciOi..." },
      "data": {}
    }
    ```

    *Example Server Response (Error):*
    ```json
    {
      "id": "se1f2a3b4-c5d6-e7f8-a9b0-c1d2e3f4a5b6",
      "ts": "2025-11-07T17:20:00.100Z",
      "reply_to": "e1f2a3b4-c5d6-e7f8-a9b0-c1d2e3f4a5b6",
      "status": "error",
      "type": "loan.list_active",
      "error": {
        "code": 1101,
        "message": "Not Implemented"
      }
    }
    ```

*   `insurance.policies.list`: List available insurance policies. (NIY - Not Yet Implemented)

    *Example Client Request:*
    ```json
    {
      "id": "f1a2b3c4-d5e6-f7a8-b9c0-d1e2f3a4b5c6",
      "ts": "2025-11-07T17:25:00.000Z",
      "command": "insurance.policies.list",
      "auth": { "session": "eyJhbGciOi..." },
      "data": {
        "type": "ship"
      }
    }
    ```

    *Example Server Response (Error):*
    ```json
    {
      "id": "sf1a2b3c4-d5e6-f7a8-b9c0-d1e2f3a4b5c6",
      "ts": "2025-11-07T17:25:00.100Z",
      "reply_to": "f1a2b3c4-d5e6-f7a8-b9c0-d1e2f3a4b5c6",
      "status": "error",
      "type": "insurance.policies.list",
      "error": {
        "code": 1101,
        "message": "Not Implemented"
      }
    }
    ```

*   `insurance.policies.buy`: Purchase an insurance policy for a ship or cargo. (NIY - Not Yet Implemented)

    *Example Client Request:*
    ```json
    {
      "id": "g1b2c3d4-e5f6-g7b8-c9d0-e1f2a3b4c5d6",
      "ts": "2025-11-07T17:30:00.000Z",
      "command": "insurance.policies.buy",
      "auth": { "session": "eyJhbGciOi..." },
      "data": {
        "policy_id": "ins_ship_1"
      }
    }
    ```

    *Example Server Response (Error):*
    ```json
    {
      "id": "sg1b2c3d4-e5f6-g7b8-c9d0-e1f2a3b4c5d6",
      "ts": "2025-11-07T17:30:00.100Z",
      "reply_to": "g1b2c3d4-e5f6-g7b8-c9d0-e1f2a3b4c5d6",
      "status": "error",
      "type": "insurance.policies.buy",
      "error": {
        "code": 1101,
        "message": "Not Implemented"
      }
    }
    ```

*   `insurance.claim.file`: File a claim against an active policy. (NIY - Not Yet Implemented)

    *Example Client Request:*
    ```json
    {
      "id": "h1b2c3d4-e5f6-h7b8-c9d0-e1f2a3b4c5d6",
      "ts": "2025-11-07T17:35:00.000Z",
      "command": "insurance.claim.file",
      "auth": { "session": "eyJhbGciOi..." },
      "data": {
        "active_policy_id": "active_ins_1",
        "details": "My ship was destroyed by pirates in sector 666."
      }
    }
    ```

    *Example Server Response (Error):*
    ```json
    {
      "id": "sh1b2c3d4-e5f6-h7b8-c9d0-e1f2a3b4c5d6",
      "ts": "2025-11-07T17:35:00.100Z",
      "reply_to": "h1b2c3d4-e5f6-h7b8-c9d0-e1f2a3b4c5d6",
      "status": "error",
      "type": "insurance.claim.file",
      "error": {
        "code": 1101,
        "message": "Not Implemented"
      }
    }
    ```

### 4.7. Strategic Investment (Stocks & R&D)
*   `corp.stock.issue`: Issue new shares for a corporation to raise capital (requires leadership). (NIY - Not Yet Implemented)

    *Example Client Request:*
    ```json
    {
      "id": "a1b2c3d4-e5f6-a7b8-c9d0-e1f2a3b4c5d6",
      "ts": "2025-11-07T18:00:00.000Z",
      "command": "corp.stock.issue",
      "auth": { "session": "eyJhbGciOi..." },
      "data": {
        "quantity": 10000,
        "price": "100.00"
      }
    }
    ```

    *Example Server Response (Error):*
    ```json
    {
      "id": "sa1b2c3d4-e5f6-a7b8-c9d0-e1f2a3b3c4d5e6",
      "ts": "2025-11-07T18:00:00.100Z",
      "reply_to": "a1b2c3d4-e5f6-a7b8-c9d0-e1f2a3b4c5d6",
      "status": "error",
      "type": "corp.stock.issue",
      "error": {
        "code": 1101,
        "message": "Not Implemented"
      }
    }
    ```

*   `stock.exchange.list_stocks`: List all publicly traded corporations. (NIY - Not Yet Implemented)

    *Example Client Request:*
    ```json
    {
      "id": "b1c2d3e4-f5a6-b7c8-d9e0-f1a2b3c4d5e6",
      "ts": "2025-11-07T18:05:00.000Z",
      "command": "stock.exchange.list_stocks",
      "auth": { "session": "eyJhbGciOi..." },
      "data": {}
    }
    ```

    *Example Server Response (Error):*
    ```json
    {
      "id": "sb1c2d3e4-f5a6-b7c8-d9e0-f1a2b3c4d5e6",
      "ts": "2025-11-07T18:05:00.100Z",
      "reply_to": "b1c2d3e4-f5a6-b7c8-d9e0-f1a2b3c4d5e6",
      "status": "error",
      "type": "stock.exchange.list_stocks",
      "error": {
        "code": 1101,
        "message": "Not Implemented"
      }
    }
    ```

*   `stock.exchange.orders.create`: Place a buy/sell order for shares of a corporation. (NIY - Not Yet Implemented)

    *Example Client Request:*
    ```json
    {
      "id": "c1d2e3f4-a5b6-c7d8-e9f0-a1b2c3d4e5f6",
      "ts": "2025-11-07T18:10:00.000Z",
      "command": "stock.exchange.orders.create",
      "auth": { "session": "eyJhbGciOi..." },
      "data": {
        "ticker": "MYCORP",
        "side": "buy",
        "quantity": 100,
        "price": "150.00"
      }
    }
    ```

    *Example Server Response (Error):*
    ```json
    {
      "id": "sc1d2e3f4-a5b6-c7d8-e9f0-a1b2c3d4e5f6",
      "ts": "2025-11-07T18:10:00.100Z",
      "reply_to": "c1d2e3f4-a5b6-c7d8-e9f0-a1b2c3d4e5f6",
      "status": "error",
      "type": "stock.exchange.orders.create",
      "error": {
        "code": 1101,
        "message": "Not Implemented"
      }
    }
    ```

*   `stock.exchange.orders.list`: List open stock orders for the player. (NIY - Not Yet Implemented)

    *Example Client Request:*
    ```json
    {
      "id": "e1f2a3b4-c5d6-e7f8-a9b0-c1d2e3f4a5b6",
      "ts": "2025-11-07T18:20:00.000Z",
      "command": "stock.exchange.orders.list",
      "auth": { "session": "eyJhbGciOi..." },
      "data": {}
    }
    ```

    *Example Server Response (Error):*
    ```json
    {
      "id": "se1f2a3b4-c5d6-e7f8-a9b0-c1d2e3f4a5b6",
      "ts": "2025-11-07T18:20:00.100Z",
      "reply_to": "e1f2a3b4-c5d6-e7f8-a9b0-c1d2e3f4a5b6",
      "status": "error",
      "type": "stock.exchange.orders.list",
      "error": {
        "code": 1101,
        "message": "Not Implemented"
      }
    }
    ```


*   `stock.portfolio.list`: View the player's current stock portfolio.

    *Example Client Request:*
    ```json
    {
      "id": "e1f2a3b4-c5d6-e7f8-a9b0-c1d2e3f4a5b6",
      "ts": "2025-11-07T18:20:00.000Z",
      "command": "stock.portfolio.list",
      "auth": { "session": "eyJhbGciOi..." },
      "data": {}
    }
    ```

    *Example Server Response:*
    ```json
    {
      "id": "se1f2a3b4-c5d6-e7f8-a9b0-c1d2e3f4a5b6",
      "ts": "2025-11-07T18:20:00.100Z",
      "reply_to": "e1f2a3b4-c5d6-e7f8-a9b0-c1d2e3f4a5b6",
      "status": "ok",
      "type": "stock.portfolio",
      "data": {
        "holdings": [
          { "ticker": "MYCORP", "quantity": 200, "average_price": "140.00", "current_value": "30000.00" }
        ]
      }
    }
    ```

*   `research.projects.list`: List active R&D projects available for funding.

    *Example Client Request:*
    ```json
    {
      "id": "f1a2b3c4-d5e6-f7a8-b9c0-d1e2f3a4b5c6",
      "ts": "2025-11-07T18:25:00.000Z",
      "command": "research.projects.list",
      "auth": { "session": "eyJhbGciOi..." },
      "data": {}
    }
    ```

    *Example Server Response:*
    ```json
    {
      "id": "sf1a2b3c4-d5e6-f7a8-b9c0-d1e2f3a4b5c6",
      "ts": "2025-11-07T18:25:00.100Z",
      "reply_to": "f1a2b3c4-d5e6-f7a8-b9c0-d1e2f3a4b5c6",
      "status": "ok",
      "type": "research.projects_list",
      "data": {
        "projects": [
          { "project_id": "rd_1", "name": "Advanced Warp Drive", "goal": "10000000.00", "funded": "2500000.00", "eta_ts": "2026-01-01T00:00:00.000Z" }
        ]
      }
    }
    ```

*   `research.projects.fund`: Contribute credits to a large-scale R&D project.

    *Example Client Request:*
    ```json
    {
      "id": "g1b2c3d4-e5f6-g7b8-c9d0-e1f2a3b4c5d6",
      "ts": "2025-11-07T18:30:00.000Z",
      "command": "research.projects.fund",
      "auth": { "session": "eyJhbGciOi..." },
      "data": {
        "project_id": "rd_1",
        "amount": "10000.00"
      }
    }
    ```

    *Example Server Response:*
    ```json
    {
      "id": "sg1b2c3d4-e5f6-g7b8-c9d0-e1f2a3b4c5d6",
      "ts": "2025-11-07T18:30:00.100Z",
      "reply_to": "g1b2c3d4-e5f6-g7b8-c9d0-e1f2a3b4c5d6",
      "status": "ok",
      "type": "research.funding_receipt",
      "data": {
        "project_id": "rd_1",
        "amount_funded": "10000.00",
        "total_funded": "2510000.00"
      }
    }
    ```

### 4.8. Ship, Planets & Combat
*   `ship.status`/`ship.info`: Get status of the player's ship.

    *Example Client Request:*
    ```json
    {
      "id": "a1b2c3d4-e5f6-a7b8-c9d0-e1f2a3b4c5d6",
      "ts": "2025-11-07T19:00:00.000Z",
      "command": "ship.status",
      "auth": { "session": "eyJhbGciOi..." },
      "data": {}
    }
    ```

    *Example Server Response:*
    ```json
    {
      "id": "sa1b2c3d4-e5f6-a7b8-c9d0-e1f2a3b4c5d6",
      "ts": "2025-11-07T19:00:00.100Z",
      "reply_to": "a1b2c3d4-e5f6-a7b8-c9d0-e1f2a3b4c5d6",
      "status": "ok",
      "type": "ship.status",
      "data": {
        "ship_id": 123,
        "name": "My Ship",
        "hull": 95,
        "shields": 100,
        "cargo": [
          { "commodity": "ore", "quantity": 50 }
        ]
      }
    }
    ```

*   `ship.info`: Get detailed information about the player's ship.

    *Example Client Request:*
    ```json
    {
      "id": "a1b2c3d4-e5f6-a7b8-c9d0-e1f2a3b4c5d6",
      "ts": "2025-11-07T19:00:00.000Z",
      "command": "ship.info",
      "auth": { "session": "eyJhbGciOi..." },
      "data": {}
    }
    ```

    *Example Server Response:*
    ```json
    {
      "id": "sa1b2c3d4-e5f6-a7b8-c9d0-e1f2a3b4c5d6",
      "ts": "2025-11-07T19:00:00.100Z",
      "reply_to": "a1b2c3d4-e5f6-a7b8-c9d0-e1f2a3b4c5d6",
      "status": "ok",
      "type": "ship.info",
      "data": {
        "ship_id": 123,
        "name": "My Ship",
        "type": "Freighter",
        "holds_capacity": 100,
        "holds_current": 50,
        "cargo": [
          { "commodity": "ore", "quantity": 50 }
        ],
        "location": {
          "sector_id": 1,
          "sector_name": "Sol"
        }
      }
    }
    ```

*   `ship.upgrade`: Upgrade ship components at a shipyard.

    *Example Client Request:*
    ```json
    {
      "id": "b1c2d3e4-f5a6-b7c8-d9e0-f1a2b3c4d5e6",
      "ts": "2025-11-07T19:05:00.000Z",
      "command": "ship.upgrade",
      "auth": { "session": "eyJhbGciOi..." },
      "data": {
        "component": "shields",
        "level": 2
      }
    }
    ```

    *Example Server Response:*
    ```json
    {
      "id": "sb1c2d3e4-f5a6-b7c8-d9e0-f1a2b3c4d5e6",
      "ts": "2025-11-07T19:05:00.100Z",
      "reply_to": "b1c2d3e4-f5a6-b7c8-d9e0-f1a2b3c4d5e6",
      "status": "ok",
      "type": "ship.upgraded",
      "data": {
        "component": "shields",
        "new_level": 2
      }
    }
    ```

*   `ship.jettison`: Jettison cargo into space.

    *Example Client Request:*
    ```json
    {
      "id": "c1d2e3f4-a5b6-c7d8-e9f0-a1b2c3d4e5f6",
      "ts": "2025-11-07T19:10:00.000Z",
      "command": "ship.jettison",
      "auth": { "session": "eyJhbGciOi..." },
      "data": {
        "commodity": "ore",
        "quantity": 10
      }
    }
    ```

    *Example Server Response:*
    ```json
    {
      "id": "sc1d2e3f4-a5b6-c7d8-e9f0-a1b2c3d4e5f6",
      "ts": "2025-11-07T19:10:00.100Z",
      "reply_to": "c1d2e3f4-a5b6-c7d8-e9f0-a1b2c3d4e5f6",
      "status": "ok",
      "type": "ship.jettisoned",
      "data": {
        "remaining_cargo": [
          { "commodity": "ore", "quantity": 40 }
        ]
      }
    }
    ```

*   `planet.create`: Create a new planet.

    *Example Client Request:*
    ```json
    {
      "id": "d1e2f3a4-b5c6-d7e8-f9a0-b1c2d3e4f5a6",
      "ts": "2025-11-07T19:15:00.000Z",
      "command": "planet.create",
      "auth": { "session": "eyJhbGciOi..." },
      "data": {
        "name": "My Planet",
        "type": "terran"
      }
    }
    ```

    *Example Server Response:*
    ```json
    {
      "id": "sd1e2f3a4-b5c6-d7e8-f9a0-b1c2d3e4f5a6",
      "ts": "2025-11-07T19:15:00.100Z",
      "reply_to": "d1e2f3a4-b5c6-d7e8-f9a0-b1c2d3e4f5a6",
      "status": "ok",
      "type": "planet.created",
      "data": {
        "planet_id": 101
      }
    }
    ```

*   `planet.list_mine`: List planets owned by the player.

    *Example Client Request:*
    ```json
    {
      "id": "e1f2a3b4-c5d6-e7f8-a9b0-c1d2e3f4a5b6",
      "ts": "2025-11-07T19:20:00.000Z",
      "command": "planet.list_mine",
      "auth": { "session": "eyJhbGciOi..." },
      "data": {}
    }
    ```

    *Example Server Response:*
    ```json
    {
      "id": "se1f2a3b4-c5d6-e7f8-a9b0-c1d2e3f4a5b6",
      "ts": "2025-11-07T19:20:00.100Z",
      "reply_to": "e1f2a3b4-c5d6-e7f8-a9b0-c1d2e3f4a5b6",
      "status": "ok",
      "type": "planet.my_planets",
      "data": {
        "planets": [
          { "planet_id": 101, "name": "My Planet", "sector_id": 1 }
        ]
      }
    }
    ```

*   `planet.deposit`/`planet.withdraw`: Transfer goods between ship and planet.

    *Example Client Request (deposit):*
    ```json
    {
      "id": "f1a2b3c4-d5e6-f7a8-b9c0-d1e2f3a4b5c6",
      "ts": "2025-11-07T19:25:00.000Z",
      "command": "planet.deposit",
      "auth": { "session": "eyJhbGciOi..." },
      "data": {
        "planet_id": 101,
        "commodity": "ore",
        "quantity": 20
      }
    }
    ```

    *Example Server Response (deposit):*
    ```json
    {
      "id": "sf1a2b3c4-d5e6-f7a8-b9c0-d1e2f3a4b5c6",
      "ts": "2025-11-07T19:25:00.100Z",
      "reply_to": "f1a2b3c4-d5e6-f7a8-b9c0-d1e2f3a4b5c6",
      "status": "ok",
      "type": "planet.transfer_complete",
      "data": {
        "ship_cargo": [
          { "commodity": "ore", "quantity": 20 }
        ],
        "planet_storage": [
          { "commodity": "ore", "quantity": 20 }
        ]
      }
    }
    ```

*   `combat.attack`: Attack a target (ship, planet, fighters).

    *Example Client Request:*
    ```json
    {
      "id": "g1b2c3d4-e5f6-g7b8-c9d0-e1f2a3b4c5d6",
      "ts": "2025-11-07T19:30:00.000Z",
      "command": "combat.attack",
      "auth": { "session": "eyJhbGciOi..." },
      "data": {
        "target_id": 456
      }
    }
    ```

    *Example Server Response:*
    ```json
    {
      "id": "sg1b2c3d4-e5f6-g7b8-c9d0-e1f2a3b4c5d6",
      "ts": "2025-11-07T19:30:00.100Z",
      "reply_to": "g1b2c3d4-e5f6-g7b8-c9d0-e1f2a3b4c5d6",
      "status": "ok",
      "type": "combat.attack_initiated",
      "data": {
        "target_id": 456
      }
    }
    ```

*   `combat.deploy_fighters`: Deploy fighters in the current sector.

    *Example Client Request:*
    ```json
    {
      "id": "h1b2c3d4-e5f6-h7b8-c9d0-e1f2a3b4c5d6",
      "ts": "2025-11-07T19:35:00.000Z",
      "command": "combat.deploy_fighters",
      "auth": { "session": "eyJhbGciOi..." },
      "data": {
        "quantity": 10
      }
    }
    ```

    *Example Server Response:*
    ```json
    {
      "id": "sh1b2c3d4-e5f6-h7b8-c9d0-e1f2a3b4c5d6",
      "ts": "2025-11-07T19:35:00.100Z",
      "reply_to": "h1b2c3d4-e5f6-h7b8-c9d0-e1f2a3b4c5d6",
      "status": "ok",
      "type": "combat.fighters_deployed",
      "data": {
        "quantity": 10
      }
    }
    ```

*   `combat.lay_mines`: Lay mines in the current sector.

    *Example Client Request:*
    ```json
    {
      "id": "i1b2c3d4-e5f6-i7b8-c9d0-e1f2a3b4c5d6",
      "ts": "2025-11-07T19:40:00.000Z",
      "command": "combat.lay_mines",
      "auth": { "session": "eyJhbGciOi..." },
      "data": {
        "quantity": 5
      }
    }
    ```

    *Example Server Response:*
    ```json
    {
      "id": "si1b2c3d4-e5f6-i7b8-c9d0-e1f2a3b4c5d6",
      "ts": "2025-11-07T19:40:00.100Z",
      "reply_to": "i1b2c3d4-e5f6-i7b8-c9d0-e1f2a3b4c5d6",
      "status": "ok",
      "type": "combat.mines_laid",
      "data": {
        "quantity": 5
      }
    }
    ```

---

## 5. Server-to-Client Events

### 5.1. Economic & Financial Events
*   `bank.transaction.completed`: A bank transaction was processed.

    *Example Server Event:*
    ```json
    {
      "id": "evt-1",
      "ts": "2025-11-07T20:00:00.000Z",
      "event": "bank.transaction.completed",
      "data": {
        "kind": "deposit",
        "amount": "1000.00",
        "new_balance": "12500.00"
      }
    }
    ```

*   `bank.interest.paid`: Interest was paid into the player's account.

    *Example Server Event:*
    ```json
    {
      "id": "evt-2",
      "ts": "2025-11-08T00:00:00.000Z",
      "event": "bank.interest.paid",
      "data": {
        "amount": "12.34",
        "new_balance": "12512.34"
      }
    }
    ```

*   `market.order.filled`: A commodity order on the exchange was partially or fully filled.

    *Example Server Event:*
    ```json
    {
      "id": "evt-3",
      "ts": "2025-11-07T20:05:00.000Z",
      "event": "market.order.filled",
      "data": {
        "order_id": "ord_buy_1",
        "commodity": "ore",
        "quantity_filled": 50,
        "price": "55.00"
      }
    }
    ```

*   `stock.order.filled`: A stock market order was filled.

    *Example Server Event:*
    ```json
    {
      "id": "evt-4",
      "ts": "2025-11-07T20:10:00.000Z",
      "event": "stock.order.filled",
      "data": {
        "order_id": "stock_ord_1",
        "ticker": "MYCORP",
        "quantity_filled": 50,
        "price": "150.00"
      }
    }
    ```

*   `bounty.claimed`: A bounty posted by the player has been claimed.

    *Example Server Event:*
    ```json
    {
      "id": "evt-5",
      "ts": "2025-11-07T20:15:00.000Z",
      "event": "bounty.claimed",
      "data": {
        "bounty_id": "bounty_123",
        "reward": "50000.00",
        "claimed_by": "Hunter"
      }
    }
    ```

*   `fine.issued`: A new fine has been issued to the player.

    *Example Server Event:*
    ```json
    {
      "id": "evt-6",
      "ts": "2025-11-07T20:20:00.000Z",
      "event": "fine.issued",
      "data": {
        "fine_id": "fine_2",
        "reason": "Speeding in a no-wake zone",
        "amount": "250.00"
      }
    }
    ```

*   `loan.payment.due`: A loan payment is due soon.

    *Example Server Event:*
    ```json
    {
      "id": "evt-7",
      "ts": "2025-11-13T17:10:00.100Z",
      "event": "loan.payment.due",
      "data": {
        "loan_id": "loan_1",
        "amount_due": "1000.00",
        "due_date": "2025-11-14T17:10:00.100Z"
      }
    }
    ```

*   `insurance.payout.received`: An insurance claim has been paid out.

    *Example Server Event:*
    ```json
    {
      "id": "evt-8",
      "ts": "2025-11-07T20:25:00.000Z",
      "event": "insurance.payout.received",
      "data": {
        "claim_id": "claim_1",
        "payout_amount": "90000.00"
      }
    }
    ```

### 5.2. Standard Events
*   `sector.player_entered`/`sector.player_left`

    *Example Server Event (entered):*
    ```json
    {
      "id": "evt-9",
      "ts": "2025-11-07T21:00:00.000Z",
      "event": "sector.player_entered",
      "data": {
        "player_name": "player2",
        "sector_id": 1
      }
    }
    ```

*   `combat.attacked`

    *Example Server Event:*
    ```json
    {
      "id": "evt-10",
      "ts": "2025-11-07T21:05:00.000Z",
      "event": "combat.attacked",
      "data": {
        "attacker_name": "pirate1",
        "damage": 20
      }
    }
    ```

*   `combat.ship_destroyed`

    *Example Server Event:*
    ```json
    {
      "id": "evt-11",
      "ts": "2025-11-07T21:10:00.000Z",
      "event": "combat.ship_destroyed",
      "data": {
        "killer_name": "pirate1"
      }
    }
    ```

*   `chat.message`

    *Example Server Event:*
    ```json
    {
      "id": "evt-12",
      "ts": "2025-11-07T21:15:00.000Z",
      "event": "chat.message",
      "data": {
        "from": "player2",
        "message": "Hello, world!"
      }
    }
    ```

*   `mail.new`

    *Example Server Event:*
    ```json
    {
      "id": "evt-13",
      "ts": "2025-11-07T21:20:00.000Z",
      "event": "mail.new",
      "data": {
        "from": "System",
        "subject": "Welcome to the game!"
      }
    }
    ```

*   `system.notice`

    *Example Server Event:*
    ```json
    {
      "id": "evt-14",
      "ts": "2025-11-07T21:25:00.000Z",
      "event": "system.notice",
      "data": {
        "message": "Server rebooting in 15 minutes."
      }
    }
    ```

*   `engine.tick`

    *Example Server Event:*
    ```json
    {
      "id": "evt-15",
      "ts": "2025-11-07T21:30:00.000Z",
      "event": "engine.tick",
      "data": {
        "tick": 1234567890
      }
    }
    ```

---

## 6. Server-to-Engine Protocol (S2S - Internal)

This protocol governs communication between the player-facing Server and the backend Game Engine, which handles simulation. It uses two database tables (`engine_events`, `engine_commands`) and a TCP control link.

### 6.1. Server → Engine Events (Facts)
The Server informs the Engine of validated player actions.
*   `player.trade.v1`: Trade occurred.
*   `player.corp_join.v1`: Player joined a corp.
*   `player.illegal_act.v1`: Player committed a crime.
*   `bounty.posted.v1`: A player posted a bounty.
*   `loan.accepted.v1`: A player accepted a loan.
*   `stock.order.placed.v1`: A player placed a stock order.
*   ... and all other significant economic actions.

### 6.2. Engine → Server Commands (Mutations)
The Engine commands the Server to mutate the world state based on its simulation.
*   `port.refresh_stock_and_price.v1`: Update port inventory/prices.
*   `bank.pay_interest.v1`: Pay interest to all eligible players.
*   `player.adjust_credits.v1`: Adjust player credits (for fines, interest, etc.).
*   `corp.adjust_funds.v1`: Adjust corporation funds.
*   `economy.update.v1`: Trigger a macro-economic update.
*   `market.match_orders.v1`: Command to run the order matching engine for commodities/stocks.
*   `loan.process_repayments.v1`: Process automated loan repayments.
*   `server_broadcast.v1`: Send a global notice to all players.

---

## 7. Error Model

The error model uses HTTP-like status codes within the JSON response. `status: "error"` or `status: "refused"` will be accompanied by an `error` object. Code ranges are defined to categorize errors by domain.

*   **1100s**: System/General
*   **1200s**: Auth/Player
*   **1700s**: Trade & Market
*   **2100s**: Banking & Finance (New)
*   **2200s**: Stocks & R&D (New)
*   **2300s**: Loans & Insurance (New)

**New Error Codes for v2.0:**
*   `2100`: Insufficient funds in bank
*   `2101`: Target bank account not found
*   `2102`: Transfer to self not allowed
*   `2103`: Standing order creation failed
*   `2201`: Insufficient shares to sell
*   `2202`: Stock not publicly traded
*   `2301`: Credit rating too low for loan
*   `2302`: Collateral insufficient
*   `2303`: Insurance claim denied

---

## 8. Worked Flow Example: Ship Mortgage

1.  **Player → Server**: `loan.offers.list` to see available ship mortgage options at a shipyard.
2.  **Server → Player**: A list of loan offers with terms (down payment, interest rate, duration).
3.  **Player → Server**: `loan.apply` for a specific ship loan.
    *   **Data**: `{ "ship_class": "Merchant Freighter", "loan_offer_id": 456 }`
4.  **Server**: Validates player credit rating and eligibility.
5.  **Server → Engine**: `loan.application.v1` event.
6.  **Engine**: Processes the application. If approved, it enqueues commands.
7.  **Engine → Server**: `player.adjust_credits.v1` (for the loan principal), `ship.create.v1` (to create the new ship), and `loan.create_record.v1`.
8.  **Server**: Executes commands, creating the ship and updating the player's bank balance and loan record.
9.  **Server → Player**: `loan.approved` event with details of the new ship and loan.
10. **Engine (Later)**: On a schedule, the Engine runs `loan.process_repayments.v1`, which will trigger `player.adjust_credits.v1` commands to deduct payments from player bank accounts.

---

## 9. Full Command & Event Index (v2.0)

### Client-to-Server Commands

*   `auth.login`
*   `auth.logout`
*   `auth.refresh`
*   `auth.register`
*   `bank.balance`
*   `bank.deposit`
*   `bank.orders.cancel`
*   `bank.orders.create`
*   `bank.orders.list`
*   `bank.statement`
*   `bank.transfer`
*   `bank.withdraw`
*   `bounty.list`
*   `bounty.post`
*   `combat.attack`
*   `combat.deploy_fighters`
*   `combat.lay_mines`
*   `corp.balance`
*   `corp.deposit`
*   `corp.issue_dividend`
*   `corp.set_tax`
*   `corp.statement`
*   `corp.stock.issue`
*   `corp.withdraw`
*   `fine.list`
*   `fine.pay`
*   `insurance.claim.file`
*   `insurance.policies.buy`
*   `insurance.policies.list`
*   `loan.accept`
*   `loan.apply`
*   `loan.list_active`
*   `loan.offers.list`
*   `loan.repay`
*   `market.contracts.buy`
*   `market.contracts.list`
*   `market.contracts.sell`
*   `market.orders.cancel`
*   `market.orders.create`
*   `market.orders.list`
*   `move.autopilot.start`
*   `move.autopilot.status`
*   `move.autopilot.stop`
*   `move.describe_sector`
*   `move.pathfind`
*   `move.transwarp`
*   `move.warp`
*   `nav.avoid.add`
*   `nav.avoid.list`
*   `nav.avoid.remove`
*   `nav.bookmark.add`
*   `nav.bookmark.list`
*   `nav.bookmark.remove`
*   `planet.create`
*   `planet.deposit`
*   `planet.list_mine`
*   `planet.withdraw`
*   `player.get_settings`
*   `player.list_online`
*   `player.my_info`
*   `player.rankings`
*   `player.set_prefs`
*   `research.projects.fund`
*   `research.projects.list`
*   `ship.info`
*   `ship.jettison`
*   `ship.status`
*   `ship.upgrade`
*   `stock.exchange.list_stocks`
*   `stock.exchange.orders.cancel`
*   `stock.exchange.orders.create`
*   `stock.portfolio.list`
*   `system.capabilities`
*   `system.describe_schema`
*   `system.hello`
*   `trade.buy`
*   `trade.history`
*   `trade.port_info`
*   `trade.quote`
*   `trade.sell`

### Server-to-Client Events

*   `bank.interest.paid`
*   `bank.transaction.completed`
*   `bounty.claimed`
*   `chat.message`
*   `combat.attacked`
*   `combat.ship_destroyed`
*   `engine.tick`
*   `fine.issued`
*   `insurance.payout.received`
*   `loan.payment.due`
*   `mail.new`
*   `market.order.filled`
*   `sector.player_entered`
*   `sector.player_left`
*   `stock.order.filled`
*   `system.notice`

### Server-to-Engine (S2S) Protocol

#### Server → Engine Events (Facts)

*   `bounty.posted.v1`
*   `fedspace:asset_cleared`
*   `fedspace:tow`
*   `fighters.deployed`
*   `loan.accepted.v1`
*   `mines.deployed`
*   `npc.iss.move.v1`
*   `npc.iss.warp.v1`
*   `npc.move`
*   `npc.online`
*   `npc.trade`
*   `player.corp_join.v1`
*   `player.illegal_act.v1`
*   `player.trade.v1`
*   `ship.self_destruct.initiated`
*   `stock.order.placed.v1`
*   `trade.large_sale`

#### Engine → Server Commands (Mutations)

*   `bank.pay_interest.v1`
*   `corp.adjust_funds.v1`
*   `economy.update.v1`
*   `loan.process_repayments.v1`
*   `market.match_orders.v1`
*   `player.adjust_credits.v1`
*   `port.refresh_stock_and_price.v1`
*   `s2s.broadcast.sweep`
*   `s2s.command.push`
*   `s2s.engine.shutdown`
*   `s2s.error`
*   `s2s.event.relay`
*   `s2s.health.ack`
*   `s2s.health.check`
*   `s2s.planet.genesis`
*   `s2s.planet.transfer`
*   `s2s.player.migrate`
*   `s2s.port.restock`
*   `s2s.replication.heartbeat`
*   `server_broadcast.v1`

---

This v2.0 protocol provides a comprehensive framework for a rich, interactive, and player-driven galactic economy, while maintaining the robust, versioned, and introspective design of the original protocol.
