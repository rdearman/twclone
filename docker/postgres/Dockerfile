FROM postgres:16-bookworm

# The PostgreSQL entrypoint drops to UID 999 before running init scripts.
# Let that service account read only the runtime-role secret, whose host file
# remains root:10002 mode 0640 instead of being world-readable.
RUN groupadd --system --gid 10002 twclone-secret-reader \
 && usermod --append --groups twclone-secret-reader postgres
