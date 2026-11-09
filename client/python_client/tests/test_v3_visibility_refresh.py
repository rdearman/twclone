"""V3-VIS-01 regression tests for authoritative visibility refreshes."""

import client


class DisconnectingVisibilityConn:
    def __init__(self, delegate):
        self.delegate = delegate
        self.calls = delegate.calls

    def rpc(self, command, data):
        self.calls.append({"command": command, "data": data})
        if command == "sector.scan":
            raise ConnectionError("socket closed")
        return self.delegate.rpc(command, data)


class DisconnectingDensityConn(DisconnectingVisibilityConn):
    def rpc(self, command, data):
        self.calls.append({"command": command, "data": data})
        if command == "sector.scan.density":
            raise ConnectionError("socket closed during density scan")
        return self.delegate.rpc(command, data)


def test_empty_density_response_is_a_valid_no_row_result(
    ctx_factory, fake_conn, capsys
):
    ctx = ctx_factory(sector={"id": 12, "name": "Known"})
    fake_conn.queue("sector.scan.density", {
        "status": "ok", "data": [],
    })

    client.sector_density_scan_flow(ctx)

    assert [call["command"] for call in fake_conn.calls] == ["sector.scan.density"]
    assert ctx.last_sector_desc["name"] == "Known"
    output = capsys.readouterr().out
    assert "No density values were returned by the server." in output
    assert "unexpected response shape" not in output


def test_detailed_scan_disconnect_preserves_confirmed_sector_without_traceback(
    ctx_factory, fake_conn, capsys
):
    ctx = ctx_factory(sector={"id": 12, "name": "Known"})
    ctx.conn = DisconnectingVisibilityConn(fake_conn)

    client.detailed_sector_scan_flow(ctx)

    assert ctx.last_sector_desc == {"id": 12, "name": "Known"}
    assert ctx.state["last_rpc"] is None
    assert ctx.conn.calls == [{"command": "sector.scan", "data": {}}]
    output = capsys.readouterr().out
    assert "outcome is uncertain after connection loss" in output
    assert "Reconnect before relying on scan information" in output


def test_density_scan_disconnect_preserves_confirmed_sector_without_traceback(
    ctx_factory, fake_conn, capsys
):
    ctx = ctx_factory(sector={"id": 12, "name": "Known"})
    ctx.conn = DisconnectingDensityConn(fake_conn)

    client.sector_density_scan_flow(ctx)

    assert ctx.last_sector_desc == {"id": 12, "name": "Known"}
    assert ctx.state["last_rpc"] is None
    assert ctx.conn.calls == [{"command": "sector.scan.density", "data": {"sector_id": 12}}]
    output = capsys.readouterr().out
    assert "Density scan outcome is uncertain after connection loss" in output
    assert "Reconnect before relying on density information" in output


def test_malformed_authoritative_refresh_preserves_confirmed_sector(
    ctx_factory, fake_conn, capsys
):
    """A success envelope with invalid sector data must fail closed."""
    ctx = ctx_factory(sector={"id": 12, "name": "Known"})
    fake_conn.queue("move.describe_sector", {
        "status": "ok", "type": "sector.info", "data": ["not", "a", "sector"]
    })

    assert client.refresh_sector_and_display(ctx) is False
    assert ctx.last_sector_desc == {"id": 12, "name": "Known"}
    assert fake_conn.calls == [{
        "command": "move.describe_sector", "data": {"sector_id": 12}
    }]
    assert "returned malformed data" in capsys.readouterr().out
