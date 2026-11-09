"""V3 movement client acceptance for an uncertain transwarp reply."""

import client


def test_transwarp_disconnect_preserves_last_confirmed_state(
    ctx_factory, fake_conn, capsys, monkeypatch
):
    """A lost transwarp reply must not invent relocation or hazard effects."""
    ctx = ctx_factory(sector={"id": 17, "name": "Before"})
    fake_conn.queue("move.describe_sector", {
        "status": "ok", "type": "sector.info", "data": {"sector_id": 44, "name": "After"},
    })
    fake_conn.queue("player.my_info", {
        "status": "ok", "type": "player.info", "data": {"player": {"id": 3, "sector": 44}},
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "type": "ship.status", "data": {"ship": {"id": 9}},
    })
    monkeypatch.setattr("builtins.input", lambda *_: "44")

    class DisconnectingConn:
        def __init__(self, delegate):
            self.delegate = delegate
            self.calls = delegate.calls

        def rpc(self, command, data):
            if command == "move.transwarp":
                self.calls.append({"command": command, "data": data})
                raise ConnectionError("socket closed")
            return self.delegate.rpc(command, data)

    ctx.conn = DisconnectingConn(fake_conn)
    client.transwarp_flow(ctx)

    output = capsys.readouterr().out
    assert "Transwarp outcome is uncertain after connection loss" in output
    assert "Reconnect before attempting another move" in output
    assert ctx.last_sector_desc == {"id": 44, "name": "After"}
    assert ctx.state.get("last_rpc") is None
    assert [call["command"] for call in fake_conn.calls] == [
        "move.transwarp", "move.describe_sector", "player.my_info", "ship.status",
    ]
