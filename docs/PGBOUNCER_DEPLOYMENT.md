# PgBouncer Connection Pooling - Production Deployment

## Overview
Deploy PgBouncer as a connection pooling proxy between the game server and PostgreSQL to handle 100+ concurrent connections efficiently.

## Architecture

```
100 Game Server Threads
        ↓
   Game Server (localhost:1234)
        ↓
   PgBouncer Proxy (localhost:6432)
   - Connection Pooling
   - Transaction Pooling
   - 25 connections to PostgreSQL
        ↓
   PostgreSQL (localhost:5432)
```

## Key Configuration Parameters

| Parameter | Value | Purpose |
|-----------|-------|---------|
| `pool_mode` | transaction | Recycle connections after each transaction (efficient) |
| `default_pool_size` | 25 | Main pool size - handles 25 concurrent DB users |
| `min_pool_size` | 5 | Minimum maintained connections |
| `reserve_pool_size` | 5 | Extra reserve for spikes |
| `max_client_conn` | 1000 | Max clients PgBouncer accepts |
| `max_db_connections` | 100 | Max connections to PostgreSQL |

## Performance Characteristics

**Before (Direct to PostgreSQL):**
- 100 bots = 100 PostgreSQL connections
- Exceeds max_connections limit (~115)
- Auth failure rate: 47%

**After (With PgBouncer):**
- 100 bots = 25-30 PostgreSQL connections
- Well within limits
- Estimated auth success: 99%+
- Connection reuse via transaction pooling

## Deployment Steps

### 1. Install PgBouncer
```bash
apt install pgbouncer -y
```

### 2. Create Configuration
Copy the provided `pgbouncer.ini` to `/etc/pgbouncer/pgbouncer.ini`

### 3. Create User Authentication File
```bash
echo '"postgres" "B1lb0 Bagg1ns!"' > /etc/pgbouncer/userlist.txt
chmod 600 /etc/pgbouncer/userlist.txt
```

### 4. Create Required Directories
```bash
mkdir -p /var/log/pgbouncer
mkdir -p /var/run/pgbouncer
chown pgbouncer:pgbouncer /var/log/pgbouncer
chown pgbouncer:pgbouncer /var/run/pgbouncer
chmod 750 /var/log/pgbouncer
chmod 750 /var/run/pgbouncer
```

### 5. Update Server Configuration
Change database connection in game server:
- **From**: `localhost:5432` (direct PostgreSQL)
- **To**: `localhost:6432` (PgBouncer proxy)

Update server's database connection code:
```c
// In server database initialization:
db_connect("localhost", 6432, "twclone", "postgres", "B1lb0 Bagg1ns!");
```

### 6. Start PgBouncer
```bash
systemctl start pgbouncer
systemctl enable pgbouncer
```

### 7. Verify Configuration
```bash
# Check PgBouncer is listening
netstat -tlnp | grep pgbouncer

# Check logs
tail -f /var/log/pgbouncer/pgbouncer.log

# Monitor pool stats
psql -h 127.0.0.1 -p 6432 -U postgres -d pgbouncer -c "SHOW POOLS;"
```

## Monitoring Commands

```bash
# View current pools
psql -h 127.0.0.1 -p 6432 -U postgres -d pgbouncer -c "SHOW POOLS;"

# View current clients
psql -h 127.0.0.1 -p 6432 -U postgres -d pgbouncer -c "SHOW CLIENTS;"

# View server connections
psql -h 127.0.0.1 -p 6432 -U postgres -d pgbouncer -c "SHOW SERVERS;"

# View statistics
psql -h 127.0.0.1 -p 6432 -U postgres -d pgbouncer -c "SHOW STATS;"

# View config
psql -h 127.0.0.1 -p 6432 -U postgres -d pgbouncer -c "SHOW CONFIG;"
```

## Scaling Considerations

### For 100-500 Concurrent Users:
```ini
pool_mode = transaction
default_pool_size = 25
max_client_conn = 500
```

### For 500-1000 Concurrent Users:
```ini
pool_mode = transaction
default_pool_size = 50
max_client_conn = 1000
```

### For 1000+ Concurrent Users:
- Consider statement pooling instead of transaction pooling
- Increase default_pool_size to 75-100
- Add connection pooling layer or use pgpool-II
- Implement read replicas for scaling

## Troubleshooting

### Too many clients error persists
- Check `max_client_conn` is higher than active connections
- Verify pool_mode is `transaction` (not `statement`)
- Check reserve_pool_size

### Slow queries
- Check `server_idle_timeout` (600 seconds default is good)
- Monitor pool stats for connection churn
- Consider statement pooling if needed

### Connection refused
- Verify PostgreSQL is running on port 5432
- Check userlist.txt has correct credentials
- Verify pgbouncer.ini database entry

## Production Checklist

- [ ] PgBouncer installed
- [ ] Configuration files in place
- [ ] Directory permissions set correctly
- [ ] Userlist.txt created and secured
- [ ] PostgreSQL max_connections >= 100
- [ ] Game server updated to use port 6432
- [ ] PgBouncer started and enabled
- [ ] Monitoring configured
- [ ] Stress test with 100 bots passes
- [ ] Log rotation configured
- [ ] Backup of configuration files

## Expected Results After Deployment

| Metric | Before | After |
|--------|--------|-------|
| PostgreSQL connections needed | 100 | 25-30 |
| Max concurrent bots | ~50 | 500+ |
| Auth success rate (100 bots) | 47% | 99%+ |
| Memory per connection | 10MB | 0.4MB (shared) |
| Latency overhead | 0ms | <1ms |

## Rollback Plan

If issues arise:
1. Stop PgBouncer: `systemctl stop pgbouncer`
2. Update server to connect to port 5432 directly
3. Restart game server
4. Connection returns to direct PostgreSQL

---

*Connection pooling solution for production deployment*
