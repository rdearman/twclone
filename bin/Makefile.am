

# AM_CFLAGS = -Wall -Wextra
# AM_CFLAGS = -Wall -Wextra -D_GNU_SOURCE -I../src -DDB_BACKEND_PG -ljansson -lpq


# 1. Define Sanitizer Flags
SANITIZE_FLAGS = -fsanitize=address -fsanitize=undefined -fno-omit-frame-pointer -g

# 2. Add the PostgreSQL and MySQL include paths explicitly
# Replace /usr/include/postgresql with the result of 'pg_config --includedir' if different
PG_INC = -I/usr/include/postgresql
MYSQL_INC = -I/usr/include/mysql # Common MySQL include path

# 3. Update AM_CFLAGS
# added $(PG_INC) and $(MYSQL_INC) here
AM_CFLAGS = -Wall -Wextra -Werror=maybe-uninitialized  -D_GNU_SOURCE -I../src -I../src/db -I../src/db/repo $(PG_INC) $(MYSQL_INC) -DDB_BACKEND_PG -ljansson -lpq $(SANITIZE_FLAGS)

# 4. Linker flags
AM_LDFLAGS = $(SANITIZE_FLAGS)


server_LDADD   	= -ljansson -lcrypto -lpthread -lm -lpq -lreadline -lhistory
AUTOMAKE_OPTIONS = subdir-objects

bin_PROGRAMS = \
	server \
	bigbang


# BigBang sources (PostgreSQL Only)
bigbang_SOURCES = \
        ../src/bigbang_pg_main.c \
        ../src/db/db_api.c \
        ../src/db/sql_driver.c \
        ../src/db/pg/db_pg.c \
        ../src/db/mysql/db_mysql.c \
        ../src/common.c \
        ../src/server_log.c
bigbang_LDFLAGS = -ljansson -lpq
bigbang_LDADD = -ljansson -lpq -lcrypto -lpthread -lm -lreadline -lhistory $(server_LDADD)


# Server sources (now with DB abstraction)
server_SOURCES = \
	../src/common.c \
	../src/db/db_api.c \
	../src/db/sql_driver.c \
	../src/db/pg/db_pg.c \
	../src/db/mysql/db_mysql.c \
	../src/game_db.c \
	../src/db/repo/repo_cmd.c \
	../src/db/repo/repo_cmds.c \
	../src/db/repo/repo_citadel.c \
	../src/db/repo/repo_config.c \
	../src/db/repo/repo_engine_consumer.c \
	../src/db/repo/repo_database.c \
	../src/db/repo/repo_engine.c \
	../src/db/repo/repo_auth.c \
	../src/db/repo/repo_autopilot.c \
	../src/db/repo/repo_bank.c \
	../src/db/repo/repo_market.c \
	../src/db/repo/repo_main.c \
	../src/db/repo/repo_news.c \
	../src/db/repo/repo_player_settings.c \
	../src/db/repo/repo_cron.c \
	../src/db/repo/repo_combat.c \
	../src/db/repo/repo_communication.c \
	../src/db/repo/repo_planets.c \
	../src/db/repo/repo_ports.c \
	../src/db/repo/repo_corporation.c \
	../src/db/repo/repo_players.c \
	../src/db/repo/repo_ships.c \
	../src/db/repo/repo_session.c \
	../src/db/repo/repo_stardock.c \
	../src/db/repo/repo_sysop.c \
	../src/db/repo/repo_universe.c \
	../src/db/repo/repo_clusters.c \
	../src/db/repo/repo_warp.c \
	../src/engine_consumer.c \
	../src/globals.c \
	../src/s2s_keyring.c \
	../src/s2s_transport.c \
	../src/schemas.c \
	../src/server_auth.c \
	../src/server_autopilot.c \
	../src/server_bank.c \
	../src/server_bulk.c \
	../src/server_citadel.c \
	../src/server_clusters.c \
	../src/server_cmds.c \
	../src/server_combat.c  \
	../src/server_communication.c \
	../src/server_config.c \
	../src/server_corporation.c \
	../src/server_cron.c \
	../src/server_engine.c \
	../src/server_envelope.c \
	../src/server_log.c \
	../src/server_loop.c \
	../src/server_main.c \
	../src/server_news.c \
	../src/server_planets.c \
	../src/server_players.c \
	../src/server_police.c \
	../src/server_ports.c \
	../src/server_s2s.c \
	../src/server_ships.c \
	../src/server_stardock.c \
	../src/server_sysop.c \
	../src/server_universe.c \
	../src/server_warp_post_processing.c \
	../src/sysop_interaction.c
