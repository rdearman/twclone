"""V3-VIS-01 transport-failure acceptance checks for visibility workflows."""

import client


class DisconnectedConn:
    def __init__(self):
        self.calls = []

    def rpc(self, command, data):
        self.calls.append({"command": command, "data": data})
        raise ConnectionError("socket closed")


def test_scan_disconnect_preserves_confirmed_sector_and_does_not_guess_refresh(
    ctx_factory, capsys
):
    """A lost read reply is uncertain, so no payload or speculative read is trusted."""
    conn = DisconnectedConn()
    ctx = ctx_factory(sector={"id": 12, "name": "Confirmed"})
    ctx.conn = conn

    client.detailed_sector_scan_flow(ctx)

    assert ctx.last_sector_desc == {"id": 12, "name": "Confirmed"}
    assert conn.calls == [{"command": "sector.scan", "data": {}}]
    output = capsys.readouterr().out
    assert "outcome is uncertain after connection loss" in output
    assert "Reconnect before relying on scan information" in output


def test_density_disconnect_preserves_confirmed_sector_and_does_not_guess_refresh(
    ctx_factory, capsys
):
    """Density scans use the same reconnect-safe contract as detailed scans."""
    conn = DisconnectedConn()
    ctx = ctx_factory(sector={"id": 12, "name": "Confirmed"})
    ctx.conn = conn

    client.sector_density_scan_flow(ctx)

    assert ctx.last_sector_desc == {"id": 12, "name": "Confirmed"}
    assert conn.calls == [{"command": "sector.scan.density", "data": {"sector_id": 12}}]
    output = capsys.readouterr().out
    assert "outcome is uncertain after connection loss" in output
    assert "Reconnect before relying on density information" in output
