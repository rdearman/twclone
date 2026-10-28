"""Guard repository boundaries so plaintext/hash values do not leak to clients."""

from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


class PasswordAuthWiringTest(unittest.TestCase):
    def test_both_registration_repositories_hash_before_database_write(self):
        for path, function_name in (
            (ROOT / "src/db/repo/repo_auth.c", "repo_auth_register_player"),
            (ROOT / "src/db/repo/repo_cmds.c", "repo_cmds_register_player"),
        ):
            source = path.read_text()
            function = source.split(function_name, 1)[1]
            self.assertIn("tw_password_hash", function)
            self.assertIn("db_bind_text(password_hash)", function)

    def test_password_change_hashes_and_authentication_verifies_encoded_value(self):
        source = (ROOT / "src/db/repo/repo_auth.c").read_text()
        update = source.split("repo_auth_update_password", 1)[1]
        verify = source.split("repo_auth_verify_password", 1)[1].split(
            "repo_auth_update_password", 1
        )[0]
        self.assertIn("tw_password_hash", update)
        self.assertIn("db_bind_text(password_hash)", update)
        self.assertIn("tw_password_verify", verify)

    def test_legacy_login_query_does_not_return_hash(self):
        source = (ROOT / "src/db/repo/repo_cmds.c").read_text()
        login_query = source.split("repo_cmds_get_login_info", 1)[1].split(
            "repo_cmds_register_player", 1
        )[0]
        self.assertNotIn("SELECT player_id, passwd", login_query)
        server = (ROOT / "src/server_cmds.c").read_text()
        login_path = server.split("play_login (", 1)[1].split("user_create (", 1)[0]
        self.assertIn("repo_auth_verify_password", login_path)
        self.assertNotIn("ct_str_eq", login_path)


if __name__ == "__main__":
    unittest.main()
