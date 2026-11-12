#!/bin/sh
set -eu
umask 077
cp /run/secrets/app_pgpass /tmp/app.pgpass
chmod 0600 /tmp/app.pgpass
cp /app/bin/bigbang.json /tmp/bigbang.json
chmod 0400 /tmp/bigbang.json
cd /tmp
exec /app/bin/server
