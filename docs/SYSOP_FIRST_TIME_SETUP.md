# twclone Server: Sysop First-Time Setup Guide

**Target Audience:** Systems operators or admins setting up twclone for the first time.

**Duration:** 30–60 minutes depending on whether PostgreSQL is pre-installed.

**Last Updated:** 2026-03-03

---

## Table of Contents

1. [Pre-Installation Checklist](#pre-installation-checklist)
2. [Prerequisites & Dependencies](#prerequisites--dependencies)
3. [Building from Source](#building-from-source)
4. [Database Setup (PostgreSQL)](#database-setup-postgresql)
5. [Creating the Universe (Big Bang)](#creating-the-universe-big-bang)
6. [Starting the Server](#starting-the-server)
7. [Verifying the Installation](#verifying-the-installation)
8. [Optional: Enabling TLS/SSL](#optional-enabling-tlsssl)
9. [Running Tests](#running-tests)
10. [Troubleshooting](#troubleshooting)
11. [Daily Operations](#daily-operations)

---

## Pre-Installation Checklist

Before you start, gather or prepare:

- [ ] A Linux/macOS/WSL system with root or `sudo` access
- [ ] PostgreSQL 12+ or MySQL 8.0+ (PostgreSQL recommended)
- [ ] Development tools: GCC or Clang, GNU make, pkg-config
- [ ] PostgreSQL dev libraries (`libpq-dev` on Ubuntu, `postgresql-devel` on RHEL)
- [ ] About 2 GB free disk space for the database
- [ ] A user account for running the server (e.g., `twclone`) — recommended for production

---

## Prerequisites & Dependencies

### On Ubuntu/Debian

```bash
sudo apt-get update
sudo apt-get install -y \
  build-essential \
  postgresql postgresql-contrib \
  libpq-dev \
  libjansson-dev \
  libreadline-dev \
  libssl-dev \
  pkg-config
```

### On RHEL/CentOS/Fedora

```bash
sudo dnf groupinstall -y "Development Tools"
sudo dnf install -y \
  postgresql-server postgresql-contrib \
  postgresql-devel \
  jansson-devel \
  readline-devel \
  openssl-devel \
  pkg-config
```

### On macOS (with Homebrew)

```bash
brew install postgresql jansson readline openssl pkg-config
```

### Verify Installation

```bash
# Check GCC/Clang
gcc --version   # or clang --version

# Check PostgreSQL client
psql --version

# Check OpenSSL (for TLS support)
openssl version
```

---

## Building from Source

### 1. Clone or Navigate to Repository

```bash
cd /home/twclone  # Or your twclone directory
```

### 2. Clean and Build

```bash
make clean
make -j$(nproc)   # Parallel build using all CPU cores
```

**Expected output:**
- Compiles `src/*.c` files
- Links against PostgreSQL, Jansson, OpenSSL, readline
- Produces three binaries in `./bin/`:
  - `bin/server` — main game server
  - `bin/bigbang` — universe generator
  - `bin/client` — test client (optional)

**Verbose build** (if troubleshooting):
```bash
make V=1
```

### 3. Verify Binaries Exist

```bash
ls -lah bin/server bin/bigbang bin/client
# Should show three executable files
```

---

## Database Setup (PostgreSQL)

### 1. Start PostgreSQL Service

```bash
# On systemd systems
sudo systemctl start postgresql
sudo systemctl enable postgresql  # Auto-start on boot

# On older systems
sudo service postgresql start
```

### 2. Create Database User and Database

```bash
# Connect as postgres superuser
sudo -u postgres psql

# Inside psql prompt, run:
CREATE USER twclone_user WITH PASSWORD 'your_secure_password';
CREATE DATABASE twclone_db OWNER twclone_user;
ALTER USER twclone_user CREATEDB;
\q
```

### 3. Verify Database Connection

```bash
psql -h localhost -U twclone_user -d twclone_db -c "SELECT version();"
```

You should see PostgreSQL version info. If you get a password prompt, enter `your_secure_password`.

### 4. Note the Connection String

You'll need this for the Big Bang configuration:

```
postgresql://twclone_user:your_secure_password@localhost:5432/twclone_db
```

---

## Creating the Universe (Big Bang)

The **Big Bang** tool initializes the universe with sectors, ports, and navigation links.

### 1. Create Configuration File

Copy the sample and edit it:

```bash
cd /home/twclone

# Create the config (if not already present)
cat > bin/bigbang.json << 'EOF'
{
  "db_connection": "postgresql://twclone_user:your_secure_password@localhost:5432/twclone_db",
  "num_sectors": 500,
  "density": 5,
  "port_ratio": 0.15,
  "planet_ratio": 0.1,
  "port_size": 10,
  "tech_level": 5,
  "port_credits": 50000,
  "min_tunnels": 2,
  "min_tunnel_len": 3
}
EOF
```

**Key Parameters:**
- `num_sectors`: Total game sectors (500 is standard)
- `density`: Warp link density (5 = medium, 3 = sparse, 7 = dense)
- `port_ratio`: Proportion of sectors with ports (0.15 = 15%)
- `planet_ratio`: Proportion of sectors with planets (0.1 = 10%)

### 2. Adjust Connection String

Edit `bin/bigbang.json` and replace:
- `twclone_user` — your database user
- `your_secure_password` — the password you set above
- `localhost` — your database host (usually `localhost` for local installs)

### 3. Run Big Bang

```bash
cd /home/twclone
./bin/bigbang
```

**Expected output:**
```
BIGBANG: Connecting to database...
BIGBANG: Creating 500 sectors...
BIGBANG: Creating ports and planets...
BIGBANG: Generating warp network (tunnels, one-ways, dead-ends)...
BIGBANG: Universe created successfully.
```

### 4. Verify Database Population

```bash
psql -U twclone_user -d twclone_db -c "SELECT COUNT(*) FROM sectors;"
# Should return: count = 500 (or your num_sectors value)

psql -U twclone_user -d twclone_db -c "SELECT COUNT(*) FROM ports;"
# Should return non-zero count

psql -U twclone_user -d twclone_db -c "SELECT * FROM config LIMIT 5;"
# Should show initial config (turnsperday, startingcredits, etc.)
```

---

## Starting the Server

### 1. Ensure Database Connection String is Accessible

The server reads the database connection from the environment or hardcoded defaults. Verify `src/server_config.c` or set:

```bash
export DATABASE_URL="postgresql://twclone_user:your_secure_password@localhost:5432/twclone_db"
```

### 2. Run the Server

```bash
cd /home/twclone

# Basic startup
./bin/server --host 0.0.0.0 --port 1234

# Or with explicit host/port
./bin/server --host 0.0.0.0 --port 1234 &
```

**Expected output** (check logs):
```bash
tail -f twclone.log
```

You should see:
```
[INFO] server starting…
[INFO] listening on 0.0.0.0:1234
[INFO] Engine process started (PID: XXXX)
[INFO] Database connected
```

### 3. Background Execution (Production)

To run in background and survive terminal close:

```bash
nohup ./bin/server --host 0.0.0.0 --port 1234 > twclone.log 2>&1 &
echo $! > twclone.pid
```

Or use systemd (recommended for production):

```bash
sudo tee /etc/systemd/system/twclone.service > /dev/null << 'EOF'
[Unit]
Description=twclone Game Server
After=network.target postgresql.service

[Service]
Type=simple
User=twclone
WorkingDirectory=/home/twclone
ExecStart=/home/twclone/bin/server --host 0.0.0.0 --port 1234
Restart=on-failure
RestartSec=10
StandardOutput=append:/var/log/twclone/server.log
StandardError=append:/var/log/twclone/server.log

[Install]
WantedBy=multi-user.target
EOF

sudo mkdir -p /var/log/twclone
sudo chown twclone:twclone /var/log/twclone
sudo systemctl enable twclone
sudo systemctl start twclone
```

---

## Verifying the Installation

### 1. Server is Running

```bash
pgrep -f "bin/server"
# Should print a PID (process ID)
```

### 2. Server Port is Listening

```bash
netstat -tlnp | grep 1234
# Or on newer systems:
ss -tlnp | grep 1234
```

Expected output should show something like:
```
tcp        0      0 0.0.0.0:1234            0.0.0.0:*               LISTEN      1234/bin/server
```

### 3. Quick Connectivity Test

```bash
# Using netcat (nc)
echo '{"cmd":"system.hello"}' | nc localhost 1234

# Or using telnet
telnet localhost 1234
# (type '{"cmd":"system.hello"}' and press Ctrl+D)
```

Both should return a JSON response (possibly an error, but the connection succeeds).

### 4. Run the Test Suite

```bash
cd /home/twclone
python3 tests.v2/run_suites_all.py 2>&1 | head -50
```

This runs all test suites and prints a summary. You should see:
```
>>> EXECUTING SUITE: tests.v2/suite_login.json
Test: Login succeeds with valid credentials... PASS
...
```

---

## Optional: Enabling TLS/SSL

TLS (encrypted connections) is optional but recommended for production.

### Quick Setup (5 minutes)

#### 1. Generate Self-Signed Certificate

```bash
openssl req -x509 -newkey rsa:2048 -nodes \
  -keyout /etc/twclone/tls_key.pem \
  -out /etc/twclone/tls_cert.pem \
  -days 365 \
  -subj "/CN=$(hostname)"

# Create directory if needed
sudo mkdir -p /etc/twclone
sudo chmod 600 /etc/twclone/tls_key.pem
sudo chmod 644 /etc/twclone/tls_cert.pem
```

For **production**, obtain a certificate from a Certificate Authority (Let's Encrypt, DigiCert, etc.) instead.

#### 2. Enable in Database

```bash
psql -U twclone_user -d twclone_db << SQL
INSERT INTO config (key, value, type) VALUES 
  ('tls_enabled', '1', 'int'),
  ('tls_cert_path', '/etc/twclone/tls_cert.pem', 'string'),
  ('tls_key_path', '/etc/twclone/tls_key.pem', 'string')
ON CONFLICT (key) DO UPDATE 
SET value = EXCLUDED.value, type = EXCLUDED.type;
SQL
```

#### 3. Restart Server

```bash
# If running in foreground, press Ctrl+C
# If running in background:
kill $(pgrep -f "bin/server")
sleep 2

# Restart
./bin/server --host 0.0.0.0 --port 1234 &
```

#### 4. Verify TLS is Working

```bash
echo '{"cmd":"system.hello"}' | \
  openssl s_client -connect localhost:1234 -quiet -CAfile /etc/twclone/tls_cert.pem
```

You should see a brief certificate output followed by JSON response.

**For more details**, see `docs/TLS_CONFIGURATION_GUIDE.txt`.

---

## Running Tests

### Full Test Suite

```bash
cd /home/twclone
python3 tests.v2/run_suites_all.py
```

This runs all registered test suites and reports pass/fail.

### Single Test Suite

```bash
python3 tests.v2/json_runner.py tests.v2/suite_login.json
```

### With Verbose Output

```bash
python3 tests.v2/run_suites_all.py -v 2>&1 | tee test_results.log
```

### Expected Result

```
>>> EXECUTING SUITE: tests.v2/suite_login.json
Test: Login succeeds with valid credentials... PASS
Test: Login fails with invalid credentials... PASS
Test: Logout succeeds... PASS
>>> SUITE PASSED: tests.v2/suite_login.json

>>> EXECUTING SUITE: tests.v2/suite_ships.json
Test: List ships succeeds... PASS
...
```

---

## Troubleshooting

### Problem: "Connection refused" when starting server

**Cause:** Database not running or connection string incorrect.

**Solution:**
```bash
# Check PostgreSQL is running
sudo systemctl status postgresql

# Verify database exists
psql -l | grep twclone_db

# Check connection string in bin/bigbang.json and server code
grep -r "DATABASE_URL\|db_connection" src/ bin/
```

### Problem: Server starts but tests fail to connect

**Cause:** Server not listening on the correct port.

**Solution:**
```bash
# Check server is truly running
ps aux | grep bin/server

# Verify it's listening
ss -tlnp | grep 1234

# Check logs
tail -50 twclone.log

# Try manual connection
telnet localhost 1234
```

### Problem: "Failed to load TLS certificate"

**Cause:** Certificate or key file doesn't exist or is unreadable.

**Solution:**
```bash
# Check files exist
ls -la /etc/twclone/tls_*.pem

# Check readable
cat /etc/twclone/tls_cert.pem | head -1
# Should print: -----BEGIN CERTIFICATE-----

# Verify permissions
sudo chmod 644 /etc/twclone/tls_cert.pem
sudo chmod 600 /etc/twclone/tls_key.pem

# Check config in database
psql -U twclone_user -d twclone_db \
  -c "SELECT key, value FROM config WHERE key LIKE 'tls%';"

# Restart server
kill $(pgrep -f "bin/server")
sleep 2
./bin/server &
```

### Problem: "Too many connections" error in logs

**Cause:** Database connection limit reached (default: 100 connections).

**Solution:** Use PgBouncer connection pooling. See `docs/PGBOUNCER_DEPLOYMENT.md` for setup instructions.

### Problem: Universe generation hangs or fails

**Cause:** Database connection issue or invalid configuration.

**Solution:**
```bash
# Test database connection directly
psql -h localhost -U twclone_user -d twclone_db -c "SELECT 1;"

# Check Big Bang config for typos
cat bin/bigbang.json | python3 -m json.tool

# Run Big Bang with verbose output (if available)
./bin/bigbang --verbose

# Drop and recreate database if corrupted
psql -U postgres << SQL
DROP DATABASE twclone_db;
CREATE DATABASE twclone_db OWNER twclone_user;
SQL

./bin/bigbang
```

---

## Daily Operations

### Monitoring

**Check server is running:**
```bash
pgrep -f "bin/server" && echo "Server is running" || echo "Server is down!"
```

**Monitor logs in real time:**
```bash
tail -f twclone.log
```

**Check database size:**
```bash
psql -U twclone_user -d twclone_db -c \
  "SELECT pg_size_pretty(pg_database_size('twclone_db'));"
```

**Check active connections:**
```bash
psql -U twclone_user -d twclone_db -c \
  "SELECT count(*) FROM pg_stat_activity WHERE datname = 'twclone_db';"
```

### Restart the Server

```bash
# Kill existing process
kill $(pgrep -f "bin/server")
sleep 2

# Start fresh
./bin/server --host 0.0.0.0 --port 1234 &
```

Or with systemd:
```bash
sudo systemctl restart twclone
```

### Backup Database

```bash
# PostgreSQL dump
pg_dump -U twclone_user twclone_db > twclone_backup_$(date +%Y%m%d_%H%M%S).sql

# Or with compression
pg_dump -U twclone_user twclone_db | gzip > twclone_backup_$(date +%Y%m%d_%H%M%S).sql.gz
```

### Restore from Backup

```bash
# Drop and recreate
psql -U postgres -c "DROP DATABASE twclone_db;"
psql -U postgres -c "CREATE DATABASE twclone_db OWNER twclone_user;"

# Restore
psql -U twclone_user -d twclone_db < twclone_backup_YYYYMMDD_HHMMSS.sql

# Or from compressed backup
gunzip -c twclone_backup_YYYYMMDD_HHMMSS.sql.gz | psql -U twclone_user -d twclone_db
```

### Check Configuration

```bash
# View current game settings
psql -U twclone_user -d twclone_db -c \
  "SELECT key, value, type FROM config ORDER BY key LIMIT 20;"

# Update a setting (e.g., turns per day)
psql -U twclone_user -d twclone_db -c \
  "UPDATE config SET value = '8' WHERE key = 'turnsperday';"
```

**Note:** Some config changes require server restart to take effect. Check `docs/DATABASE_RULES.md` for live-reload behavior.

---

## Additional Resources

- **`README.md`** — Project overview and quick start
- **`docs/ENGINE.md`** — Game engine design, tick loop, cron tasks
- **`docs/DATABASE_RULES.md`** — Database architecture and SQL rules
- **`docs/PROTOCOL.v3/`** — Complete JSON protocol specification
- **`docs/PGBOUNCER_DEPLOYMENT.md`** — Scaling to 500+ concurrent users
- **`docs/TLS_CONFIGURATION_GUIDE.txt`** — Detailed TLS/SSL setup
- **`tests.v2/README_RIG.md`** — Test framework documentation

---

## Support & Troubleshooting

**For detailed diagnostics:**

```bash
# Server log
tail -100 twclone.log | grep -i error

# Database log (PostgreSQL)
sudo tail -100 /var/log/postgresql/postgresql.log | grep twclone_db

# Network connections
netstat -tlnp | grep 1234
```

**Common Commands:**

| Task | Command |
|------|---------|
| Verify server running | `pgrep -f "bin/server"` |
| Check listening ports | `ss -tlnp \| grep 1234` |
| View recent logs | `tail -50 twclone.log` |
| Test database | `psql -U twclone_user -d twclone_db -c "SELECT 1;"` |
| Reload config | Bump `config_version` in database; server reloads automatically |
| Kill server gracefully | `kill $(pgrep -f "bin/server")` |
| Run test suite | `python3 tests.v2/run_suites_all.py` |

---

## Next Steps

1. **Verify the installation** by running tests (see [Running Tests](#running-tests)).
2. **Enable TLS** for secure connections if running on an untrusted network (see [Optional: Enabling TLS/SSL](#optional-enabling-tlsssl)).
3. **Set up automated backups** using cron or your backup service.
4. **Configure monitoring** (Prometheus, Nagios, etc.) if running in production.
5. **Review security hardening** in `docs/PRODUCTION_DEPLOYMENT_GUIDE.md` for multi-user setups.

---

**Good luck! The server is now ready for gameplay. Happy twcloning!** 🚀
