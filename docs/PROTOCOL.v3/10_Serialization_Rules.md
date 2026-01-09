# 10. Serialization Rules

## 1. Data Types

*   **Integers**: Used for IDs, quantities, and discrete values.
*   **Strings**: UTF-8.
*   **Booleans**: `true` / `false`.
*   **Decimals/Currency**: String-encoded decimals to avoid floating-point errors (e.g., `"12345.67"`).
    *   *Convention*: **MUST** use String-encoded decimals for all currency fields in Client RPC responses (e.g., `total_cost`, `credits_remaining`) to ensure precision and consistent parsing.

## 2. Units

*   **Distance**: Sectors (Integer hops) or Coordinates (x, y, z).
*   **Time**: ISO-8601 UTC Strings for timestamps (`ts`). Seconds (Integer) for durations/TTLs.
*   **Percentages**: 0-100 (Integer) or 0.0-1.0 (Float) as specified per field.
*   **Hit Points (HP)**: Integer.

## 3. Localization Keys (i18n)

Events should return **keys** rather than hardcoded English text.

### 3.1 Format
Dot-separated identifiers: `domain.action.element`

**Examples**:
*   `engine.tick.title`
*   `combat.hit.body`
*   `nav.sector.enter.title`

### 3.2 Templating
Values in the `data` payload are used to interpolate localized strings.
*   Template: `{attacker_id} hit {defender_id} with {weapon}.`
*   Payload: `{ "attacker_id": "P1", "defender_id": "P2", "weapon": "Laser" }`

### 3.3 Catalogs
Clients download catalogs (maps of Key -> Template) via `client.hello` negotiation.

## 4. Common Enums

*   **`weapon_code`**: `["laser_mk1", "plasma_cannon", ...]`
*   **`notice_severity`**: `["info", "warn", "error"]`
*   **`commodity`**: `["ORE", "ORG", "EQU", "COL"]`
