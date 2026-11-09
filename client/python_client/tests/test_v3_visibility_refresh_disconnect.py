"""V3-VIS-01 refusal recovery when the authoritative refresh disconnects."""

import client


class RefusalThenDisconnect:
    def __init__(self, calls):
        self.calls = calls

    def rpc(self, command, data):
        self.calls.append({"command": command, "data": data})
        if command == "move.scan":
            return {
                "status": "refused",
                "error": {"message": "Scan requires the current sector."},
            }
        raise ConnectionError("socket closed during visibility refresh")


def test_scan_refusal_with_refresh_disconnect_keeps_confirmed_sector(
    ctx_factory, capsys
):
    calls = []
    ctx = ctx_factory(sector={"id": 12, "name": "Confirmed"})
    ctx.conn = RefusalThenDisconnect(calls)

    client.scan_sector(ctx)

    assert ctx.last_sector_desc == {"id": 12, "name": "Confirmed"}
    assert calls == [
        {"command": "move.scan", "data": {}},
        {"command": "move.describe_sector", "data": {"sector_id": 12}},
    ]
    output = capsys.readouterr().out
    assert "Scan requires the current sector." in output
    assert "Sector refresh outcome is uncertain after connection loss" in output
    assert output.count("Sector refresh outcome is uncertain after connection loss") == 1
    assert "Sector refresh could not be confirmed after connection loss" not in output
    assert "confirmed sector was not changed" in output
