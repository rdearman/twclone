import pytest

import client


@pytest.mark.parametrize(
    ("frequency", "extra_answers", "expected_extra"),
    [
        ("daily", [], {}),
        ("weekly", ["1"], {"weekday": 1}),
        ("weekly", ["7"], {"weekday": 7}),
        ("monthly", ["31"], {"day_of_month": 31}),
    ],
)
def test_standing_order_flow_sends_contract_schedule_fields(
    frequency, extra_answers, expected_extra, ctx_factory, fake_conn,
    monkeypatch, capsys,
):
    answers = iter(["43", "250", frequency, "08:15", *extra_answers])
    monkeypatch.setattr("builtins.input", lambda *_: next(answers))
    payload = {
        "recipient_player_id": 43,
        "amount": 250,
        "frequency": frequency,
        "time_utc": "08:15",
        **expected_extra,
    }
    fake_conn.queue("bank.orders.create", {
        "status": "ok", "type": "bank.orders.created", "data": {
            "order_id": 91, **payload, "next_run_at": 1795076100,
        },
    })
    ctx = ctx_factory()

    client.bank_order_create_flow(ctx)

    assert fake_conn.calls == [{"command": "bank.orders.create", "data": payload}]
    assert ctx.state["last_rpc"]["type"] == "bank.orders.created"
    assert "Standing order 91 scheduled" in capsys.readouterr().out


@pytest.mark.parametrize(
    "answers",
    [
        ["0", "250", "daily", "08:15"],
        ["43", "0", "daily", "08:15"],
        ["43", "250", "yearly", "08:15"],
        ["43", "250", "weekly", "08:15", "8"],
        ["43", "250", "monthly", "08:15", "32"],
        ["43", "250", "daily", "24:15"],
    ],
)
def test_standing_order_flow_rejects_invalid_input_without_rpc(
    answers, ctx_factory, fake_conn, monkeypatch, capsys,
):
    pending = iter(answers)
    monkeypatch.setattr("builtins.input", lambda *_: next(pending))
    ctx = ctx_factory()

    client.bank_order_create_flow(ctx)

    assert fake_conn.calls == []
    assert capsys.readouterr().out


def test_standing_order_flow_does_not_claim_success_without_matching_receipt(
    ctx_factory, fake_conn, monkeypatch, capsys,
):
    answers = iter(["43", "250", "daily", "08:15"])
    monkeypatch.setattr("builtins.input", lambda *_: next(answers))
    fake_conn.queue("bank.orders.create", {
        "status": "ok", "type": "bank.orders.created", "data": {
            "order_id": 91, "recipient_player_id": 99, "amount": 250,
            "frequency": "daily", "time_utc": "08:15", "next_run_at": 1,
        },
    })
    ctx = ctx_factory()

    client.bank_order_create_flow(ctx)

    assert len(fake_conn.calls) == 1
    assert "was not confirmed" in capsys.readouterr().out
