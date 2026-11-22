#!/bin/sh
set -eu

PROJECT=twclone-beta
COMPOSE=/opt/twclone-beta/current/compose.beta.yaml
ENV_FILE=/etc/twclone-beta/.env.beta

docker compose -p "$PROJECT" -f "$COMPOSE" --env-file "$ENV_FILE" up -d db

db_id=
i=0
while [ "$i" -lt 60 ]; do
  db_id=$(docker compose -p "$PROJECT" -f "$COMPOSE" --env-file "$ENV_FILE" ps -q db)
  if [ -n "$db_id" ] && [ "$(docker inspect --format '{{.State.Health.Status}}' "$db_id" 2>/dev/null || true)" = healthy ]; then
    break
  fi
  i=$((i + 1))
  sleep 2
done
if [ -z "$db_id" ] || [ "$(docker inspect --format '{{.State.Health.Status}}' "$db_id" 2>/dev/null || true)" != healthy ]; then
  echo "TWClone beta PostgreSQL did not become healthy" >&2
  exit 1
fi

# BigBang is a destructive, one-time initializer. Never include the migrate
# service in routine boot/restart; the initial deployment runs it explicitly.
docker compose -p "$PROJECT" -f "$COMPOSE" --env-file "$ENV_FILE" up -d --no-deps game

game_id=
i=0
while [ "$i" -lt 60 ]; do
  game_id=$(docker compose -p "$PROJECT" -f "$COMPOSE" --env-file "$ENV_FILE" ps -q game)
  if [ -n "$game_id" ] && [ "$(docker inspect --format '{{.State.Health.Status}}' "$game_id" 2>/dev/null || true)" = healthy ]; then
    exit 0
  fi
  i=$((i + 1))
  sleep 2
done
echo "TWClone beta game server did not become healthy" >&2
exit 1
