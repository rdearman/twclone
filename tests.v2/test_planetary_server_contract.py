import json
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class PlanetaryServerContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.planets = (ROOT / "src/server_planets.c").read_text(encoding="utf-8")
        cls.citadel = (ROOT / "src/server_citadel.c").read_text(encoding="utf-8")
        cls.combat = (ROOT / "src/server_combat.c").read_text(encoding="utf-8")
        cls.loop = (ROOT / "src/server_loop.c").read_text(encoding="utf-8")
        cls.universe = (ROOT / "src/server_universe.c").read_text(encoding="utf-8")
        cls.universe_repo = (ROOT / "src/db/repo/repo_universe.c").read_text(
            encoding="utf-8"
        )
        cls.ports_repo = (ROOT / "src/db/repo/repo_ports.c").read_text(
            encoding="utf-8"
        )
        cls.db_api = (ROOT / "src/db/db_api.c").read_text(encoding="utf-8")
        cls.market_repo = (ROOT / "src/db/repo/repo_market.c").read_text(encoding="utf-8")
        cls.cron = (ROOT / "src/server_cron.c").read_text(encoding="utf-8")
        cls.cron_repo = (ROOT / "src/db/repo/repo_cron.c").read_text(encoding="utf-8")
        cls.corp_repo = (ROOT / "src/db/repo/repo_corporation.c").read_text(encoding="utf-8")
        cls.planet_repo = (ROOT / "src/db/repo/repo_planets.c").read_text(encoding="utf-8")
        cls.ports = (ROOT / "src/server_ports.c").read_text(encoding="utf-8")
        cls.cargo_repo = (ROOT / "src/db/repo/repo_cargo.c").read_text(encoding="utf-8")
        cls.citadel_repo = (ROOT / "src/db/repo/repo_citadel.c").read_text(encoding="utf-8")
        cls.protocol = (ROOT / "docs/PROTOCOL.v3/24_Planets_Outposts_Stations.md").read_text(
            encoding="utf-8"
        )
        cls.suite = json.loads(
            (ROOT / "tests.v2/suite_planet_fighter_transfers.json").read_text(
                encoding="utf-8"
            )
        )
        cls.landing_suite = json.loads(
            (ROOT / "tests.v2/suite_planet_landing_defense.json").read_text(
                encoding="utf-8"
            )
        )

    def test_fighter_transfer_is_registered_and_uses_atomic_repository(self):
        self.assertIn('"planet.fighters.transfer", cmd_planet_fighters_transfer', self.loop)
        handler = self.planets.split("cmd_planet_fighters_transfer", 1)[1].split(
            "cmd_planet_rename", 1
        )[0]
        self.assertIn("repo_planets_transfer_fighters", handler)
        self.assertNotIn("ERR_NOT_IMPLEMENTED", handler)
        self.assertIn('"ship_to_planet"', handler)
        self.assertIn('"planet_to_ship"', handler)
        self.assertIn("landed_planet != planet_id", handler)
        self.assertIn("You do not control this planet", handler)
        self.assertIn("repo_corp_check_member_role", handler)
        self.assertIn("revalidates ownership, landing, available", handler)

    def test_slave_purchase_requires_evil_alignment_independent_of_neutral_policy(self):
        buy = self.ports.split("cmd_trade_buy", 1)[1].split(
            "cmd_", 1
        )[0]
        self.assertIn('strcasecmp (commodity, "SLV") == 0', buy)
        self.assertIn("h_player_is_evil_aligned (db, ctx->player_id)", buy)
        self.assertLess(
            buy.index("h_player_is_evil_aligned"),
            buy.index("h_can_trade_commodity"),
        )
        self.assertEqual(buy.count("h_player_is_evil_aligned"), 2)
        settled_slave_guard = buy.rindex("h_player_is_evil_aligned")
        self.assertGreater(settled_slave_guard, buy.index("if (!db_tx_begin"))
        self.assertLess(settled_slave_guard, buy.index("h_market_move_port_stock"))
        self.assertIn("db_alignment_band_for_value", self.ports)

    def test_ferengi_retaliation_persistence_failure_aborts_combat(self):
        attack = self.combat.split("int\nhandle_ship_attack (", 1)[1].split(
            "\nint\ncmd_combat_attack", 1
        )[0]
        retaliation = attack.index("ferengi_handle_hostile_attack")
        rollback = attack.index("db_tx_rollback", retaliation)
        response = attack.index("Could not persist Ferengi retaliation state", rollback)
        damage = attack.index("apply_combat_damage", retaliation)
        self.assertLess(rollback, response)
        self.assertLess(response, damage)
        handler = self.universe.split("ferengi_handle_hostile_attack", 1)[1].split(
            "void\niss_init", 1
        )[0]
        self.assertIn("captain_rc < 0", handler)
        self.assertIn('"ferengi_hunt"', handler)
        self.assertIn("if (repo_npc_intel_upsert", handler)
        self.assertIn('repo_npc_action_complete (db, action_key, "completed"', handler)

    def test_terra_enforcement_uses_shared_destruction_without_asset_seizure(self):
        terra = self.planets.split("/* Terra protection", 1)[1].split(
            "/* Get attacker ship fighters */", 1
        )[0]
        self.assertIn(
            "destroy_ship_and_handle_side_effects (ctx, ctx->player_id) != 0",
            terra,
        )
        self.assertIn("Could not settle ship destruction", terra)
        self.assertIn('"escape_pod_spawned", json_true ()', terra)
        self.assertNotIn("h_apply_terra_sanctions", self.planets)
        self.assertNotIn("seized your assets", terra)

    def test_orion_blockader_holds_home_and_uses_existing_defence_systems(self):
        blockader = self.universe.split(
            "if (personality == SHIP_PERSONALITY_BLOCKADER)", 1
        )[1].split("if (personality == SHIP_PERSONALITY_OFFENSIVE", 1)[0]
        self.assertIn("new_target = (ori_home_sector_id > 0)", blockader)
        self.assertIn("current_sector == ori_home_sector_id", blockader)
        self.assertIn("npc_deploy_defensive_fighter", blockader)
        self.assertIn("npc_deploy_defensive_mine", blockader)
        self.assertIn("repo_universe_ship_is_orion_owned", blockader)
        self.assertIn('"orion:blockade:attack:', blockader)

    def test_lethal_npc_player_attack_uses_universal_destruction_path(self):
        npc_attack = self.combat.split(
            "npc_combat_attack_ship (", 1
        )[1].split("\nint\ncmd_combat_status", 1)[0]
        self.assertIn("target.hull <= 0 && target.player_id > 0", npc_attack)
        self.assertIn("handle_ship_destruction (db, &kill_ctx)", npc_attack)
        self.assertIn("KILL_CAUSE_COMBAT", npc_attack)
        self.assertLess(
            npc_attack.index("handle_ship_destruction"),
            npc_attack.index("h_get_ship_cargo_and_holds"),
        )

    def test_lethal_pvp_attack_pods_player_but_not_npc_owned_ship(self):
        attack = self.combat.split("int\nhandle_ship_attack (", 1)[1].split(
            "\nint\ncmd_combat_attack", 1
        )[0]
        self.assertIn("repo_players_is_npc", attack)
        self.assertIn("def.hull <= 0 && def.player_id > 0 && !defender_is_npc", attack)
        self.assertIn("handle_ship_destruction (db, &kill_ctx)", attack)
        self.assertIn("KILL_CAUSE_COMBAT", attack)

    def test_treasury_is_not_authorized_by_landing_alone(self):
        info = self.planets.split("cmd_planet_info", 1)[1].split(
            "cmd_planet_fighters_transfer", 1
        )[0]
        self.assertIn("bool treasury_authorized = false", info)
        self.assertIn("if (treasury_authorized && citadel_exists)", info)
        self.assertIn("db_planets_get_treasury", info)
        self.assertIn("repo_corp_check_member_role", info)

    def test_planet_info_returns_qcannon_settings_from_repository(self):
        info = self.planets.split("cmd_planet_info", 1)[1].split(
            "cmd_planet_fighters_transfer", 1
        )[0]
        citadel_repo = (ROOT / "src/db/repo/repo_citadel.c").read_text(
            encoding="utf-8"
        )
        self.assertIn("repo_citadel_get_qcannon_state", info)
        self.assertIn('"qCannonSector"', info)
        self.assertIn('"qCannonAtmosphere"', info)
        self.assertIn("qcannon_rc != ERR_DB_NOT_FOUND", info)
        self.assertIn("COALESCE(qCannonSector, 0)", citadel_repo)
        self.assertIn("COALESCE(qCannonAtmosphere, 0)", citadel_repo)

    def test_landing_defense_fires_before_denial_and_reports_persistence_failure(self):
        land = self.planets.split("cmd_planet_land", 1)[1].split(
            "cmd_planet_launch", 1
        )[0]
        self.assertLess(
            land.index("h_trigger_atmosphere_quasar"),
            land.index("if (!can_land)"),
        )
        self.assertIn("defense_rc < 0", land)
        self.assertIn("defense_rc == 2", land)
        self.assertIn("resolve_landing_fighter_defense", land)
        self.assertIn("send_response_error_data", land)
        self.assertIn('"combat.planet_landing_defense.v1"', self.planets)
        self.assertIn('"combat.hit.quasar"', self.combat)

    def test_landing_validates_before_turn_and_commits_turn_with_landing(self):
        land = self.planets.split("cmd_planet_land", 1)[1].split(
            "cmd_planet_launch", 1
        )[0]
        self.assertIn(
            'json_unpack (data, "{s:i}", "planet_id", &planet_id) != 0',
            land,
        )
        self.assertLess(
            land.index("Missing or invalid 'planet_id'."),
            land.index("db_player_get_sector"),
        )
        # A failed authoritative sector read must fail closed before any
        # landing/turn decision; it must not be interpreted as sector 0.
        self.assertIn("db_player_get_sector (db, ctx->player_id, &player_sector) != 0", land)
        self.assertLess(
            land.index("Could not determine your current sector."),
            land.index("db_planets_get_land_info"),
        )
        self.assertLess(land.index("if (player_sector != planet_sector)"),
                        land.index("h_consume_player_turn"))
        self.assertLess(land.index("if (!can_land)"),
                        land.index("h_consume_player_turn"))
        # Jurisdiction and alignment refusals are also pre-charge checks.
        self.assertLess(land.index("ERR_BANNED_FROM_JURISDICTION"),
                        land.index("h_consume_player_turn"))
        self.assertLess(land.index("REASON_EVIL_ALIGN"),
                        land.index("h_consume_player_turn"))
        self.assertIn("repo_clusters_get_cluster_for_sector(db, planet_sector, &cluster_id)", land)
        self.assertIn("Could not determine jurisdiction for landing.", land)
        self.assertIn("repo_clusters_get_player_banned(db, cluster_id,", land)
        self.assertIn("Could not determine landing permissions.", land)
        self.assertIn("db_player_get_alignment (db, ctx->player_id, &alignment) != 0", land)
        self.assertIn("Could not determine alignment for landing.", land)
        # Jurisdiction and Earth-alignment refusals must precede hostile
        # defense resolution, otherwise a refusal can mutate defenses or ship
        # state and emit a combat event.
        self.assertLess(
            land.index("Could not determine jurisdiction for landing."),
            land.index("h_trigger_atmosphere_quasar_report"),
        )
        self.assertLess(
            land.index("REASON_EVIL_ALIGN"),
            land.index("h_trigger_atmosphere_quasar_report"),
        )
        self.assertIn("db_tx_begin (db, DB_TX_IMMEDIATE", land)
        self.assertIn("h_get_active_ship_id (db, ctx->player_id) <= 0", land)
        self.assertIn("tx_ship_id = h_get_active_ship_id (db, ctx->player_id)", land)
        self.assertIn("Could not revalidate landing state.", land)
        self.assertIn("tx_player_sector", land)
        self.assertIn("tx_can_land", land)
        self.assertIn("db_tx_rollback (db, NULL)", land)
        self.assertIn("db_tx_commit (db, &txerr)", land)
        # Policy changes must not race the turn-consuming write after the
        # transaction has revalidated location and ownership.
        self.assertIn("tx_cluster_id", land)
        self.assertIn("repo_clusters_get_player_banned (db, tx_cluster_id", land)
        self.assertIn("db_player_get_alignment (db, ctx->player_id, &tx_alignment)", land)
        self.assertIn("tx_alignment < 0 && tx_planet_sector == 1", land)
        self.assertLess(land.index("Could not revalidate landing permissions."),
                        land.index("h_consume_player_turn"))

    def test_planet_land_schema_is_strict_and_positive_only(self):
        # The C schema is the dispatch authority; keep malformed payloads out
        # before cmd_planet_land can reach any database or turn path.
        schema_c = (ROOT / "src/schemas.c").read_text(encoding="utf-8")
        land_schema = schema_c.split("schema_planet_land (void)\n{", 1)[1].split(
            "schema_planet_launch", 1
        )[0]
        self.assertIn('json_array_append_new (required, json_string ("planet_id"))', land_schema)
        self.assertIn('json_object_set_new (root, "additionalProperties", json_boolean (0))', land_schema)
        self.assertIn('json_object_set_new (planet_id_prop, "minimum", json_integer (1))', land_schema)

    def test_landing_fixture_requires_state_snapshots_for_refusals_and_success(self):
        names = [test["name"] for test in self.landing_suite["tests"]]
        required = {
            "Evidence snapshot before owner landing",
            "Evidence snapshot after owner landing",
            "Reject owner landing with zero turns",
            "Verify zero-turn landing preserved state",
            "Reject malformed owner landing without mutation",
            "Undefended outsider landing is refused without damage",
            "Verify undefended ship unchanged and not landed",
            "Planet owner bypasses its armed Quasar defense",
        }
        self.assertTrue(required.issubset(names))

        # A fixture assertion is evidence intent only; it is not a live-run
        # result. Require the immutable fields that prove turn/landing state.
        snapshots = [
            test for test in self.landing_suite["tests"]
            if "snapshot" in test["name"].lower()
        ]
        self.assertTrue(snapshots)
        for snapshot in snapshots:
            sql = snapshot.get("data", {}).get("sql", "")
            self.assertIn("turns_remaining", sql)
            self.assertIn("lastplanet", sql)
            self.assertIn("onplanet", sql)

    def test_landing_transaction_rolls_back_failures_before_context_refresh(self):
        land = self.planets.split("cmd_planet_land", 1)[1].split(
            "cmd_planet_launch", 1
        )[0]

        begin = land.index("db_tx_begin (db, DB_TX_IMMEDIATE")
        consume = land.index("h_consume_player_turn (db, ctx, 1)")
        persist = land.index("db_player_land_on_planet (db, ctx->player_id, planet_id)")
        event = land.index('db_log_engine_event ((long long) time (NULL), "planet.land.v1"')
        commit = land.index("db_tx_commit (db, &txerr)")
        context_refresh = land.index("ctx->sector_id = 0")

        self.assertLess(begin, consume)
        self.assertLess(land.index("Could not revalidate landing state."), consume)
        self.assertLess(consume, persist)
        self.assertLess(persist, event)
        self.assertLess(event, commit)
        self.assertLess(commit, context_refresh)

        # Every post-charge failure path must roll back before returning.
        turn_failure = land.index("if (tc != TURN_CONSUME_SUCCESS)")
        self.assertLess(turn_failure, land.index("db_tx_rollback (db, NULL)", turn_failure))
        landing_failure = land.index("if (db_player_land_on_planet")
        self.assertLess(landing_failure, land.index("db_tx_rollback (db, NULL)", landing_failure))
        event_failure = land.index("if (!landing_event")
        self.assertLess(event_failure, land.index("db_tx_rollback (db, NULL)", event_failure))
        commit_failure = land.index("if (!db_tx_commit")
        self.assertLess(commit_failure, land.index("db_tx_rollback (db, NULL)", commit_failure))

    def test_landing_transaction_starts_only_after_defense_refusal_resolution(self):
        land = self.planets.split("cmd_planet_land", 1)[1].split(
            "cmd_planet_launch", 1
        )[0]

        defense = land.index("h_trigger_atmosphere_quasar_report")
        denial = land.index("if (!can_land)")
        transaction = land.index("db_tx_begin (db, DB_TX_IMMEDIATE")
        consume = land.index("h_consume_player_turn (db, ctx, 1)")

        # Hostile landing defenses may update defensive state, but a refused
        # landing must never enter the turn/landing transaction.  This keeps
        # refusal semantics distinct from the successful landing commit.
        self.assertLess(defense, denial)
        self.assertLess(denial, transaction)
        self.assertLess(transaction, consume)
        self.assertIn("json_decref (defense_results);", land)

    def test_harvest_is_registered_and_moves_only_existing_stock_atomically(self):
        handler = self.planets.split("cmd_planet_harvest", 1)[1].split(
            "cmd_planet_emancipate", 1
        )[0]
        repository = self.planet_repo.split("repo_planets_harvest", 1)[1].split(
            "db_planets_rename", 1
        )[0]
        self.assertIn('"planet.harvest", cmd_planet_harvest', self.loop)
        self.assertNotIn("ERR_NOT_IMPLEMENTED", handler)
        self.assertIn("repo_planets_harvest", handler)
        self.assertIn("turns_charged", handler)
        self.assertIn("repo_corp_lock_member_role", repository)
        self.assertIn('"ORE", "ORG", "EQU"', repository)
        self.assertIn("db_ports_decrement_stock", repository)
        self.assertIn("repo_cargo_add", repository)
        self.assertIn("repo_cargo_sync_legacy_columns", repository)
        self.assertIn("available_holds", repository)
        self.assertIn("db_tx_commit", repository)
        self.assertIn("db_tx_rollback", repository)

    def test_capture_population_choice_is_server_validated_and_atomic(self):
        handler = self.planets.split("cmd_combat_attack_planet", 1)[1].split(
            "h_is_illegal_commodity", 1
        )[0]
        repository = self.planet_repo.split("db_planets_capture", 1)[1].split(
            "db_planets_lose_fighters", 1
        )[0]
        schema_c = (ROOT / "src/schemas.c").read_text(encoding="utf-8")
        capture_schema = schema_c.split("schema_combat_attack_planet (void)", 1)[1].split(
            "schema_bank_balance", 1
        )[0]
        self.assertIn('"enslave_colonists"', capture_schema)
        self.assertIn("db_alignment_band_for_value", handler)
        self.assertIn("is_evil && strcmp (population_type, \"COLONIST\")", handler)
        self.assertIn("liberate_slaves", handler)
        self.assertIn("db_tx_rollback", handler)
        self.assertIn("expected_population_type", repository)
        self.assertIn("expected_owner_id", repository)
        self.assertIn("population_type=CASE", repository)
        self.assertIn("slave_unrest_stopped=CASE", repository)

    def test_planet_view_production_uses_scheduler_formula_and_slave_policy(self):
        view = self.planets.split("cmd_planet_view", 1)[1].split(
            "cmd_planet_info", 1
        )[0]
        production = self.cron_repo.split(
            "db_cron_planet_get_production_overview_json", 1
        )[1]
        self.assertIn("db_cron_planet_get_production_overview_json", view)
        self.assertIn("WHEN 'ORG' THEN COALESCE(p.colonists_org,0)*COALESCE(pt.organicsProduction,0)", production)
        self.assertIn("WHEN 'EQU' THEN COALESCE(p.colonists_eq,0)*COALESCE(pt.equipmentProduction,0)", production)
        self.assertIn('strcmp (code, "ORE") == 0', production)
        self.assertIn('strcmp (code, "ORG") == 0', production)
        self.assertIn('strcmp (code, "EQU") == 0', production)
        self.assertIn("normal_output / 2", production)
        self.assertIn("unrest_stopped", production)

    def test_slave_deposit_uses_real_cargo_and_prevents_mixed_populations(self):
        deposit = self.planets.split("cmd_planet_deposit", 1)[1].split(
            "cmd_planet_withdraw", 1
        )[0]
        repository = self.planet_repo.split(
            "repo_planets_deposit_slaves", 1
        )[1].split("db_planets_rename", 1)[0]
        self.assertIn("repo_planets_deposit_slaves", deposit)
        self.assertIn('strcasecmp (commodity, "SLV")', deposit)
        self.assertIn("alignment_band ab", repository)
        self.assertIn('population_type IN (\'SLAVE\',\'UNPOPULATED\')', repository)
        self.assertIn("repo_cargo_add", repository)
        self.assertIn('"SLV", -quantity', repository)
        self.assertIn("colonists_unassigned=COALESCE(colonists_unassigned,0)+{2}", repository)
        self.assertIn("sql_for_update_of (db, \"p, pl, s\"", repository)
        self.assertIn("db_tx_commit", repository)
        self.assertIn("db_tx_rollback", repository)
    def test_citadel_lookup_does_not_treat_query_failure_as_no_defense(self):
        self.assertIn("load_landing_citadel_defenses", self.planets)
        self.assertIn("rc == ERR_DB_NOT_FOUND", self.planets)
        self.assertIn("db_planets_get_citadel_defenses_ex", self.planets)
        self.assertIn("db_planets_consume_citadel_shields", self.planets)
        self.assertNotIn("db_planets_update_citadel_shields (db, planet_id,\n                                                shields_after)", self.planets)
        self.assertIn("state_rc == ERR_DB_NOT_FOUND", self.combat)
        self.assertIn("if (state_rc != 0)", self.combat)

    def test_server_defense_and_planet_transwarp_checks_match_manual_level_gates(self):
        # The original TW2002 manual assigns combat control to L2, shields to
        # L5, interdictor operation to L6, and Planetary TransWarp to L4.
        land_defense = self.planets.split(
            "resolve_landing_fighter_defense", 1
        )[1].split("cmd_planet_land", 1)[0]
        self.assertIn("citadel_level >= 2", land_defense)
        self.assertIn("citadel_level >= 5", land_defense)

        planet_transwarp = self.planets.split("cmd_planet_transwarp", 1)[1].split(
            "cmd_planet_colonists_set", 1
        )[0]
        self.assertIn("if (level < 4)", planet_transwarp)

        self.assertIn("c.level >= 6", self.universe_repo)
        interdiction = self.universe.split("h_check_interdiction (", 1)[1].split(
            "\nint\n", 1
        )[0]
        self.assertIn("repo_universe_get_interdictors", interdiction)

    def test_protocol_and_integration_suite_cover_transfer_and_visibility(self):
        self.assertIn("repo_planets_transfer_fighters(", self.protocol)
        names = [test["name"] for test in self.suite["tests"]]
        self.assertIn("Transfer fighters from ship to planet", names)
        self.assertIn("Transfer fighters from planet to ship", names)
        self.assertIn("Landed visitor does not receive treasury balance", names)
        self.assertIn("Reject transfer exceeding available ship fighters", names)

    def test_landing_regressions_cover_authorized_hostile_unguarded_and_lethal(self):
        names = [test["name"] for test in self.landing_suite["tests"]]
        for required in (
            "Planet owner bypasses its armed Quasar defense",
            "Authorized corporation member bypasses armed defense",
            "Undefended outsider landing is refused without damage",
            "Quasar damage consumes shields before hull",
            "Planetary fighters engage outsider; access remains denied",
            "Lethal Quasar defense destroys attacking ship",
        ):
            self.assertIn(required, names)

    def test_landing_suite_covers_fedspace_refusal_and_non_fedspace_combat(self):
        names = [test["name"] for test in self.landing_suite["tests"]]
        self.assertIn("FedSpace refusal prevents planetary defense engagement", names)
        self.assertIn("Verify protected-sector attempt caused no damage or defense event", names)
        fixture = next(
            test for test in self.landing_suite["tests"]
            if test.get("name") == "Create owned and defended planets"
        )
        self.assertIn("977712,20,", fixture["data"]["sql"])
        self.assertIn("protected_sector", self.planets)
        self.assertIn("ERR_DB_NOT_FOUND", self.planets)

    def test_planet_market_sale_is_atomic_and_stock_decrement_is_guarded(self):
        handler = self.planets.split("cmd_planet_market_sell", 1)[1].split(
            "cmd_planet_market_buy_order", 1
        )[0]
        self.assertLess(handler.index("db_tx_begin"),
                        handler.index("db_ports_decrement_stock"))
        self.assertLess(handler.index("db_ports_decrement_stock"),
                        handler.index("h_add_player_petty_cash"))
        self.assertIn("db_tx_rollback", handler)
        self.assertIn("db_tx_commit", handler)
        self.assertIn("Failed to credit sale proceeds.", handler)
        self.assertIn("AND quantity >= {1}", self.ports_repo)
        self.assertIn("db_exec_rows_affected", self.ports_repo)

    def test_immediate_planet_purchase_checks_every_settlement_leg(self):
        handler = self.planets.split("cmd_planet_market_buy_order", 1)[1].split(
            "cmd_planet_colonists_set", 1
        )[0]
        immediate = handler.split("bool illegal_commodity", 1)[1].split(
            "long long worst_case_cost", 1
        )[0]
        for operation in (
            "db_ports_decrement_stock",
            "h_update_ship_cargo",
            "h_deduct_player_petty_cash_unlocked",
            "db_planets_add_treasury_buy",
        ):
            self.assertIn(operation, immediate)
        self.assertIn("settlement_rc != 0", immediate)
        self.assertIn("db_tx_rollback", immediate)
        self.assertIn("Purchase settlement failed.", immediate)

    def test_planet_market_uses_capacity_only_for_orders_and_stock_for_withdrawals(self):
        handler = self.planets.split("cmd_planet_market_buy_order", 1)[1].split(
            "cmd_planet_colonists_set", 1
        )[0]
        self.assertIn("bool illegal_commodity = h_is_illegal_commodity", handler)
        self.assertIn("illegal_commodity && capacity_stock < quantity_total", handler)
        self.assertIn("!illegal_commodity", handler)
        self.assertIn("quantity_total > planet_capacity)", handler)
        self.assertNotIn("worst_case_cost", handler)
        self.assertIn("planet_market_buy_order_expiry_seconds", handler)
        self.assertIn("if (illegal_commodity)", handler)

    def test_failed_backend_commit_keeps_transaction_rollback_available(self):
        commit = self.db_api.split("bool db_tx_commit", 1)[1].split(
            "bool db_tx_rollback", 1
        )[0]
        self.assertLess(commit.index("db->vt->tx_commit(db, err)"),
                        commit.index("db->tx_nest_level = 0"))
        self.assertIn("if (!db->vt->tx_commit(db, err))", commit)

    def test_corporate_market_authority_is_revalidated_under_row_locks(self):
        for command in ("cmd_planet_market_sell", "cmd_planet_market_buy_order"):
            handler = self.planets.split(command, 1)[1].split("\nint\n", 1)[0]
            self.assertIn("h_planet_market_lock_control", handler)
        self.assertIn("SELECT owner_id, owner_type FROM planets WHERE planet_id = {1} FOR UPDATE",
                      self.planet_repo)
        self.assertIn("SELECT role FROM corp_members WHERE corporation_id = {1} AND player_id = {2} FOR UPDATE",
                      self.corp_repo)

    def test_buy_orders_are_unreserved_and_commit_with_expiry(self):
        handler = self.planets.split("cmd_planet_market_buy_order", 1)[1].split(
            "\nint\n", 1
        )[0]
        self.assertIn("db_tx_begin (db, DB_TX_IMMEDIATE", handler)
        self.assertIn("db_planets_get_market_move_info (db, planet_id, commodity_code,",
                      handler)
        self.assertIn("quantity_total > fresh_capacity", handler)
        self.assertNotIn("worst_case_cost", handler)
        self.assertIn("expires_at, &order_id", handler)
        self.assertIn('"expires_at", json_integer (expires_at)', handler)
        self.assertIn("db_tx_commit (db, &txerr)", handler)

    def test_explicit_planet_buy_order_cancel_is_authorized_and_atomic(self):
        handler = self.planets.split("cmd_planet_market_buy_order_cancel", 1)[1].split(
            "\nint\n", 1
        )[0]
        self.assertIn("db_tx_begin (db, DB_TX_IMMEDIATE", handler)
        self.assertIn("creator_id != ctx->player_id", handler)
        self.assertIn("h_planet_market_lock_control", handler)
        self.assertIn("db_market_lock_planet_buy_order", handler)
        self.assertIn('"player_requested"', handler)
        self.assertIn("db_tx_commit (db, &txerr)", handler)
        self.assertIn('"remaining_quantity"', handler)
        self.assertIn('"planet.market.buy_order.cancel"', self.loop)

    def test_buy_order_expiry_config_is_bounded_and_expiry_is_persisted(self):
        config = (ROOT / "src/server_config.c").read_text(encoding="utf-8")
        sysop = (ROOT / "src/server_sysop.c").read_text(encoding="utf-8")
        self.assertIn("g_cfg.planet_market_buy_order_expiry_seconds = 86400", config)
        self.assertIn("expiry >= 60 && expiry <= 604800", config)
        self.assertIn("expiry < 60 || expiry > 604800", sysop)
        self.assertIn("expires_at <= {1}", self.cron_repo)
        self.assertIn("cancellation_reason='expired'", self.cron_repo)

    def test_market_matching_locks_current_capacity_and_buyer_balance(self):
        settlement = self.cron.split("h_daily_market_settlement (", 1)[1]
        self.assertIn("market_lock_inventory_location", settlement)
        self.assertIn("repo_bank_get_balance_by_account_id_for_update", settlement)
        self.assertIn("buyer_balance / trade_price", settlement)
        self.assertIn('"cancelled"', settlement)
        self.assertIn('"no_capacity"', settlement)
        self.assertIn('"insufficient_credits"', settlement)
        self.assertIn("AND (expires_at IS NULL OR expires_at > {3})", self.market_repo)

    def test_partial_market_fills_remain_open_and_failed_commit_restores_match_state(self):
        settlement = self.cron.split("h_daily_market_settlement (", 1)[1]
        self.assertIn('? "filled" :', settlement)
        self.assertIn('? "cancelled" : "open"', settlement)
        self.assertIn('"open";', settlement)
        self.assertIn('"cancelled"', settlement)
        self.assertIn("if (!db_tx_commit (db, &err))", settlement)
        self.assertIn("buy->filled_quantity = prior_buy_filled", settlement)
        self.assertIn("sell->filled_quantity = prior_sell_filled", settlement)

    def test_market_settlement_uses_order_location_not_account_actor_for_inventory(self):
        loader = self.market_repo.split("db_load_open_orders_for_commodity", 1)[1]
        self.assertIn("location_type, location_id", loader)
        self.assertIn("o->location_id = (int) db_res_col_i64 (res, 4, &err)", loader)
        self.assertIn("sell->location_type", self.cron)
        self.assertIn("buy->location_id", self.cron)
        trade_insert = self.market_repo.split("db_insert_commodity_trade (", 1)[1]
        self.assertIn("buyer_location_type", trade_insert)
        self.assertIn("db_bind_i64 (buyer_location_id)", trade_insert)

    def test_cancel_and_expiry_release_open_partial_orders_without_escrow(self):
        for query in ("status = 'open'",):
            self.assertIn(query, self.market_repo)
        self.assertIn("status='expired', cancellation_reason='expired'", self.cron_repo)
        schema = (ROOT / "sql/pg/000_tables.sql").read_text(encoding="utf-8")
        order_schema = schema.split("CREATE TABLE commodity_orders", 1)[1].split(
            "CREATE TABLE commodity_trades", 1
        )[0]
        self.assertIn("'open', 'filled', 'cancelled', 'expired'", order_schema)
        self.assertNotIn("reserved_credits", order_schema)

    def test_citadel_upgrade_rechecks_planet_control_inside_resource_transaction(self):
        handler = self.citadel.split("cmd_citadel_upgrade (", 1)[1]
        transaction = handler.split("// Deduct resources", 1)[0]
        self.assertIn("db_tx_begin (db, DB_TX_IMMEDIATE", transaction)
        self.assertIn("db_planets_get_owner_info_for_update", transaction)
        self.assertIn("repo_corp_lock_member_role", transaction)
        self.assertLess(
            transaction.index("db_planets_get_owner_info_for_update"),
            handler.index("repo_citadel_deduct_resources"),
        )
        self.assertIn("db_tx_rollback (db, NULL)", transaction)

    def test_citadel_reaper_rechecks_due_upgrade_under_lock_and_checks_commit(self):
        reaper = self.citadel_repo.split("repo_citadel_reap_upgrades(", 1)[1]
        self.assertIn("SELECT target_level FROM citadels", reaper)
        self.assertIn("construction_end_time <= {2}%s", reaper)
        self.assertIn("sql_for_update(db)", reaper)
        self.assertIn("AND target_level = {3}", reaper)
        self.assertIn("db_exec_rows_affected", reaper)
        self.assertIn("if (!db_tx_commit(db, &err))", reaper)


if __name__ == "__main__":
    unittest.main()
