# 04. Error Handling

## 1. Error Envelope

When a request fails (`status` != "ok"), the `error` field is populated:

```json
{
  "id": "srv-err-...",
  "reply_to": "req-uuid",
  "status": "error",
  "type": "error",
  "error": {
    "code": 1401,
    "message": "Not authenticated",
    "data": { /* optional details */ }
  }
}
```

## 2. Standard Error Codes

| Code | Meaning                 | Description |
| ---- | ----------------------- | ----------- |
| 1101 | Not implemented         | The command is known but not yet available. |
| 1105 | Validation failed       | Payload schema validation failed. |
| 1209 | Not Found               | Resource (sector, player, etc.) not found. |
| 1210 | Username already exists | Registration failed due to duplicate name. |
| 1301 | Missing Field           | A required field was missing. |
| 1302 | Invalid Argument        | A field had an invalid value or type. |
| 1304 | Limit Exceeded          | A quota (bookmarks, avoids) has been reached. |
| 1401 | Not authenticated       | Session required but missing or invalid. |
| 1402 | Bad arguments           | General logic error with arguments. |
| 1403 | Invalid topic           | Subscription topic does not exist. |
| 1405 | Topic is locked         | Cannot unsubscribe from this topic. |
| 1406 | Path not found          | Navigation/Autopilot could not find a route. |
| 1407 | Refused / Forbidden     | Request refused (permissions, rate limit, policy). |
| 1503 | Database error          | Internal storage failure. |

## 3. S2S Error Handling

For Server-to-Server (S2S) communication:
*   **Success**: `s2s.ack`
*   **Error**: `s2s.error` with payload `{ "code": "string", "message": "string", "details": {...} }`

**Common S2S Error Codes:**
*   `bad_envelope`: Missing required fields.
*   `unsupported_type`: Unknown message type.
*   `auth_required` / `auth_bad`: HMAC failure.
*   `toolarge`: Frame exceeds size limit.
*   `internal_error`: Unexpected processing error.
