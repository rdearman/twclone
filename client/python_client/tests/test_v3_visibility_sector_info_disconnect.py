"""V3-VIS-01 regression for a lost current-sector information read."""

import client


class DisconnectingSectorInfo:
    def __init__(self, delegate):
        self.delegate = delegate
        self.calls = delegate.calls

    def rpc(self, command, data):
        self.calls.append({"command": command, "data": data})
        if command == "sector.info":
            raise ConnectionError("socket closed during sector information read")
        return self.delegate.rpc(command, data)


class DisconnectingSectorRefresh(DisconnectingSectorInfo):
    def rpc(self, command, data):
        self.calls.append({"command": command, "data": data})
        if command == "move.describe_sector":
            raise ConnectionError("socket closed during sector refresh")
        return self.delegate.rpc(command, data)


def test_sector_info_disconnect_preserves_confirmed_sector_without_traceback(
    ctx_factory, fake_conn, capsys
):
    ctx = ctx_factory(sector={"id": 12, "name": "Confirmed sector"})
    ctx.conn = DisconnectingSectorInfo(fake_conn)

    client.sector_info_flow(ctx)

    assert ctx.last_sector_desc == {"id": 12, "name": "Confirmed sector"}
    assert ctx.state["last_rpc"] is None
    assert [call["command"] for call in fake_conn.calls] == ["sector.info"]
    output = capsys.readouterr().out
    assert "outcome is uncertain after connection loss" in output
    assert "confirmed sector was not changed" in output


def test_authoritative_sector_refresh_disconnect_preserves_confirmed_sector(
    ctx_factory, fake_conn, capsys
):
    ctx = ctx_factory(sector={"id": 12, "name": "Confirmed sector"})
    ctx.conn = DisconnectingSectorRefresh(fake_conn)

    client.refresh_sector_and_display(ctx)

    assert ctx.last_sector_desc == {"id": 12, "name": "Confirmed sector"}
    assert [call["command"] for call in fake_conn.calls] == ["move.describe_sector"]
    output = capsys.readouterr().out
    assert "Sector refresh outcome is uncertain after connection loss" in output
    assert output.count("Sector refresh outcome is uncertain after connection loss") == 1
    assert "Sector refresh could not be confirmed after connection loss" not in output
    assert "confirmed sector was not changed" in output
