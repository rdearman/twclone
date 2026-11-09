from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class PoliceResolutionContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.police = (ROOT / "src/server_police.c").read_text(encoding="utf-8")
        cls.repository = (ROOT / "src/db/repo/repo_clusters.c").read_text(
            encoding="utf-8"
        )
        cls.envelope = (ROOT / "src/server_envelope.c").read_text(encoding="utf-8")

    def test_optional_incident_outputs_are_safe_for_crime_tracking(self):
        read = self.repository.split("repo_clusters_read_incident", 1)[1].split(
            "repo_clusters_get_player_suspicion_wanted", 1
        )[0]
        self.assertIn("if (suspicion_out)", read)
        self.assertIn("if (wanted_out)", read)
        self.assertIn("if (banned_out)", read)
        self.assertIn("repo_clusters_get_player_suspicion_wanted(db, cluster_info.cluster_id,", self.police)
        self.assertIn("player_id, NULL, NULL)", self.police)

    def test_resolution_reads_lock_incident_row_inside_transaction(self):
        self.assertIn("FOR UPDATE", self.repository)
        bribe = self.police.split("cmd_police_bribe", 1)[1].split(
            "cmd_police_surrender", 1
        )[0]
        surrender = self.police.split("cmd_police_surrender", 1)[1].split(
            "PHASE B: Incident Tracking", 1
        )[0]
        for handler in (bribe, surrender):
            self.assertLess(handler.index("db_tx_begin"), handler.index(
                "repo_clusters_get_player_incident_for_update"
            ))
            self.assertIn("db_tx_commit", handler)
            self.assertIn("db_tx_rollback", handler)

    def test_banned_only_state_counts_as_an_active_incident(self):
        for handler_name in ("cmd_police_bribe", "cmd_police_surrender"):
            handler = self.police.split(handler_name, 1)[1]
            self.assertIn("suspicion <= 0 && wanted_level <= 0 && banned == 0", handler)

    def test_bribe_persistence_errors_do_not_return_success(self):
        bribe = self.police.split("cmd_police_bribe", 1)[1].split(
            "cmd_police_surrender", 1
        )[0]
        self.assertIn("state_rc = repo_clusters_apply_bribe_success", bribe)
        self.assertIn("state_rc = repo_clusters_apply_bribe_failure", bribe)
        self.assertIn("if (state_rc != 0)", bribe)
        self.assertIn("Could not commit police bribe.", bribe)

    def test_rejected_bribe_preserves_phase_c_result_payload(self):
        bribe = self.police.split("cmd_police_bribe", 1)[1].split(
            "cmd_police_surrender", 1
        )[0]
        self.assertIn("send_response_error_payload", bribe)
        self.assertIn('"suspicion_after"', bribe)
        self.assertIn('"wanted_level_after"', bribe)
        helper = self.envelope.split("send_response_error_payload (", 1)[1].split(
            "send_response_refused (", 1
        )[0]
        self.assertIn('json_object_set (resp, "data", payload)', helper)
        self.assertIn('json_object_set_new (resp, "error", err)', helper)
        self.assertNotIn('json_object_set (err, "data", payload)', helper)

    def test_surrender_persistence_errors_do_not_return_success(self):
        surrender = self.police.split("cmd_police_surrender", 1)[1].split(
            "PHASE B: Incident Tracking", 1
        )[0]
        self.assertIn("state_rc = repo_clusters_clear_incident_state", surrender)
        self.assertIn("state_rc = repo_clusters_reduce_incident_by_tier", surrender)
        self.assertIn("if (state_rc != 0)", surrender)
        self.assertIn("Could not commit police surrender.", surrender)

    def test_police_status_does_not_hide_ban_state_read_errors(self):
        status = self.police.split("handle_police_status", 1)[1].split(
            "cmd_police_bribe", 1
        )[0]
        banned_read = status.split("/* Read banned status */", 1)[1].split(
            "/* Build response */", 1
        )[0]
        self.assertIn("repo_clusters_get_player_banned", status)
        self.assertIn("Database error reading ban status.", banned_read)
        self.assertNotIn("banned = 0;", banned_read)


if __name__ == "__main__":
    unittest.main()
