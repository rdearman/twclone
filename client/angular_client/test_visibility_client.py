import contextlib
import io
import importlib.util
import sys
import unittest
from pathlib import Path

CLIENT_PATH = Path(__file__).resolve().parent / "client.py"
spec = importlib.util.spec_from_file_location("twclone_python_client", CLIENT_PATH)
client = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = client
spec.loader.exec_module(client)


class FakeConn:
    def __init__(self, response):
        self.response = response

    def rpc(self, command, payload):
        self.command = command
        self.payload = payload
        return self.response


class VisibilityClientTests(unittest.TestCase):
    def context(self, response, sector_id=12):
        return client.Context(
            conn=FakeConn(response),
            menus={},
            last_sector_desc={
                "id": sector_id,
                "name": "Confirmed sector",
                "port": {"id": 8, "class": 5},
            },
        )

    def test_refusal_does_not_render_embedded_visibility_payload(self):
        rendered = client._render_scan_card({
            "status": "refused",
            "error": {"code": "ERR_FORBIDDEN", "message": "Sector is not visible."},
            "data": {"sector_id": 999, "name": "Hidden sector", "counts": {"ships": 77}},
        })
        self.assertIn("REFUSED", rendered)
        self.assertIn("Sector is not visible.", rendered)
        self.assertNotIn("Hidden sector", rendered)
        self.assertNotIn("77", rendered)

    def test_malformed_visibility_fields_fail_closed(self):
        rendered = client._render_scan_card({
            "status": "ok",
            "type": "move.scan",
            "data": {
                "sector_id": 12,
                "name": "Known sector",
                "security": {"fedspace": "false"},
                "port": {"present": "false"},
                "counts": {"ships": 4.0},
            },
        })
        self.assertIn("fedspace=not reported", rendered)
        self.assertIn("Port: not reported", rendered)
        self.assertIn("ships=not reported", rendered)
        self.assertNotIn("Port: present", rendered)

    def test_missing_id_is_not_rendered_as_bound_scan(self):
        rendered = client._render_scan_card({
            "status": "ok",
            "type": "move.scan",
            "data": {"name": "Unbound hidden sector", "counts": {"ships": 9}},
        })
        self.assertIn("no authoritative sector ID", rendered)
        self.assertNotIn("Unbound hidden sector", rendered)
        self.assertNotIn("9", rendered)

    def test_scan_cache_accepts_only_confirmed_current_sector(self):
        response = {
            "status": "ok",
            "type": "move.scan",
            "data": {"sector_id": 99, "name": "Remote sector", "port": {"present": False}},
        }
        ctx = self.context(response)
        with contextlib.redirect_stdout(io.StringIO()):
            client.scan_sector(ctx)
        self.assertEqual(ctx.last_sector_desc["id"], 12)
        self.assertEqual(ctx.last_sector_desc["name"], "Confirmed sector")
        self.assertEqual(ctx.last_sector_desc["port"]["class"], 5)

    def test_scan_cache_does_not_merge_refusal_and_accepts_typed_current_result(self):
        refused = {
            "status": "refused",
            "type": "move.scan",
            "error": {"message": "not visible"},
            "data": {"sector_id": 12, "name": "Untrusted refusal"},
        }
        ctx = self.context(refused)
        with contextlib.redirect_stdout(io.StringIO()):
            client.scan_sector(ctx)
        self.assertEqual(ctx.last_sector_desc["name"], "Confirmed sector")

        ctx.conn.response = {
            "status": "ok",
            "type": "move.scan",
            "data": {
                "sector_id": 12,
                "name": "Current sector",
                "adjacent": [13, 14],
                "port": {"present": False},
            },
        }
        with contextlib.redirect_stdout(io.StringIO()):
            client.scan_sector(ctx)
        self.assertEqual(ctx.last_sector_desc["name"], "Current sector")
        self.assertEqual(ctx.last_sector_desc["adjacent"], [13, 14])
        self.assertFalse(ctx.last_sector_desc["port"]["present"])

    def test_density_scan_uses_only_authoritative_command_and_response_type(self):
        response = {
            "status": "ok",
            "type": "sector.density.scan",
            "data": {"sectors": [12, 4, 13, 0]},
        }
        ctx = self.context(response)
        with contextlib.redirect_stdout(io.StringIO()) as output:
            client.sector_density_scan_flow(ctx)
        self.assertEqual(ctx.conn.command, "sector.scan.density")
        self.assertEqual(ctx.conn.payload, {"sector_id": 12})
        self.assertIn("12", output.getvalue())
        self.assertIn("13", output.getvalue())

    def test_density_scan_rejects_unpublished_type_without_fallback(self):
        response = {
            "status": "ok",
            "type": "sector.scan.density",
            "data": {"sectors": [12, 99]},
        }
        ctx = self.context(response)
        with contextlib.redirect_stdout(io.StringIO()) as output:
            client.sector_density_scan_flow(ctx)
        self.assertIn("unexpected type", output.getvalue())
        self.assertEqual(ctx.conn.command, "sector.scan.density")

    def test_density_scan_rejects_malformed_rows_without_displaying_them(self):
        response = {
            "status": "ok",
            "type": "sector.density.scan",
            "data": {"sectors": [12, 4, "13", 77]},
        }
        ctx = self.context(response)
        with contextlib.redirect_stdout(io.StringIO()) as output:
            client.sector_density_scan_flow(ctx)
        self.assertIn("malformed", output.getvalue())
        self.assertNotIn("77", output.getvalue())


if __name__ == "__main__":
    unittest.main()
