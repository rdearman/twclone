#!/bin/sh
set -eu

# Install as /etc/letsencrypt/renewal-hooks/deploy/twclone-beta-game.
# Certbot calls deploy hooks only after a successful renewal. The game server
# loads its OpenSSL context at startup, so restart the service to adopt the new
# certificate. Never copy the private key into a world-readable location.

EXPECTED_LINEAGE=/etc/letsencrypt/live/game.twclone.impara.co.uk
SECRETS_DIR=/etc/twclone-beta/secrets
SERVICE=twclone-beta.service
SERVICE_GID=10001

[ "${RENEWED_LINEAGE:-}" = "$EXPECTED_LINEAGE" ] || exit 0
install -d -o root -g root -m 0700 "$SECRETS_DIR"
install -o root -g root -m 0644 "$RENEWED_LINEAGE/fullchain.pem" "$SECRETS_DIR/game.crt.new"
install -o root -g "$SERVICE_GID" -m 0640 "$RENEWED_LINEAGE/privkey.pem" "$SECRETS_DIR/game.key.new"
mv -f "$SECRETS_DIR/game.crt.new" "$SECRETS_DIR/game.crt"
mv -f "$SECRETS_DIR/game.key.new" "$SECRETS_DIR/game.key"
systemctl restart "$SERVICE"
