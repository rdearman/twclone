import client


def test_policy_list_displays_only_authoritative_server_rows(ctx_factory, fake_conn, capsys):
    ctx = ctx_factory()
    fake_conn.queue("insurance.policies.list", {
        "status": "ok", "type": "insurance.policies.list.response",
        "data": {"policies": [{"policy_id": 7, "subject_type": "ship",
                              "subject_id": 42, "premium": 10, "payout": 500,
                              "active": True, "expiry_ts": "later"}]},
    })

    client.insurance_list_flow(ctx)

    assert fake_conn.calls == [{"command": "insurance.policies.list", "data": {}}]
    assert "Policy 7: ship 42" in capsys.readouterr().out


def test_buy_policy_sends_hull_only_contract_and_requires_matching_receipt(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory()
    fake_conn.queue("insurance.policies.buy", {
        "status": "ok", "type": "insurance.policies.buy.response",
        "data": {"policy_id": 19, "status": "purchased"},
    })
    inputs = iter(["42", "3600", "10", "500", "3"])
    monkeypatch.setattr("builtins.input", lambda *_: next(inputs))

    client.insurance_buy_flow(ctx)

    assert fake_conn.calls == [{"command": "insurance.policies.buy", "data": {
        "subject_type": "ship", "subject_id": 42, "duration": 3600,
        "premium": 10, "payout": 500, "fund_id": 3,
    }}]
    assert "policy 19 purchased" in capsys.readouterr().out


def test_buy_policy_invalid_values_do_not_send_rpc(ctx_factory, fake_conn, monkeypatch, capsys):
    ctx = ctx_factory()
    inputs = iter(["42", "0", "10", "500", "3"])
    monkeypatch.setattr("builtins.input", lambda *_: next(inputs))

    client.insurance_buy_flow(ctx)

    assert fake_conn.calls == []
    assert "must be positive" in capsys.readouterr().out


def test_claim_workflow_requires_matching_settlement_receipt(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory()
    fake_conn.queue("insurance.claim.file", {
        "status": "ok", "type": "insurance.claim.file.response",
        "data": {"claim_id": 9, "status": "settled"},
    })
    inputs = iter(["9", "500", "event-17"])
    monkeypatch.setattr("builtins.input", lambda *_: next(inputs))

    client.insurance_claim_flow(ctx)

    assert fake_conn.calls == [{"command": "insurance.claim.file", "data": {
        "claim_id": 9, "event_id": "event-17", "amount": 500,
    }}]
    assert "claim 9 settled" in capsys.readouterr().out
