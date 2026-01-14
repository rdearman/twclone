#pragma once

/*
 * Repo umbrella header.
 * Server/business logic should include repo APIs from here (or individual repo_*.h),
 * and must not include db_int.h or call db_exec/db_query directly.
 */

#include "repo_cmd.h"
#include "repo_market.h"
#include "repo_player_settings.h"
#include "repo_database.h"

/* Future repo modules (enable as they become real APIs) */
#include "repo_players.h"
#include "repo_ports.h"
#include "repo_bank.h"
#include "repo_trade.h"
#include "repo_planets.h"
#include "repo_combat.h"
#include "repo_corporation.h"
#include "repo_universe.h"
#include "repo_clusters.h"
#include "repo_cron.h"
#include "repo_auth.h"
#include "repo_communication.h"
#include "repo_ships.h"
#include "repo_news.h"
#include "repo_engine.h"
