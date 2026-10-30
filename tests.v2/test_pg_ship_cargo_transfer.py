"""Live PostgreSQL regression for authorized ship-to-ship cargo transfer."""
from test_pg_bigbang_economy_profit import login_client, status_ok
from twclient import TWClient


def check_cargo_transfer(conn, host, port, account, password):
    other_name = "cargo_transfer_other"
    other_password = "CargoTransferOther-1"
    registration = TWClient(host, port, timeout=10)
    registration.connect()
    try:
        registration.register(other_name, other_password, fail_if_exists=True)
    finally:
        registration.close()
    with conn.cursor() as cur:
        cur.execute("SELECT player_id, ship_id, sector_id FROM players WHERE name=%s", (account,))
        player_id, source_ship, sector = cur.fetchone()
        cur.execute("SELECT player_id, ship_id FROM players WHERE name=%s", (other_name,))
        other_id, unauthorized_ship = cur.fetchone()
        cur.execute("UPDATE players SET sector_id=%s WHERE player_id IN (%s,%s)", (sector, player_id, other_id))
        cur.execute("UPDATE ships SET sector_id=%s, holds=20 WHERE ship_id IN (%s,%s)", (sector, source_ship, unauthorized_ship))
        cur.execute("INSERT INTO ship_ownership (ship_id, player_id, role_id, is_primary) VALUES (%s,%s,1,TRUE) ON CONFLICT DO NOTHING", (unauthorized_ship, player_id))
        cur.execute("SELECT commodities_id FROM commodities WHERE code='ORE'")
        commodity_id = cur.fetchone()[0]
        cur.execute("DELETE FROM ship_cargo WHERE ship_id IN (%s,%s)", (source_ship, unauthorized_ship))
        cur.execute("INSERT INTO ship_cargo (ship_id, commodity_code, quantity) VALUES (%s,'ORE',5)", (source_ship,))
    conn.commit()
    client = login_client(host, port, account, password)
    try:
        def call(target, quantity):
            client.send_json({'command': 'ship.transfer_cargo', 'data': {'commodity_id': commodity_id, 'quantity': quantity, 'target_ship_id': target}})
            return client.recv_next_non_notice()
        result = call(unauthorized_ship, 2)
        if not status_ok(result) or result.get('data', {}).get('quantity') != 2:
            raise AssertionError(f'authorized transfer failed: {result!r}')
        with conn.cursor() as cur:
            cur.execute("SELECT quantity FROM ship_cargo WHERE ship_id=%s AND commodity_code='ORE'", (source_ship,))
            if cur.fetchone()[0] != 3:
                raise AssertionError('source cargo was not decremented')
            cur.execute("SELECT quantity FROM ship_cargo WHERE ship_id=%s AND commodity_code='ORE'", (unauthorized_ship,))
            if cur.fetchone()[0] != 2:
                raise AssertionError('target cargo was not incremented')
            cur.execute("UPDATE ship_ownership SET is_primary=FALSE WHERE ship_id=%s AND player_id=%s", (unauthorized_ship, player_id))
        conn.commit()
        if status_ok(call(unauthorized_ship, 1)):
            raise AssertionError('unauthorized transfer succeeded')
        with conn.cursor() as cur:
            cur.execute("UPDATE ship_ownership SET is_primary=TRUE WHERE ship_id=%s AND player_id=%s", (unauthorized_ship, player_id))
            cur.execute("UPDATE ships SET holds=1 WHERE ship_id=%s", (unauthorized_ship,))
        conn.commit()
        if status_ok(call(unauthorized_ship, 1)):
            raise AssertionError('capacity refusal succeeded')
        with conn.cursor() as cur:
            cur.execute("SELECT quantity FROM ship_cargo WHERE ship_id=%s AND commodity_code='ORE'", (source_ship,))
            before = cur.fetchone()[0]
        if status_ok(call(unauthorized_ship, 99)):
            raise AssertionError('insufficient cargo refusal succeeded')
        with conn.cursor() as cur:
            cur.execute("SELECT quantity FROM ship_cargo WHERE ship_id=%s AND commodity_code='ORE'", (source_ship,))
            if cur.fetchone()[0] != before:
                raise AssertionError('refused transfer changed source cargo')
    finally:
        client.close()
    print('PASS ship.transfer_cargo: atomic success, cargo accounting and ownership refusal')
