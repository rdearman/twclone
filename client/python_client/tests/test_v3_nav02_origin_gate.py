"""NAV-02 client boundary: transwarp requires a confirmed origin sector."""

import client


def test_transwarp_does_not_send_without_authoritative_current_sector(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory(sector={"name": "Unidentified cached sector"})
    monkeypatch.setattr("builtins.input", lambda *_: "44")

    assert client.transwarp_flow(ctx) is False
    assert fake_conn.calls == []
    assert "refresh the confirmed current sector" in capsys.readouterr().out


def test_transwarp_rejects_non_integer_cached_origin_without_coercion(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory(sector={"id": "17", "name": "String-ID sector"})
    monkeypatch.setattr("builtins.input", lambda *_: "44")

    assert client.transwarp_flow(ctx) is False
    assert fake_conn.calls == []
    assert "refresh the confirmed current sector" in capsys.readouterr().out
