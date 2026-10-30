"""Verify unsupported cargo transfer cannot change the caller's cloak."""
from test_pg_bigbang_economy_profit import login_client, status_ok


def check_cargo_refusal(conn, host, port, account, password):
    with conn.cursor() as cur:
        cur.execute('SELECT ship_id FROM players WHERE name=%s', (account,))
        ship_id = cur.fetchone()[0]
        cur.execute('UPDATE ships SET cloaked=CURRENT_TIMESTAMP WHERE ship_id=%s RETURNING cloaked', (ship_id,))
        cloak = cur.fetchone()[0]
    conn.commit()
    client = login_client(host, port, account, password)
    try:
        client.send_json({'command': 'ship.transfer_cargo', 'data': {
            'commodity_id': 1, 'quantity': 1, 'target_ship_id': ship_id,
        }})
        response = client.recv_next_non_notice()
        if status_ok(response) or 'not implemented' not in str(response).lower():
            raise AssertionError(f'unexpected cargo transfer response: {response!r}')
        with conn.cursor() as cur:
            cur.execute('SELECT cloaked FROM ships WHERE ship_id=%s', (ship_id,))
            if cur.fetchone()[0] != cloak:
                raise AssertionError('refused transfer changed cloak state')
        conn.commit()
    finally:
        client.close()
    print('PASS unsupported ship.transfer_cargo refuses without changing cloak')
