# V3 Canonical Equipment Catalogue

This catalogue supplies the canonical base prices used by equipment purchase
and SHIP-002 installed-equipment valuation. SysOp/game editions may customize
equipment prices; the figures below are the project's standard classic TWGS
3.13 reference baseline, not a claim that every TradeWars 2002 server used the
same values. Existing catalogue rows are not overwritten by migrations.

## Standard reference values

| Catalogue code | TradeWars item | Unit price (credits) | Purchase location | Quantity rule |
|---|---|---:|---|---|
| `MINES` | Armid mine | 1,000 | StarDock | Ship type `maxmines` |
| `LIMPETS` | Limpet mine | 10,000 | StarDock | Ship type `maxlimpets` |
| `PROBE` | Subspace Ether Probe | 3,000 | StarDock | Ship type `max_probes` |
| `PHOTONS` | Photon missile | 40,000 | StarDock | Ship type `maxphotons` |
| `BEACONS` | Marker beacon | 100 | StarDock | Ship type `maxbeacons` |

`PROBE` means the consumable Subspace Ether Probe carried in the ship's
`probes` quantity. It does not mean the Psychic Probe: the original game has
two distinct probe mechanics. The Psychic Probe is a trade-information
capability and has no corresponding installed/count field in the current
`ships` model, so it is not added to sold-hull installed-equipment valuation.

Purchase quantities remain constrained by the current ship type's configured
capacity. The catalogue does not grant ships capacity they do not have. In
particular, the existing `maxlimpets` values continue to govern whether a
ship can carry purchased limpets; the purchase mapping now enforces that limit
instead of rejecting the item as an unknown catalogue mapping. Photon capacity
is similarly enforced through `maxphotons`.

## Sale valuation

The sale repository multiplies each installed quantity by its catalogue unit
price, adds that value to the hull base value, applies the configured sale
factor in basis points and rounds down by integer division:

```text
floor((hull_base + sum(installed_quantity * unit_price)) * sale_factor_bp / 10000)
```

Cargo is excluded. Consumables present on the hull are retained with the used
hull; this catalogue supplies their valuation and does not make them
independently refundable.

## Historical evidence and limits

An archived TWGS 3.13 configuration-exporter thread records these values
together in its "File Format Tester" game: Armid 1,000, Limpet 10,000, Beacon
100, Ether Probe 3,000, and Photon 40,000 credits. The exporter author notes
that most costs are omitted when they match the configured default. This is
historical reference configuration data, not a price table printed in the
official manual and not proof that every TW2002/TWGS edition used these
figures. The project adopts this classic profile as the requested baseline;
known edited tournament/game configurations have different values. The
official TradeWars manual confirms the distinct Ether Probe, mine, and Photon
mechanics, but does not provide this numeric price table.

- [TWGS configuration exporter archive](https://www.classictw.com/viewtopic.php?f=1&start=0&t=10108)
- [TWGS 3.13 File Format Tester export showing the adopted reference values](https://www.classictw.com/viewtopic.php?f=1&start=0&t=10108)
- [Example of an explicitly edited 2010 tournament configuration](https://www.classictw.com/viewtopic.php?p=198878)
- [TradeWars 2002 manual (1990–2000)](https://electraphysics.com/documents/tradewarsmanual.pdf)
- [TWGS settings reference illustrating server/configuration variation](https://www.thestardock.com/files/ModernManual/advanced/twgs-settings.md)

The selected values intentionally follow the classic 3.13 reference export.
They are canonical V3 catalogue defaults; deployments may retain their own
existing configured catalogue values. See [`V3_GAMEPLAY_DECISIONS.md`](V3_GAMEPLAY_DECISIONS.md)
and [`19_Ship_Commands.md`](PROTOCOL.v3/19_Ship_Commands.md).
