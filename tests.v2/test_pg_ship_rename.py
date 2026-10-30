"""Live active-ship rename contract checks in a disposable PostgreSQL world."""
from test_pg_bigbang_economy_profit import login_client, status_ok
from twclient import TWClient


def check_ship_rename(conn, host, port, account, password):
    with conn.cursor() as cur:
        cur.execute('SELECT player_id, ship_id FROM players WHERE name=%s', (account,))
        player_id, ship_id = cur.fetchone()
    client = login_client(host, port, account, password)
    try:
        def call(command, data):
            client.send_json({'command': command, 'data': data})
            return client.recv_next_non_notice()

        for command in ('ship.rename', 'ship.reregister'):
            name = 'Renamed ' + command
            result = call(command, {'name': name})
            if not status_ok(result) or result.get('data') != {'ship_id': ship_id, 'name': name}:
                raise AssertionError(f'{command} failed: {result!r}')
            with conn.cursor() as cur:
                cur.execute('SELECT name FROM ships WHERE ship_id=%s', (ship_id,))
                if cur.fetchone()[0] != name:
                    raise AssertionError('rename was not persisted')
            conn.commit()
            if not status_ok(call(command, {'name': name})):
                raise AssertionError('renaming to the current name should succeed')
        for data in ({}, {'name': ''}, {'name': 'x' * 101}, {'name': 'bad', 'ship_id': ship_id}):
            if status_ok(call('ship.rename', data)):
                raise AssertionError(f'invalid rename accepted: {data!r}')
        with conn.cursor() as cur:
            cur.execute('UPDATE ship_ownership SET is_primary=FALSE WHERE ship_id=%s AND player_id=%s', (ship_id, player_id))
        conn.commit()
        if status_ok(call('ship.rename', {'name': 'Unauthorized'})):
            raise AssertionError('rename succeeded without primary ownership')
        with conn.cursor() as cur:
            cur.execute('SELECT name FROM ships WHERE ship_id=%s', (ship_id,))
            if cur.fetchone()[0] != 'Renamed ship.reregister':
                raise AssertionError('unauthorized rename changed ship name')
        conn.commit()
    finally:
        client.close()
    unauth = TWClient(host, port, timeout=10)
    unauth.connect()
    try:
        unauth.send_json({'command': 'ship.rename', 'data': {'name': 'Unauthorized'}})
        if status_ok(unauth.recv_next_non_notice()):
            raise AssertionError('unauthenticated rename succeeded')
    finally:
        unauth.close()
    print('PASS ship.rename: persistence, aliases, invalid input, ownership, authentication')
