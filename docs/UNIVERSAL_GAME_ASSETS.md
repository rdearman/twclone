# Universal game asset library

## Ownership and layout

`assets/` at the repository root is the canonical, client-neutral library.
Godot, the HTTPS/Web client, and mobile clients consume the same files. None
of the clients owns artwork or keeps a private duplicate.

```text
assets/
  catalog.json
  ships/
  locations/
  commodities/
  hardware/
  UI/
```

`assets/catalog.json` is the source of truth for stable asset IDs, display
names, categories, variant paths, media types, and game-object mappings. The
initial library reuses the current planet/sector art, adds optimized WebP
variants, and adds a unique scalable Stardock illustration. Original PNG
atlases remain in `locations/` as source material; they are not client
variants and are not entries in the catalogue.

## ID rules and game response references

IDs are lowercase, namespaced, and independent of filenames where possible:

| Object | Example asset ID | Catalogue mapping |
| --- | --- | --- |
| Stardock | `location.stardock` | Port type ID `9` |
| Other port | `location.port.station` | Port mapping default |
| Planet class K | `planet.class.k` | Class code `K` or seeded type ID |
| Planet class M | `planet.class.m` | Class code `M` or seeded type ID |
| Hauler artwork | `ship.generic-hauler` | Existing cargo and transport ship type names |
| Fighter artwork | `ship.generic-fighter` | Existing combat ship type names and fallback |

The catalogue maps every ship type currently seeded by the game, including
the Escape Pod, to one of the two existing ship illustrations. These are
artwork archetypes, not new gameplay ship types. The mapping can change by
updating the catalogue without changing client code.

Future server responses may include an optional field such as
`"asset_id":"location.stardock"`. A client should resolve that ID from the
catalogue, then use `object_mappings` when the field is absent. IDs must be
treated as opaque catalogue keys; do not infer filesystem paths from server
input. Adding this optional field to a server response is a separate protocol
change and is not part of this asset-only work.

When adding artwork, add a new stable ID rather than repurposing an ID for a
different object. Keep display text and type-name aliases in the catalogue so
renames or alternate server labels do not require client code edits.

## Size variants and formats

Raster art uses WebP, which is supported by current desktop browsers, Godot,
and mobile image stacks. Transparent sprites retain alpha. Variants are:

- `thumbnail`: compact rows, selectors, and constrained bandwidth.
- `standard`: normal client display.
- `large`: high-density or large-view display.

The exact variant paths and the current pixel guidance are in `catalog.json`.
The Stardock illustration is SVG and uses one scalable source for each size
entry. Clients should select a suitable variant for display density and avoid
downloading the large version for a small icon.

## Client integration

### Godot desktop

The Godot project has `client/godot_client/godot/assets` as a symlink to the
repository-root `assets/`. `AssetCatalog.gd` reads `catalog.json` and resolves
object IDs to Godot resources. Sector backgrounds, ports, ships, and planet
classes use this adapter rather than atlas paths embedded in scene code. Godot
keeps importer settings next to shared source assets and creates its normal
local `.godot/imported/` cache. Those settings and cache are import metadata,
not a second checked-in artwork library. The resource loader uses this
metadata for desktop exports. Since the catalogue is read as JSON at runtime,
desktop export presets must include `assets/catalog.json` in the exported
package as a non-resource file.

### HTTPS and Angular Web

Serve the repository `assets/` directory as static content under the same
origin at `/assets/`, so `assets/catalog.json` is `/assets/catalog.json` and a
catalogue path such as `assets/ships/ship-hauler-large.webp` resolves directly
from the site origin. Do not prepend `/assets/` to the full catalogue path a
second time. This is static delivery, not a gameplay API. Set correct
`Content-Type` and `X-Content-Type-Options: nosniff`. Treat published asset
paths as immutable: publish revisions under new filenames and update the
catalogue. Those files can then use long-lived immutable caching; cache or
revalidate the catalogue more frequently so mapping updates become visible.
Angular should fetch the catalogue once and resolve IDs to listed paths
rather than duplicating mappings in TypeScript.

### Mobile

Mobile clients should fetch the catalogue at login/startup, retain it by
`catalog_version`, and cache requested variant files by their immutable path
in the platform's normal disk cache. Use thumbnails on small lists or low-bandwidth
connections, and download large variants only when a view needs them. Keep the
last-known catalogue and required files available for offline rendering.

## Current scope and gaps

The catalogue currently has location, planet-class, and ship illustrations.
`commodities/`, `hardware/`, and `UI/` are reserved canonical categories and
are empty apart from notes; no new item artwork is fabricated here. The
current sector ship response exposes an ID and name but not its ship type, so
Godot uses the catalogue fallback there. Once the server includes a known
type name or optional `asset_id`, existing catalogue mappings can select the
specific hauler/fighter artwork without a client code change.

## Validation and Godot debug inspection

Run `python3 assets/tools/validate_catalog.py` from the repository root. It
checks ID syntax and uniqueness, mapping references, every listed image path,
source-code asset ID literals, and catalogue/image files that are unused or
unlisted. It exits nonzero on errors and reports unused items as warnings.

In a Godot debug build, select an object in the sector view and press **F3**
to show its resolved asset ID below the object label. `UNMAPPED` means the
current object data could not be resolved by an explicit `asset_id` or the
catalogue mapping. Press F3 again to hide the diagnostic.
