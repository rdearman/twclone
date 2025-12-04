# 10. Serialization Rules

## 1. Data Types

*   **Integers**: Used for IDs, quantities, and discrete values.
*   **Strings**: UTF-8.
*   **Booleans**: `true` / `false`.
*   **Decimals/Currency**: String-encoded decimals to avoid floating-point errors (e.g., `"12345.67"`).
    *   *Alternative*: Minor units (integers) where specified (e.g., banking internal storage), but API often prefers string-decimal for display. **Protocol V2 spec uses String-encoded decimals (`"1000.00"`) or Minor Units (Integers) depending on the context.**
    *   *Convention*: Use String-encoded decimals for currency in Client RPCs unless "minor units" is explicitly stated.

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
*   **`commodity`**: `["ore", "organics", "equipment", "fuel"]`
