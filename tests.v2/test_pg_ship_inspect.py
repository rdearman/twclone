"""Live ship.inspect visibility regression, for a disposable PostgreSQL server.

Call check_ship_inspection with a fresh registered player and its database;
all modified player and ship rows must belong to the disposable test world.
"""
import secrets

from test_pg_bigbang_economy_profit import login_client, status_ok
from twclient import TWClient


def check_ship_inspection(conn, host, port, account, password):
    other = 'inspect_qa_' + secrets.token_hex(5)
    other_password = secrets.token_urlsafe(28)
    registration = TWClient(host, port, timeout=10)
    registration.connect()
    try:
        registration.send_json({'command': 'ship.inspect', 'data': {'ship_id': 1}})
        if status_ok(registration.recv_next_non_notice()):
            raise AssertionError('unauthenticated inspection succeeded')
        if not registration.register(other, other_password, fail_if_exists=True):
            raise AssertionError('second player registration failed')
    finally:
        registration.close()
    with conn.cursor() as cur:
        cur.execute('SELECT player_id, ship_id, sector_id FROM players WHERE name=%s', (account,))
        player, own_ship, sector = cur.fetchone()
        cur.execute('SELECT player_id, ship_id FROM players WHERE name=%s', (other,))
        other_player, other_ship = cur.fetchone()
        cur.execute('UPDATE players SET sector_id=%s WHERE player_id=%s', (sector, other_player))
        cur.execute('UPDATE ships SET sector_id=%s, cloaked=NULL WHERE ship_id=%s', (sector, other_ship))
        cur.execute('UPDATE ships SET cloaked=CURRENT_TIMESTAMP WHERE ship_id=%s', (own_ship,))
    conn.commit()
    client = login_client(host, port, account, password)
    try:
        def inspect(ship_id):
            client.send_json({'command': 'ship.inspect', 'data': {'ship_id': ship_id}})
            return client.recv_next_non_notice()

        for data in ({}, {'ship_id': 0}, {'ship_id': -1}, {'ship_id': '1'},
                     {'ship_id': own_ship, 'sector_id': sector}):
            client.send_json({'command': 'ship.inspect', 'data': data})
            response = client.recv_next_non_notice()
            if status_ok(response):
                raise AssertionError(f'invalid request {data!r} succeeded: {response!r}')

        for ship_id in (own_ship, other_ship):
            response = inspect(ship_id)
            ship = response.get('data', {}).get('ship', {})
            if (not status_ok(response) or ship.get('ship_id') != ship_id
                    or set(ship) != {'ship_id', 'name', 'is_derelict'}
                    or ship.get('is_derelict') is not False):
                raise AssertionError(f'visible ship {ship_id} returned {response!r}')
        with conn.cursor() as cur:
            cur.execute('UPDATE ships SET cloaked=CURRENT_TIMESTAMP WHERE ship_id=%s', (other_ship,))
        conn.commit()
        hidden = inspect(other_ship)
        missing = inspect(2147483647)
        if hidden.get('status') != 'error' or hidden.get('error') != missing.get('error'):
            raise AssertionError(f'cloaked/missing distinction: {hidden!r} / {missing!r}')
        with conn.cursor() as cur:
            cur.execute('SELECT sector_id FROM sectors WHERE sector_id<>%s LIMIT 1', (sector,))
            remote = cur.fetchone()[0]
            cur.execute('UPDATE ships SET sector_id=%s, cloaked=NULL WHERE ship_id=%s', (remote, other_ship))
        conn.commit()
        distant = inspect(other_ship)
        if distant.get('status') != 'error' or distant.get('error') != missing.get('error'):
            raise AssertionError(f'remote/missing distinction: {distant!r} / {missing!r}')
    finally:
        client.close()
    print('PASS ship.inspect: authentication, invalid requests, selected ship, own cloak, foreign cloak, remote, missing')
