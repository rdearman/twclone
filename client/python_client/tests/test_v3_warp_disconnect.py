"""V3-HAZ-01 client acceptance for a lost warp reply."""

import client


def test_warp_disconnect_preserves_confirmed_state_and_does_not_retry_hazards(
    ctx_factory, fake_conn, capsys, monkeypatch
):
    class DisconnectingConn:
        def __init__(self, delegate):
            self.delegate = delegate
            self.calls = delegate.calls

        def rpc(self, command, data):
            self.calls.append({"command": command, "data": data})
            if command == "move.warp":
                raise ConnectionError("socket closed")
            return self.delegate.rpc(command, data)

    ctx = ctx_factory(sector={"id": 17, "name": "Confirmed"})
    ctx.conn = DisconnectingConn(fake_conn)
    monkeypatch.setattr("builtins.input", lambda *_: "44")

    result = client.warp_flow(ctx)

    output = capsys.readouterr().out
    assert result is None
    assert "Warp outcome is uncertain after connection loss" in output
    assert "Reconnect before attempting another move" in output
    assert ctx.last_sector_desc == {"id": 17, "name": "Confirmed"}
    assert ctx.state["last_rpc"] is None
    assert [call["command"] for call in fake_conn.calls] == ["move.warp"]
