# Shared TWClone assets

This is the canonical client-neutral artwork library. `catalog.json` is the
machine-readable index; IDs are stable references and paths are relative to
the repository root. Clients should resolve an explicit `asset_id` first and
use the catalogue's object mappings as a fallback.

Artwork is stored once here. Godot reaches this directory through a project
symlink so there is no Godot-owned artwork copy. The original PNG atlases are
retained as source material; clients should serve and load the WebP variants
or scalable Stardock SVG listed in the catalogue.

See [`docs/UNIVERSAL_GAME_ASSETS.md`](../docs/UNIVERSAL_GAME_ASSETS.md) for
ID conventions, sizing, client integration, caching, and the existing ship
type mappings.

Validate the catalogue and source references from the repository root with:

```bash
python3 assets/tools/validate_catalog.py
```
