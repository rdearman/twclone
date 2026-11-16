FROM debian:bookworm-slim AS build

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    autoconf automake gcc g++ make libtool pkg-config \
    libjansson-dev libpq-dev libssl-dev libreadline-dev libargon2-dev \
    libbsd-dev uuid-dev \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .
RUN autoreconf -fi \
 && ./configure --enable-postgres \
 && make -j2 SANITIZE_FLAGS=

FROM debian:bookworm-slim AS runtime
ENV DEBIAN_FRONTEND=noninteractive
# The runtime image is intentionally non-root and /app is read-only. Keep the
# default server log in a writable, ephemeral location unless deployment config
# explicitly selects a persistent log path.
ENV TWCLONE_LOG_PATH=/tmp/twclone.log
RUN apt-get update && apt-get install -y --no-install-recommends \
    libjansson4 libpq5 libssl3 libreadline8 libargon2-1 libbsd0 \
    postgresql-client python3 python3-psycopg python3-jwt python3-cryptography \
 && rm -rf /var/lib/apt/lists/* \
 && groupadd --system --gid 10001 twclone \
 && useradd --system --uid 10001 --gid 10001 --home-dir /nonexistent \
      --shell /usr/sbin/nologin twclone

WORKDIR /app/bin
COPY --from=build /src/bin/server /app/bin/server
COPY --from=build /src/bin/bigbang /app/bin/bigbang
COPY tools/feedback_github_publisher.py /app/bin/feedback_github_publisher.py
COPY --from=build /src/sql/pg /app/sql/pg
COPY docker/bigbang.beta.json /app/bin/bigbang.json
COPY docker/entrypoint-server.sh /app/entrypoint-server.sh
COPY docker/entrypoint-migrate.sh /app/entrypoint-migrate.sh
COPY docker/grant-runtime-privileges.sql /app/grant-runtime-privileges.sql
RUN chmod 0555 /app/entrypoint-server.sh /app/entrypoint-migrate.sh \
 && chmod -R a-w /app

USER 10001:10001
ENTRYPOINT ["/app/entrypoint-server.sh"]
