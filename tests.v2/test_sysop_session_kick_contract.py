from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def test_sysop_kick_revokes_durable_sessions_before_closing_connections():
    handler = (ROOT / "src/server_sysop.c").read_text(encoding="utf-8")
    repository = (ROOT / "src/db/repo/repo_session.c").read_text(encoding="utf-8")
    dispatch = (ROOT / "src/server_loop.c").read_text(encoding="utf-8")

    kick = handler.split("int cmd_sysop_player_kick", 1)[1].split(
        "int cmd_sysop_player_sessions_get", 1
    )[0]
    revoke_call = kick.index("repo_session_revoke_player_sessions(db, target_id)")
    socket_kick = kick.index("server_sysop_kick_player(target_id)")

    assert revoke_call < socket_kick
    assert "DELETE FROM sessions WHERE player_id = {1}" in repository
    assert '"player.my_info", cmd_player_my_info' in dispatch

