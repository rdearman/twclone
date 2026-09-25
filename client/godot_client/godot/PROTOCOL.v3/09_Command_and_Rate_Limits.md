# 09. Command and Rate Limits

## 1. Rate Limiting

Limits are applied per-session or per-IP.

### 1.1 Metadata Headers
The server returns rate limit status in the `meta` field of response envelopes:
```json
"meta": {
  "rate_limit": {
    "limit": 200,   // Max requests per window
    "remaining": 152, // Requests left
    "reset": "2025-09-17T20:00:00Z" // Window reset time
  }
}
```

### 1.2 Error Code
If a limit is exceeded, the server returns error `1407 Rate limited`.

## 2. Hard Limits

*   **Max Frame Size**: 64 KiB (default).
*   **Request Frequency**: Default 200 requests per minute (configurable).
*   **S2S Limits**: Configured in `s2s_keys` and `config` table (e.g., `s2s.frame_size_limit`).

## 3. Command-Specific Limits

*   **Bookmarks**: Max 64 per player.
*   **Avoid List**: Max 64 per player.
*   **Bulk Operations**: Max items per request (e.g., `100` for batch operations) where applicable.
*   **Pagination**: Default page size 20, max 50 (or 100 for specific queries like leaderboards).
