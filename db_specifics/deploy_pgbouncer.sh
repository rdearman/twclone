#!/bin/bash
set -e

# Production-ready PgBouncer deployment script

echo "════════════════════════════════════════════════════════════"
echo "🚀 PgBouncer Connection Pooling - Production Deployment"
echo "════════════════════════════════════════════════════════════"
echo ""

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Configuration
DB_HOST="localhost"
DB_PORT="5432"
DB_NAME="twclone"
DB_USER="postgres"
DB_PASSWORD="B1lb0 Bagg1ns!"
PGBOUNCER_PORT="6432"

echo -e "${YELLOW}Step 1: Installing PgBouncer...${NC}"
if ! command -v pgbouncer &> /dev/null; then
    echo "PgBouncer not found. Installing..."
    apt-get update -qq && apt-get install -y pgbouncer > /dev/null 2>&1
    echo -e "${GREEN}✓ PgBouncer installed${NC}"
else
    echo -e "${GREEN}✓ PgBouncer already installed${NC}"
fi
echo ""

echo -e "${YELLOW}Step 2: Creating PgBouncer configuration...${NC}"
mkdir -p /etc/pgbouncer
cat > /etc/pgbouncer/pgbouncer.ini << 'EOF'
[databases]
twclone = host=localhost port=5432 dbname=twclone user=postgres password=B1lb0\ Bagg1ns!

[pgbouncer]
logfile = /var/log/pgbouncer/pgbouncer.log
pidfile = /var/run/pgbouncer/pgbouncer.pid

listen_port = 6432
listen_addr = 127.0.0.1
unix_socket_dir = /var/run/pgbouncer

auth_type = plain
auth_file = /etc/pgbouncer/userlist.txt

pool_mode = transaction
max_client_conn = 1000
default_pool_size = 25
min_pool_size = 5
reserve_pool_size = 5
reserve_pool_timeout = 3

max_db_connections = 100
max_user_connections = 100

statement_timeout = 0
query_timeout = 0
idle_in_transaction_session_timeout = 0

server_lifetime = 3600
server_idle_timeout = 600
server_connect_timeout = 15
server_login_retry = 15

application_name_add_host = 0
verbose = 1

stats_period = 60
EOF
echo -e "${GREEN}✓ Configuration created at /etc/pgbouncer/pgbouncer.ini${NC}"
echo ""

echo -e "${YELLOW}Step 3: Creating user authentication file...${NC}"
echo '"postgres" "B1lb0 Bagg1ns!"' > /etc/pgbouncer/userlist.txt
chmod 600 /etc/pgbouncer/userlist.txt
echo -e "${GREEN}✓ User authentication file created and secured${NC}"
echo ""

echo -e "${YELLOW}Step 4: Creating required directories...${NC}"
mkdir -p /var/log/pgbouncer
mkdir -p /var/run/pgbouncer
chown pgbouncer:pgbouncer /var/log/pgbouncer 2>/dev/null || true
chown pgbouncer:pgbouncer /var/run/pgbouncer 2>/dev/null || true
chmod 750 /var/log/pgbouncer
chmod 750 /var/run/pgbouncer
echo -e "${GREEN}✓ Directories created and configured${NC}"
echo ""

echo -e "${YELLOW}Step 5: Starting PgBouncer service...${NC}"
systemctl start pgbouncer
if systemctl is-active --quiet pgbouncer; then
    echo -e "${GREEN}✓ PgBouncer started successfully${NC}"
else
    echo -e "${RED}✗ Failed to start PgBouncer${NC}"
    exit 1
fi
echo ""

echo -e "${YELLOW}Step 6: Enabling PgBouncer on boot...${NC}"
systemctl enable pgbouncer
echo -e "${GREEN}✓ PgBouncer enabled for auto-start${NC}"
echo ""

echo -e "${YELLOW}Step 7: Verifying PgBouncer connectivity...${NC}"
sleep 2
if netstat -tlnp 2>/dev/null | grep -q ":$PGBOUNCER_PORT"; then
    echo -e "${GREEN}✓ PgBouncer listening on port $PGBOUNCER_PORT${NC}"
else
    echo -e "${RED}✗ PgBouncer not listening on port $PGBOUNCER_PORT${NC}"
    exit 1
fi
echo ""

echo "════════════════════════════════════════════════════════════"
echo -e "${GREEN}✅ PgBouncer Deployment Complete!${NC}"
echo "════════════════════════════════════════════════════════════"
echo ""
echo "📊 Configuration Summary:"
echo "  PgBouncer Port:     $PGBOUNCER_PORT"
echo "  Database:           $DB_NAME"
echo "  Pool Size:          25 connections"
echo "  Max Clients:        1000"
echo "  Pool Mode:          Transaction"
echo ""
echo "🔧 Next Steps:"
echo "  1. Update your game server to connect to localhost:$PGBOUNCER_PORT"
echo "  2. Restart the game server"
echo "  3. Monitor with: tail -f /var/log/pgbouncer/pgbouncer.log"
echo "  4. Run stress test: python3 ai_player/spawn.py --bot-dir ai_player --amount 100"
echo ""
echo "📈 Monitoring Commands:"
echo "  psql -h 127.0.0.1 -p $PGBOUNCER_PORT -U postgres -d pgbouncer -c \"SHOW POOLS;\""
echo "  psql -h 127.0.0.1 -p $PGBOUNCER_PORT -U postgres -d pgbouncer -c \"SHOW STATS;\""
echo ""
