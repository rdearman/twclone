"""Source-contract checks for classic TW2002 sale equipment values.

These checks verify the catalogue, cross-backend migration and purchase-limit
wiring. The JSON ship-sale suite supplies the server-level settlement case.
"""

import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
EXPECTED = {
    "MINES": ("Armid Mine", 1000),
    "LIMPETS": ("Limpet Mine", 10000),
    "PROBE": ("Subspace Ether Probe", 3000),
    "PHOTONS": ("Photon Missile", 40000),
    "BEACONS": ("Marker Beacon", 100),
}


class ShipSaleEquipmentCatalogueTests(unittest.TestCase):
    def test_both_canonical_seed_files_include_reference_prices(self):
        for path in (ROOT / "sql/pg/091_seed_essential.sql",
                     ROOT / "sql/my/091_seed_essential.sql"):
            source = path.read_text(encoding="utf-8")
            for code, (name, price) in EXPECTED.items():
                with self.subTest(file=path.name, code=code):
                    self.assertIn(f"('{code}', '{name}', {price}", source)

    def test_runtime_migrations_preserve_existing_custom_catalogue_rows(self):
        pg = (ROOT / "sql/pg/178_ship_sale_equipment_catalogue.sql").read_text(
            encoding="utf-8"
        )
        mysql = (ROOT / "sql/my/130_ship_sale_equipment_catalogue.sql").read_text(
            encoding="utf-8"
        )
        self.assertIn("ON CONFLICT (code) DO NOTHING", pg)
        self.assertIn("INSERT IGNORE", mysql)
        for code, (_, price) in EXPECTED.items():
            with self.subTest(code=code):
                self.assertIn(f"'{code}'", pg)
                self.assertIn(str(price), pg)
                self.assertIn(f"'{code}'", mysql)
                self.assertIn(str(price), mysql)

    def test_migration_manifest_registers_postgresql_migration(self):
        manifest = json.loads(
            (ROOT / "sql/pg/migration_manifest.json").read_text(encoding="utf-8")
        )
        entries = [entry for entry in manifest["scripts"]
                   if entry["name"] == "178_ship_sale_equipment_catalogue.sql"]
        self.assertEqual(len(entries), 1)
        self.assertTrue(entries[0]["required"])

    def test_limpet_and_photon_purchases_use_ship_type_capacity(self):
        server = (ROOT / "src/server_stardock.c").read_text(encoding="utf-8")
        repo = (ROOT / "src/db/repo/repo_stardock.c").read_text(encoding="utf-8")
        for item, column, limit in (
            ("LIMPETS", "limpets", "maxlimpets"),
            ("PHOTONS", "photons", "maxphotons"),
        ):
            with self.subTest(item=item):
                self.assertIn(f'strcmp (code, "{item}") == 0', server)
                self.assertIn(f'col_name = "{column}"', server)
                self.assertIn(f'col_name, "{column}"', server)
                self.assertIn(f'strcpy (limit_col, "{limit}")', server)
                self.assertIn(f'{{"{column}", "{limit}", false}}', repo)

    def test_canonical_spec_distinguishes_ether_and_psychic_probes(self):
        spec = (ROOT / "docs/V3_EQUIPMENT_CATALOGUE.md").read_text(
            encoding="utf-8"
        )
        self.assertIn("Subspace Ether Probe", spec)
        self.assertIn("Psychic Probe", spec)
        self.assertIn("3.13 reference", spec)


if __name__ == "__main__":
    unittest.main()
