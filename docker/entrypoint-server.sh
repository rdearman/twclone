#!/bin/sh
set -eu
umask 077
cp /run/secrets/app_pgpass /tmp/app.pgpass
chmod 0600 /tmp/app.pgpass
runtime_dir=$(mktemp -d /tmp/twclone-runtime.XXXXXX)
chmod 0700 "$runtime_dir"
cp /app/bin/bigbang.json "$runtime_dir/bigbang.json"
chmod 0400 "$runtime_dir/bigbang.json"
cd "$runtime_dir"
/usr/bin/python3 /app/bin/feedback_github_publisher.py &
exec /app/bin/server
