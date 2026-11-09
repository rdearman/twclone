"""V3-VIS-01: a lost visibility reply must not alter confirmed state."""

import client


def test_scan_disconnect_preserves_confirmed_sector_and_does_not_retry(
    ctx_factory, fake_conn, capsys
):
    class DisconnectingConn:
        def __init__(self, delegate):
            self.delegate = delegate
            self.calls = delegate.calls

        def rpc(self, command, data):
            self.calls.append({"command": command, "data": data})
            if command == "move.scan":
                raise ConnectionError("socket closed")
            return self.delegate.rpc(command, data)

    confirmed = {"id": 12, "name": "Known"}
    ctx = ctx_factory(sector=confirmed)
    ctx.conn = DisconnectingConn(fake_conn)

    client.scan_sector(ctx)

    assert ctx.last_sector_desc == confirmed
    assert ctx.state["last_rpc"] is None
    assert ctx.conn.calls == [{"command": "move.scan", "data": {}}]
    output = capsys.readouterr().out
    assert "outcome is uncertain after connection loss" in output
    assert "confirmed sector was not changed" in output
