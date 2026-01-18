--
-- PostgreSQL database dump
--

\restrict i95pTXBtkPqwpHcJ6MAek6w1UTbEo2SfiJXuUYDoeQ5v26kmcN5YRobkFT0pgFl

-- Dumped from database version 16.11 (Ubuntu 16.11-0ubuntu0.24.04.1)
-- Dumped by pg_dump version 16.11 (Ubuntu 16.11-0ubuntu0.24.04.1)

SET statement_timeout = 0;
SET lock_timeout = 0;
SET idle_in_transaction_session_timeout = 0;
SET client_encoding = 'UTF8';
SET standard_conforming_strings = on;
SELECT pg_catalog.set_config('search_path', '', false);
SET check_function_bodies = false;
SET xmloption = content;
SET client_min_messages = warning;
SET row_security = off;

--
-- Name: public; Type: SCHEMA; Schema: -; Owner: postgres
--

-- *not* creating schema, since initdb creates it


ALTER SCHEMA public OWNER TO postgres;

--
-- Name: SCHEMA public; Type: COMMENT; Schema: -; Owner: postgres
--

COMMENT ON SCHEMA public IS '';


--
-- Name: db_result; Type: TYPE; Schema: public; Owner: postgres
--

CREATE TYPE public.db_result AS (
	code integer,
	message text,
	id bigint
);


ALTER TYPE public.db_result OWNER TO postgres;

--
-- Name: apply_game_defaults(); Type: FUNCTION; Schema: public; Owner: postgres
--

CREATE FUNCTION public.apply_game_defaults() RETURNS void
    LANGUAGE plpgsql
    AS $$
BEGIN
END;
$$;


ALTER FUNCTION public.apply_game_defaults() OWNER TO postgres;

--
-- Name: assert_column_exists(regclass, text); Type: FUNCTION; Schema: public; Owner: postgres
--

CREATE FUNCTION public.assert_column_exists(p_table regclass, p_col text) RETURNS void
    LANGUAGE plpgsql
    AS $$
DECLARE
  v_exists boolean;
BEGIN
  SELECT EXISTS (
    SELECT 1
    FROM pg_attribute a
    WHERE a.attrelid = p_table
      AND a.attname  = p_col
      AND a.attnum   > 0
      AND NOT a.attisdropped
  ) INTO v_exists;

  IF NOT v_exists THEN
    RAISE EXCEPTION 'Required column %.% does not exist.', p_table::text, p_col
      USING ERRCODE = '42703';
  END IF;
END;
$$;


ALTER FUNCTION public.assert_column_exists(p_table regclass, p_col text) OWNER TO postgres;

--
-- Name: assert_table_exists(regclass); Type: FUNCTION; Schema: public; Owner: postgres
--

CREATE FUNCTION public.assert_table_exists(p_table regclass) RETURNS void
    LANGUAGE plpgsql
    AS $$
BEGIN
  -- regclass cast will fail if table missing
  PERFORM 1;
END;
$$;


ALTER FUNCTION public.assert_table_exists(p_table regclass) OWNER TO postgres;

--
-- Name: bank_apply_interest(); Type: FUNCTION; Schema: public; Owner: postgres
--

CREATE FUNCTION public.bank_apply_interest() RETURNS public.db_result
    LANGUAGE plpgsql
    AS $$
BEGIN
  -- TODO: tie to your schema (bank_accounts interest_rate, balance, last_accrued, etc)
  RETURN (0, 'template: implement bank_apply_interest using real columns', NULL);
END;
$$;


ALTER FUNCTION public.bank_apply_interest() OWNER TO postgres;

--
-- Name: bank_process_orders(integer); Type: FUNCTION; Schema: public; Owner: postgres
--

CREATE FUNCTION public.bank_process_orders(batch_limit integer DEFAULT 1000) RETURNS public.db_result
    LANGUAGE plpgsql
    AS $$
BEGIN
  -- TODO: tie to your schema (orders table, tx table, idempotency marker)
  RETURN (0, 'template: implement bank_process_orders using real columns', NULL);
END;
$$;


ALTER FUNCTION public.bank_process_orders(batch_limit integer) OWNER TO postgres;

--
-- Name: bigbang_lock(bigint); Type: FUNCTION; Schema: public; Owner: postgres
--

CREATE FUNCTION public.bigbang_lock(p_key bigint) RETURNS void
    LANGUAGE plpgsql
    AS $$
BEGIN
  PERFORM pg_advisory_lock(p_key);
END;
$$;


ALTER FUNCTION public.bigbang_lock(p_key bigint) OWNER TO postgres;

--
-- Name: bigbang_unlock(bigint); Type: FUNCTION; Schema: public; Owner: postgres
--

CREATE FUNCTION public.bigbang_unlock(p_key bigint) RETURNS void
    LANGUAGE plpgsql
    AS $$
BEGIN
  PERFORM pg_advisory_unlock(p_key);
END;
$$;


ALTER FUNCTION public.bigbang_unlock(p_key bigint) OWNER TO postgres;

--
-- Name: constellation_name(); Type: FUNCTION; Schema: public; Owner: postgres
--

CREATE FUNCTION public.constellation_name() RETURNS text
    LANGUAGE plpgsql
    AS $$
DECLARE
    v_rand int;
    v_max_idx int := 414;
BEGIN
    IF floor(random() * 2)::int = 0 THEN
        v_rand := 1 + floor(random() * v_max_idx)::int;
        RETURN get_constellation_name_entry (v_rand);
    ELSE
        RETURN get_constellation_name_entry (0);
    END IF;
END;
$$;


ALTER FUNCTION public.constellation_name() OWNER TO postgres;

--
-- Name: fn_bank_transactions_after_insert(); Type: FUNCTION; Schema: public; Owner: postgres
--

CREATE FUNCTION public.fn_bank_transactions_after_insert() RETURNS trigger
    LANGUAGE plpgsql
    AS $$
BEGIN
    UPDATE
        bank_accounts
    SET
        balance = CASE NEW.direction
        WHEN 'DEBIT' THEN
            balance - NEW.amount
        WHEN 'CREDIT' THEN
            balance + NEW.amount
        ELSE
            balance
        END
    WHERE
        bank_accounts_id = NEW.account_id;
    -- Update the transaction record with the resulting balance (requires another update in PG unless logic is moved to app layer, keeping as is to match legacy logic)
    UPDATE
        bank_transactions
    SET
        balance_after = (
            SELECT
                balance
            FROM
                bank_accounts
            WHERE
                bank_accounts_id = NEW.account_id)
    WHERE
        bank_transactions_id = NEW.bank_transactions_id;
    RETURN NEW;
END;
$$;


ALTER FUNCTION public.fn_bank_transactions_after_insert() OWNER TO postgres;

--
-- Name: fn_bank_transactions_before_delete(); Type: FUNCTION; Schema: public; Owner: postgres
--

CREATE FUNCTION public.fn_bank_transactions_before_delete() RETURNS trigger
    LANGUAGE plpgsql
    AS $$
BEGIN
    RAISE EXCEPTION 'BANK_LEDGER_APPEND_ONLY';
    RETURN OLD;
END;
$$;


ALTER FUNCTION public.fn_bank_transactions_before_delete() OWNER TO postgres;

--
-- Name: fn_bank_transactions_before_insert(); Type: FUNCTION; Schema: public; Owner: postgres
--

CREATE FUNCTION public.fn_bank_transactions_before_insert() RETURNS trigger
    LANGUAGE plpgsql
    AS $$
DECLARE
    current_bal bigint;
BEGIN
    IF NEW.direction = 'DEBIT' THEN
        SELECT
            balance INTO current_bal
        FROM
            bank_accounts
        WHERE
            bank_accounts_id = NEW.account_id;
        IF (current_bal - NEW.amount) < 0 THEN
            RAISE EXCEPTION 'BANK_INSUFFICIENT_FUNDS';
        END IF;
    END IF;
    RETURN NEW;
END;
$$;


ALTER FUNCTION public.fn_bank_transactions_before_insert() OWNER TO postgres;

--
-- Name: fn_corp_one_leader_guard(); Type: FUNCTION; Schema: public; Owner: postgres
--

CREATE FUNCTION public.fn_corp_one_leader_guard() RETURNS trigger
    LANGUAGE plpgsql
    AS $$
BEGIN
    IF NEW.role = 'Leader' AND EXISTS (
        SELECT
            1
        FROM
            corp_members
        WHERE
            corporation_id = NEW.corporation_id AND ROLE = 'Leader') THEN
        RAISE EXCEPTION 'corp may have only one Leader';
    END IF;
    RETURN NEW;
END;
$$;


ALTER FUNCTION public.fn_corp_one_leader_guard() OWNER TO postgres;

--
-- Name: fn_corp_owner_leader_sync(); Type: FUNCTION; Schema: public; Owner: postgres
--

CREATE FUNCTION public.fn_corp_owner_leader_sync() RETURNS trigger
    LANGUAGE plpgsql
    AS $$
BEGIN
    -- Downgrade old leader
    UPDATE
        corp_members
    SET
        ROLE = 'Officer'
    WHERE
        corporation_id = NEW.corporation_id
        AND ROLE = 'Leader'
        AND player_id <> NEW.owner_id;
    -- Ensure new owner is leader
    INSERT INTO corp_members (corporation_id, player_id, ROLE)
        VALUES (NEW.corporation_id, NEW.owner_id, 'Leader')
    ON CONFLICT (corp_id, player_id)
        DO UPDATE SET ROLE = 'Leader';
    RETURN NEW;
END;
$$;


ALTER FUNCTION public.fn_corp_owner_leader_sync() OWNER TO postgres;

--
-- Name: fn_corp_owner_must_be_member_insert(); Type: FUNCTION; Schema: public; Owner: postgres
--

CREATE FUNCTION public.fn_corp_owner_must_be_member_insert() RETURNS trigger
    LANGUAGE plpgsql
    AS $$
BEGIN
    IF NEW.owner_id IS NOT NULL AND NOT EXISTS (
        SELECT
            1
        FROM
            corp_members
        WHERE
            corporation_id = NEW.corporation_id AND player_id = NEW.owner_id) THEN
        INSERT INTO corp_members (corporation_id, player_id, ROLE)
            VALUES (NEW.corporation_id, NEW.owner_id, 'Leader');
    END IF;
    RETURN NEW;
END;
$$;


ALTER FUNCTION public.fn_corp_owner_must_be_member_insert() OWNER TO postgres;

--
-- Name: fn_corp_tx_after_insert(); Type: FUNCTION; Schema: public; Owner: postgres
--

CREATE FUNCTION public.fn_corp_tx_after_insert() RETURNS trigger
    LANGUAGE plpgsql
    AS $$
BEGIN
    UPDATE
        corp_accounts
    SET
        balance = CASE WHEN NEW.kind IN ('withdraw', 'transfer_out', 'dividend', 'salary') THEN
            balance - NEW.amount
        ELSE
            balance + NEW.amount
        END
    WHERE
        corp_id = NEW.corp_id;
    UPDATE
        corp_tx
    SET
        balance_after = (
            SELECT
                balance
            FROM
                corp_accounts
            WHERE
                corp_id = NEW.corp_id)
    WHERE
        corp_tx_id = NEW.corp_tx_id;
    RETURN NEW;
END;
$$;


ALTER FUNCTION public.fn_corp_tx_after_insert() OWNER TO postgres;

--
-- Name: fn_corp_tx_before_insert(); Type: FUNCTION; Schema: public; Owner: postgres
--

CREATE FUNCTION public.fn_corp_tx_before_insert() RETURNS trigger
    LANGUAGE plpgsql
    AS $$
DECLARE
    current_bal bigint;
BEGIN
    -- Ensure account exists
    INSERT INTO corp_accounts (corp_id, currency, balance, last_interest_at)
        VALUES (NEW.corp_id, COALESCE(NEW.currency, 'CRD'), 0, NULL)
    ON CONFLICT
        DO NOTHING;
    -- Check funds for outgoing types
    IF NEW.kind IN ('withdraw', 'transfer_out', 'dividend', 'salary') THEN
        SELECT
            balance INTO current_bal
        FROM
            corp_accounts
        WHERE
            corp_id = NEW.corp_id;
        IF (current_bal - NEW.amount) < 0 THEN
            RAISE EXCEPTION 'CORP_INSUFFICIENT_FUNDS';
        END IF;
    END IF;
    RETURN NEW;
END;
$$;


ALTER FUNCTION public.fn_corp_tx_before_insert() OWNER TO postgres;

--
-- Name: fn_corporations_touch_updated(); Type: FUNCTION; Schema: public; Owner: postgres
--

CREATE FUNCTION public.fn_corporations_touch_updated() RETURNS trigger
    LANGUAGE plpgsql
    AS $$
BEGIN
    NEW.updated_at := CURRENT_TIMESTAMP;
    RETURN NEW;
END;
$$;


ALTER FUNCTION public.fn_corporations_touch_updated() OWNER TO postgres;

--
-- Name: fn_planets_total_cap_before_insert(); Type: FUNCTION; Schema: public; Owner: postgres
--

CREATE FUNCTION public.fn_planets_total_cap_before_insert() RETURNS trigger
    LANGUAGE plpgsql
    AS $$
DECLARE
    current_count bigint;
    max_allowed bigint;
BEGIN
    -- Allow System (0) to bypass cap during generation
    IF NEW.created_by = 0 THEN
        RETURN NEW;
    END IF;
    SELECT
        count(*) INTO current_count
    FROM
        planets;
    SELECT
        CAST(value AS bigint) INTO max_allowed
    FROM
        config
    WHERE
        key = 'max_total_planets';
    IF current_count >= max_allowed THEN
        RAISE EXCEPTION 'ERR_UNIVERSE_FULL';
    END IF;
    RETURN NEW;
END;
$$;


ALTER FUNCTION public.fn_planets_total_cap_before_insert() OWNER TO postgres;

--
-- Name: fn_ships_ai_set_installed_shields(); Type: FUNCTION; Schema: public; Owner: postgres
--

CREATE FUNCTION public.fn_ships_ai_set_installed_shields() RETURNS trigger
    LANGUAGE plpgsql
    AS $$
BEGIN
    NEW.installed_shields := LEAST (NEW.shields, COALESCE((
            SELECT
                maxshields
            FROM shiptypes st
            WHERE
                st.shiptypes_id = NEW.type_id), NEW.shields));
    RETURN NEW;
END;
$$;


ALTER FUNCTION public.fn_ships_ai_set_installed_shields() OWNER TO postgres;

--
-- Name: generate_clusters(integer); Type: FUNCTION; Schema: public; Owner: postgres
--

CREATE FUNCTION public.generate_clusters(target_count integer DEFAULT 10) RETURNS bigint
    LANGUAGE plpgsql
    AS $$
DECLARE
    v_added bigint := 0;
    v_lock_key bigint := 72002005;
    v_max_sector bigint;
    v_center int;
    v_name text;
    v_align int;
    v_cluster_id int;
BEGIN
    PERFORM
        bigbang_lock (v_lock_key);
    -- Truncate for idempotency
    TRUNCATE cluster_sectors;
    SELECT
        MAX(sector_id) INTO v_max_sector
    FROM
        sectors;
    IF v_max_sector IS NULL THEN
        PERFORM
            bigbang_unlock (v_lock_key);
        RETURN 0;
    END IF;
    -- 1. Federation Core (Fixed)
    INSERT INTO clusters (name, ROLE, kind, center_sector, law_severity, alignment)
        VALUES ('Federation Core', 'FED', 'FACTION', 1, 3, 100)
    ON CONFLICT (name)
        DO UPDATE SET ROLE = EXCLUDED.role, kind = EXCLUDED.kind, center_sector = EXCLUDED.center_sector, law_severity = EXCLUDED.law_severity, alignment = EXCLUDED.alignment;
    -- 2. Random Clusters
    FOR i IN 1.. (target_count - 1)
    LOOP
        v_center := 11 + floor(random() * (v_max_sector - 10));
        v_name := format('Cluster %s', v_center);
        v_align := floor(random() * 200) - 100;
        -- -100 to 100
        INSERT INTO clusters (name, ROLE, kind, center_sector, law_severity, alignment)
            VALUES (v_name, 'RANDOM', 'RANDOM', v_center, 1, v_align)
        ON CONFLICT (name)
            DO UPDATE SET ROLE = EXCLUDED.role, kind = EXCLUDED.kind, center_sector = EXCLUDED.center_sector, law_severity = EXCLUDED.law_severity, alignment = EXCLUDED.alignment;
    END LOOP;
    -- 3. Populate cluster_sectors table
    FOR v_cluster_id,
    v_center IN
    SELECT
        clusters_id,
        center_sector
    FROM
        clusters LOOP
            -- Add center sector_id
            INSERT INTO cluster_sectors (cluster_id, sector_id)
                VALUES (v_cluster_id, v_center)
            ON CONFLICT
                DO NOTHING;
            -- Add adjacent sectors
            INSERT INTO cluster_sectors (cluster_id, sector_id)
            SELECT
                v_cluster_id,
                to_sector
            FROM
                sector_warps
            WHERE
                from_sector = v_center
            ON CONFLICT
                DO NOTHING;
        END LOOP;
    GET DIAGNOSTICS v_added = ROW_COUNT;
    PERFORM
        bigbang_unlock (v_lock_key);
    RETURN v_added;
END;
$$;


ALTER FUNCTION public.generate_clusters(target_count integer) OWNER TO postgres;

--
-- Name: generate_planets(integer); Type: FUNCTION; Schema: public; Owner: postgres
--

CREATE FUNCTION public.generate_planets(target_count integer) RETURNS bigint
    LANGUAGE plpgsql
    AS $$
DECLARE
    v_existing bigint;
    v_to_add bigint;
    v_added bigint := 0;
    v_lock_key bigint := 72002003;
    v_sector_count bigint;
    v_base_num bigint;
    v_ptype bigint;
BEGIN
    IF target_count IS NULL OR target_count < 0 THEN
        RAISE EXCEPTION 'target_count must be >= 0';
    END IF;
    PERFORM
        bigbang_lock (v_lock_key);
    SELECT
        COUNT(*) INTO v_existing
    FROM
        planets;
    v_to_add := GREATEST (target_count - v_existing, 0);
    IF v_to_add = 0 THEN
        PERFORM
            bigbang_unlock (v_lock_key);
        RETURN 0;
    END IF;
    SELECT
        COUNT(*) INTO v_sector_count
    FROM
        sectors;
    IF v_sector_count = 0 THEN
        RAISE EXCEPTION 'Cannot generate planets: no sectors exist';
    END IF;
    SELECT
        COALESCE(MAX(num), 0) + 1 INTO v_base_num
    FROM
        planets;
    SELECT
        planettypes_id INTO v_ptype
    FROM
        planettypes
    ORDER BY
        planettypes_id
    LIMIT 1;
    IF v_ptype IS NULL THEN
        RAISE EXCEPTION 'Cannot generate planets: no planettypes exist';
    END IF;
    INSERT INTO planets (num, sector_id, name, owner_id, owner_type, class, type, created_at, created_by)
    SELECT
        (v_base_num + gs - 1) AS num,
        ((gs - 1) % v_sector_count + 1)::int AS sector_id,
        format('Planet %s', (v_base_num + gs - 1)) AS name,
        0 AS owner_id,
        'none' AS owner_type,
        'M' AS class,
        v_ptype AS type,
        now() AS created_at,
        0 AS created_by
    FROM
        generate_series(1, v_to_add) AS gs;
    GET DIAGNOSTICS v_added = ROW_COUNT;
    PERFORM
        bigbang_unlock (v_lock_key);
    RETURN v_added;
END;
$$;


ALTER FUNCTION public.generate_planets(target_count integer) OWNER TO postgres;

--
-- Name: generate_ports(integer); Type: FUNCTION; Schema: public; Owner: postgres
--

CREATE FUNCTION public.generate_ports(target_sectors integer DEFAULT NULL::integer) RETURNS bigint
    LANGUAGE plpgsql
    AS $$
DECLARE
    v_added bigint := 0;
    v_lock_key bigint := 72002002;
    v_target int;
    v_port_ratio int;
    v_port_rec record;

    v_size int;
    v_cap int;

    v_code_ore text;
    v_code_org text;
    v_code_equ text;

    v_base_ore int;
    v_base_org int;
    v_base_equ int;

BEGIN
    PERFORM bigbang_lock(v_lock_key);

    /* Get config value for port_ratio, default to 40 if not set */
    SELECT value::int INTO v_port_ratio
      FROM config
     WHERE key = 'port_ratio';

    IF v_port_ratio IS NULL OR v_port_ratio > 100 OR v_port_ratio < 0 THEN
        v_port_ratio := 40;
    END IF;

    IF target_sectors IS NULL THEN
        SELECT COALESCE(MAX(sector_id), 0)::int INTO v_target
          FROM sectors;
    ELSE
        v_target := target_sectors;
    END IF;

    IF v_target < 0 THEN
        RAISE EXCEPTION 'target_sectors must be >= 0';
    END IF;

    PERFORM generate_sectors(v_target);

    /* Temporary table to hold newly created port IDs and types */
    CREATE TEMP TABLE new_ports (
        port_id  int,
        type     int,
        sector_id int
    ) ON COMMIT DROP;

    /* Insert ports respecting port_ratio, and capture their IDs and types */
    WITH inserted_ports AS (
        INSERT INTO ports (number, name, sector_id, type)
        SELECT
            s.sector_id AS number,
            randomname() AS name,
            s.sector_id AS sector_id,
            floor(random() * 8 + 1)::int AS type
        FROM sectors s
        WHERE s.sector_id > 10                       /* Don't create ports in Fedspace core */
          AND s.sector_id <= v_target
          AND NOT EXISTS (
                SELECT 1
                  FROM ports p
                 WHERE p.sector_id = s.sector_id
          )
          AND random() <= (v_port_ratio / 100.0)
        RETURNING port_id, type, sector_id
    )
    INSERT INTO new_ports
    SELECT port_id, type, sector_id
      FROM inserted_ports;

    GET DIAGNOSTICS v_added = ROW_COUNT;

    /*
     * Resolve commodity codes + base prices once.
     * Supports either short codes (ORE/ORG/EQU) or long codes (ore/organics/equipment).
     */
    SELECT c.code, c.base_price INTO v_code_ore, v_base_ore
      FROM commodities c
     WHERE c.illegal = 0
       AND (upper(c.code) = 'ORE' OR lower(c.code) = 'ore')
     ORDER BY (upper(c.code) = 'ORE') DESC
     LIMIT 1;

    SELECT c.code, c.base_price INTO v_code_org, v_base_org
      FROM commodities c
     WHERE c.illegal = 0
       AND (upper(c.code) = 'ORG' OR lower(c.code) = 'organics')
     ORDER BY (upper(c.code) = 'ORG') DESC
     LIMIT 1;

    SELECT c.code, c.base_price INTO v_code_equ, v_base_equ
      FROM commodities c
     WHERE c.illegal = 0
       AND (upper(c.code) = 'EQU' OR lower(c.code) = 'equipment')
     ORDER BY (upper(c.code) = 'EQU') DESC
     LIMIT 1;

    IF v_code_ore IS NULL OR v_code_org IS NULL OR v_code_equ IS NULL THEN
        RAISE EXCEPTION
            'generate_ports: missing required commodities. Need ORE/ORG/EQU (or ore/organics/equipment) in commodities table.';
    END IF;

    /* Seed trade modes + stock for the new ports */
    FOR v_port_rec IN
        SELECT port_id, type, sector_id
          FROM new_ports
    LOOP
        /* Populate port_trade based on type (unchanged behaviour) */
        IF v_port_rec.type = 1 THEN
            /* SBB (Sell Ore, Buy Organics/Equipment) */
            INSERT INTO port_trade (port_id, commodity, mode)
            VALUES
                (v_port_rec.port_id, 'organics', 'buy'),
                (v_port_rec.port_id, 'equipment', 'buy'),
                (v_port_rec.port_id, 'ore',       'sell');

        ELSIF v_port_rec.type = 2 THEN
            /* SSB */
            INSERT INTO port_trade (port_id, commodity, mode)
            VALUES
                (v_port_rec.port_id, 'ore',       'sell'),
                (v_port_rec.port_id, 'organics',  'sell'),
                (v_port_rec.port_id, 'equipment', 'buy');

        ELSIF v_port_rec.type = 3 THEN
            /* BSS */
            INSERT INTO port_trade (port_id, commodity, mode)
            VALUES
                (v_port_rec.port_id, 'ore',       'buy'),
                (v_port_rec.port_id, 'organics',  'sell'),
                (v_port_rec.port_id, 'equipment', 'sell');

        ELSIF v_port_rec.type = 4 THEN
            /* BSB */
            INSERT INTO port_trade (port_id, commodity, mode)
            VALUES
                (v_port_rec.port_id, 'ore',       'buy'),
                (v_port_rec.port_id, 'organics',  'sell'),
                (v_port_rec.port_id, 'equipment', 'buy');

        ELSIF v_port_rec.type = 5 THEN
            /* BBS */
            INSERT INTO port_trade (port_id, commodity, mode)
            VALUES
                (v_port_rec.port_id, 'ore',       'buy'),
                (v_port_rec.port_id, 'organics',  'buy'),
                (v_port_rec.port_id, 'equipment', 'sell');

        ELSIF v_port_rec.type = 6 THEN
            /* SBS */
            INSERT INTO port_trade (port_id, commodity, mode)
            VALUES
                (v_port_rec.port_id, 'ore',       'sell'),
                (v_port_rec.port_id, 'organics',  'buy'),
                (v_port_rec.port_id, 'equipment', 'sell');

        ELSIF v_port_rec.type = 7 THEN
            /* BBB (Specialty Port) */
            INSERT INTO port_trade (port_id, commodity, mode)
            VALUES
                (v_port_rec.port_id, 'ore',       'buy'),
                (v_port_rec.port_id, 'organics',  'buy'),
                (v_port_rec.port_id, 'equipment', 'buy');

        ELSIF v_port_rec.type = 8 THEN
            /* SSS (Specialty Port) */
            INSERT INTO port_trade (port_id, commodity, mode)
            VALUES
                (v_port_rec.port_id, 'ore',       'sell'),
                (v_port_rec.port_id, 'organics',  'sell'),
                (v_port_rec.port_id, 'equipment', 'sell');
        END IF;

        /*
         * Capacity model used by server_ports.c:
         * max_capacity = ports.size * 1000
         * If size is NULL, default to 10 (matches your observed 10000 caps).
         */
        SELECT COALESCE(p.size, 10) INTO v_size
          FROM ports p
         WHERE p.port_id = v_port_rec.port_id;

        v_cap := v_size * 1000;

        /*
         * Seed initial stock:
         * - If port mode is 'buy'  => low stock (0..10% of cap)
         * - If port mode is 'sell' => high stock (60..95% of cap)
         *
         * Prices seeded to base_price (runtime code adjusts as needed).
         */
        INSERT INTO entity_stock (entity_type, entity_id, commodity_code, quantity, price)
        VALUES
            ('port', v_port_rec.port_id, v_code_ore,
                CASE
                    WHEN EXISTS (SELECT 1 FROM port_trade pt WHERE pt.port_id = v_port_rec.port_id AND pt.commodity='ore' AND pt.mode='buy')
                        THEN GREATEST(0, (v_cap * (random() * 0.10))::int)
                    ELSE GREATEST(1, (v_cap * (0.60 + random() * 0.35))::int)
                END,
                v_base_ore
            ),
            ('port', v_port_rec.port_id, v_code_org,
                CASE
                    WHEN EXISTS (SELECT 1 FROM port_trade pt WHERE pt.port_id = v_port_rec.port_id AND pt.commodity='organics'  AND pt.mode='buy')
                        THEN GREATEST(0, (v_cap * (random() * 0.10))::int)
                    ELSE GREATEST(1, (v_cap * (0.60 + random() * 0.35))::int)
                END,
                v_base_org
            ),
            ('port', v_port_rec.port_id, v_code_equ,
                CASE
                    WHEN EXISTS (SELECT 1 FROM port_trade pt WHERE pt.port_id = v_port_rec.port_id AND pt.commodity='equipment' AND pt.mode='buy')
                        THEN GREATEST(0, (v_cap * (random() * 0.10))::int)
                    ELSE GREATEST(1, (v_cap * (0.60 + random() * 0.35))::int)
                END,
                v_base_equ
            )
        ON CONFLICT DO NOTHING;

        /*
         * Illegal goods are intentionally NOT seeded here.
         * clusters aren't populated yet, so this generator stays “legal-only”.
         */
    END LOOP;

    PERFORM bigbang_unlock(v_lock_key);
    RETURN v_added;
END;
$$;


ALTER FUNCTION public.generate_ports(target_sectors integer) OWNER TO postgres;

--
-- Name: generate_sectors(integer); Type: FUNCTION; Schema: public; Owner: postgres
--

CREATE FUNCTION public.generate_sectors(target_count integer) RETURNS bigint
    LANGUAGE plpgsql
    AS $$
DECLARE
    v_max_id bigint;
    v_added bigint := 0;
    v_lock_key bigint := 72002001;
    v_beacons text[] := ARRAY['For a good time, warp to Sector 69.', 'The Federation is lying to you regarding Ore prices!', 'Don''t trust the auto-pilot...', 'Red Dragon Corp rules this void!', 'Will trade sister for a Genesis Torpedo.', 'I hid 500,000 credits in Sector... [text unreadable]', 'The Feds is watching you.', 'Mining is for droids. Real pilots steal.', 'If you can read this, you''re in range of my cannons.', 'Turn back. Here there be dragons.'];
BEGIN
    IF target_count IS NULL OR target_count < 0 THEN
        RAISE EXCEPTION 'target_count must be >= 0';
    END IF;
    PERFORM
        bigbang_lock (v_lock_key);
    SELECT
        COALESCE(MAX(sector_id), 0) INTO v_max_id
    FROM
        sectors;
    IF v_max_id >= target_count THEN
        PERFORM
            bigbang_unlock (v_lock_key);
        RETURN 0;
    END IF;
    INSERT INTO sectors (sector_id, name, beacon, nebulae)
    SELECT
        gs AS sector_id,
        sub.name_val AS name,
        CASE WHEN random() < 0.02 THEN
            v_beacons[1 + floor(random() * array_length(v_beacons, 1))::int]
        ELSE
            NULL
        END AS beacon,
        sub.name_val AS nebulae
    FROM
        generate_series(v_max_id + 1, target_count) AS gs,
    LATERAL (
        SELECT
            CASE WHEN gs > 10
                AND random() < 0.6 THEN
                constellation_name ()
            ELSE
                NULL
            END AS name_val) sub;
    GET DIAGNOSTICS v_added = ROW_COUNT;
    PERFORM
        bigbang_unlock (v_lock_key);
    RETURN v_added;
END;
$$;


ALTER FUNCTION public.generate_sectors(target_count integer) OWNER TO postgres;

--
-- Name: generate_stardock(); Type: FUNCTION; Schema: public; Owner: postgres
--

CREATE FUNCTION public.generate_stardock() RETURNS bigint
    LANGUAGE plpgsql
    AS $$
DECLARE
    v_sector int;
    v_max_sector int;
    v_lock_key bigint := 72002006;
    v_exists boolean;
    v_port_id int;
BEGIN
    PERFORM
        bigbang_lock (v_lock_key);
    -- Check if Stardock exists
    SELECT
        EXISTS (
            SELECT
                1
            FROM
                ports
            WHERE
                type = 9) INTO v_exists;
    IF v_exists THEN
        PERFORM
            bigbang_unlock (v_lock_key);
        RETURN 0;
    END IF;
    SELECT
        MAX(sector_id) INTO v_max_sector
    FROM
        sectors;
    -- Pick random sector_id > 10
    v_sector := 11 + floor(random() * (v_max_sector - 10));
    -- Insert Stardock (Type 9, Size 10, Tech 10)
    INSERT INTO ports (number, name, sector_id, type, size, techlevel, petty_cash)
        VALUES (v_sector, 'Stardock', v_sector, 9, 10, 10, 1000000)
    ON CONFLICT (sector_id, number)
        DO UPDATE SET
            name = EXCLUDED.name, type = EXCLUDED.type, size = EXCLUDED.size, techlevel = EXCLUDED.techlevel, petty_cash = EXCLUDED.petty_cash
        RETURNING
            port_id INTO v_port_id;
    -- Seed Stardock Assets
    IF v_port_id IS NOT NULL THEN
        INSERT INTO stardock_assets (sector_id, owner_id, fighters, defenses, ship_capacity)
            VALUES (v_sector, 1, 10000, 500, 100) -- Assuming owner_id 1 is System/Fed
        ON CONFLICT (sector_id)
            DO NOTHING;
        -- Seed Shipyard with all purchasable ships
        INSERT INTO shipyard_inventory (port_id, ship_type_id, enabled)
        SELECT
            v_port_id,
            shiptypes_id,
            1
        FROM
            shiptypes
        WHERE
            can_purchase = TRUE
        ON CONFLICT (port_id,
            ship_type_id)
            DO NOTHING;
    END IF;
    PERFORM
        bigbang_unlock (v_lock_key);
    RETURN 1;
END;
$$;


ALTER FUNCTION public.generate_stardock() OWNER TO postgres;

--
-- Name: generate_taverns(integer); Type: FUNCTION; Schema: public; Owner: postgres
--

CREATE FUNCTION public.generate_taverns(p_ratio_pct integer DEFAULT 20) RETURNS bigint
    LANGUAGE plpgsql
    AS $$
DECLARE
    v_added bigint := 0;
    v_lock_key bigint := 72002007;
    v_ratio numeric := p_ratio_pct / 100.0;
BEGIN
    PERFORM
        bigbang_lock (v_lock_key);
    INSERT INTO taverns (sector_id, name_id)
    SELECT
        s.sector_id,
        (
            SELECT
                tavern_names_id
            FROM
                tavern_names
            ORDER BY
                random()
            LIMIT 1)
FROM
    sectors s
WHERE
    s.sector_id > 10 -- No taverns in Fedspace core
        AND NOT EXISTS (
            SELECT
                1
            FROM
                taverns t
            WHERE
                t.sector_id = s.sector_id)
        AND random() < v_ratio;
    GET DIAGNOSTICS v_added = ROW_COUNT;
    PERFORM
        bigbang_unlock (v_lock_key);
    RETURN v_added;
END;
$$;


ALTER FUNCTION public.generate_taverns(p_ratio_pct integer) OWNER TO postgres;

--
-- Name: generate_tunnels(integer, integer); Type: FUNCTION; Schema: public; Owner: postgres
--

CREATE FUNCTION public.generate_tunnels(p_min_tunnels integer DEFAULT 15, p_min_tunnel_len integer DEFAULT 4) RETURNS bigint
    LANGUAGE plpgsql
    AS $$
DECLARE
    v_tunnels_created bigint := 0;
    v_next_sector int;
    v_tunnel_len int;
    v_i int;
    v_prev_sector int;
    v_curr_sector int;
    v_max_sector int;
    v_total_needed int;
    v_tunnel_start int;
    v_tunnel_end int;
BEGIN
    -- Get the current max sector
    SELECT COALESCE(MAX(sector_id), 10) INTO v_max_sector FROM sectors;
    
    -- Calculate how many sectors we'll need for tunnels
    -- Maximum: (min_tunnels + 5) tunnels * (min_tunnel_len + 5) sectors per tunnel
    v_total_needed := (p_min_tunnels + 5) * (p_min_tunnel_len + 5);
    
    -- Insert missing sectors
    INSERT INTO sectors (sector_id, name, nebulae)
    SELECT gs, 'Tunnel Sector ' || gs::text, NULL
    FROM generate_series(v_max_sector + 1, v_max_sector + v_total_needed) AS gs
    WHERE NOT EXISTS (SELECT 1 FROM sectors WHERE sector_id = gs)
    ON CONFLICT (sector_id) DO NOTHING;
    
    -- Create table to track tunnel endpoints
    CREATE TABLE IF NOT EXISTS longest_tunnels (
        tunnel_id serial PRIMARY KEY,
        entry_sector int,
        exit_sector int,
        tunnel_path text,
        tunnel_length_edges int
    );
    TRUNCATE longest_tunnels;
    
    v_next_sector := v_max_sector + 1;
    
    -- Create min_tunnels + up to 5 extra tunnels
    WHILE v_tunnels_created < (p_min_tunnels + (random() * 5)::int) LOOP
        -- Random tunnel length between min_tunnel_len and min_tunnel_len + 4
        v_tunnel_len := p_min_tunnel_len + (random() * 5)::int;
        v_prev_sector := NULL;
        v_tunnel_start := NULL;
        v_tunnel_end := NULL;
        
        -- Create the tunnel chain
        FOR v_i IN 1..v_tunnel_len LOOP
            v_curr_sector := v_next_sector;
            v_next_sector := v_next_sector + 1;
            
            -- Track tunnel start and end
            IF v_tunnel_start IS NULL THEN
                v_tunnel_start := v_curr_sector;
            END IF;
            v_tunnel_end := v_curr_sector;
            
            -- Create bidirectional warp between previous and current
            IF v_prev_sector IS NOT NULL THEN
                INSERT INTO sector_warps (from_sector, to_sector)
                    VALUES (v_prev_sector, v_curr_sector),
                           (v_curr_sector, v_prev_sector)
                ON CONFLICT DO NOTHING;
            END IF;
            
            v_prev_sector := v_curr_sector;
        END LOOP;
        
        -- Record this tunnel's endpoints
        INSERT INTO longest_tunnels (entry_sector, exit_sector, tunnel_path, tunnel_length_edges)
        VALUES (v_tunnel_start, v_tunnel_end, v_tunnel_start::text || '-...-' || v_tunnel_end::text, v_tunnel_len);
        
        v_tunnels_created := v_tunnels_created + 1;
    END LOOP;
    
    RETURN v_tunnels_created;
END;
$$;


ALTER FUNCTION public.generate_tunnels(p_min_tunnels integer, p_min_tunnel_len integer) OWNER TO postgres;

--
-- Name: get_constellation_name_entry(integer); Type: FUNCTION; Schema: public; Owner: postgres
--

CREATE FUNCTION public.get_constellation_name_entry(idx integer) RETURNS text
    LANGUAGE sql IMMUTABLE
    AS $$
    SELECT
        (ARRAY['Uncharted Space', 'Andromeda', 'Antlia', 'Apus', 'Aquarius', 'Aquila', 'Ara', 'Aries', 'Auriga', 'Bootes', 'Caelum', 'Camelopardalis', 'Cancer', 'Canes', 'Canis', 'Canis', 'Capricornus', 'Carina', 'Cassiopeia', 'Centaurus', 'Cepheus', 'Cetus', 'Chamaleon', 'Circinus', 'Columba', 'Coma', 'Corona', 'Corona', 'Corvus', 'Crater', 'Crux', 'Cygnus', 'Delphinus', 'Dorado', 'Draco', 'Equuleus', 'Eridanus', 'Fornax', 'Gemini', 'Grus', 'Hercules', 'Horologium', 'Hydra', 'Hydrus', 'Indus', 'Lacerta', 'Leo', 'Leo', 'Lepus', 'Libra', 'Lupus', 'Lynx', 'Lyra', 'Mensa', 'Microscopium', 'Monoceros', 'Musca', 'Norma', 'Octans', 'Ophiucus', 'Orion', 'Pavo', 'Pegasus', 'Perseus', 'Phoenix', 'Pictor', 'Pisces', 'Pisces', 'Puppis', 'Pyxis', 'Reticulum', 'Sagitta', 'Sagittarius', 'Scorpius', 'Sculptor', 'Scutum', 'Serpens', 'Sextans', 'Taurus', 'Telescopium', 'Triangulum', 'Triangulum', 'Tucana', 'Ursa', 'Ursa', 'Vela', 'Virgo', 'Volans', 'Vulpecula', 'Theta Eri', 'Alpha Eri', 'Beta Sco', 'Alpha Cnc', 'Zeta Leo', 'Epsilon CMa', 'Epsilon Tau', 'Epsilon Aqr', 'Alpha Crv', 'Alpha Tau', 'Alpha Cep', 'Beta Cep', 'Alpha Cap', 'Gamma Peg', 'Gamma Leo', 'Beta Ori', 'Beta Per', 'Delta Crv', 'Gamma Gem', 'Epsilon UMa', 'Eta UMa', 'Alpha Crt', 'Gamma And', 'Gamma Gem', 'Alpha Gru', 'Zeta Cen', 'Epsilon Ori', 'Zeta Ori', 'Alpha Hya', 'Alpha CrB', 'Alpha And', 'Sigma Dra', 'Lambda Vel', 'Alpha Aql', 'Delta Dra', 'Lambda Leo', 'Eta CMa', 'Xi UMa', 'Nu UMa', 'Theta Ser', 'Tau2 Eri', 'Alpha Phe', 'Beta Sgr', 'Alpha Lep', 'Mu Dra', 'Omicron Per', 'Delta Vir', 'Eta Eri', 'Theta Peg', 'Zeta Cet', 'Omicron1 Eri', 'Eta UMa', 'Alpha Ori', 'Delta Ari', 'Beta Cas', 'Beta Oph', 'Theta Leo', 'Beta Eri', 'Beta Cap', 'Alpha Cyg', 'Epsilon Del', 'Delta Cap', 'Beta Cet', 'Beta Leo', 'Beta Cet', 'Delta Sco', 'Alpha UMa', 'Psi Dra', 'Iota Dra', 'Beta Tau', 'Gamma Dra', 'Epsilon Peg', 'Gamma Cep', 'Alpha PsA', 'Zeta CMa', 'Gamma Crv', 'Epsilon Cyg', 'Beta CMi', 'Beta Cen', 'Alpha Ari', 'Lambda Ori', 'Zeta Peg', 'Epsilon Boo', 'Nu Sco', 'Gamma Cet', 'Epsilon Sgr', 'Lambda Sgr', 'Delta Sgr', 'Omicron2 Eri', 'Alpha Equ', 'Beta UMi', 'Xi Cep', 'Upsilon Sco', 'Lambda Her', 'Epsilon Aur', 'Lambda Oph', 'Alpha Peg', 'Eta Peg', 'Epsilon Gem', 'Delta UMa', 'Lambda Ori', 'Zeta Gem', 'Beta Aur', 'Alpha Cet', 'Xi Per', 'Beta UMa', 'Delta Ori', 'Beta And', 'Alpha Per', 'Zeta UMa', 'Alpha Tri', 'Eta Boo', 'Beta CMa', 'Gamma Cap', 'Beta Boo', 'Gamma Sgr', 'Beta Lep', 'Beta CrB', 'Alpha Psc', 'Alpha Col', 'Gamma UMa', 'Gamma UMi', 'Mu Leo', 'Alpha Her', 'Alpha Oph', 'Beta Dra', 'Beta Ori', 'Alpha Cen', 'Alpha Psc', 'Delta Cas', 'Alpha Sgr', 'Eta Oph', 'Gamma Aqr', 'Mu Peg', 'Alpha Aqr', 'Beta Aqr', 'Gamma Cyg', 'Kappa Ori', 'Beta Peg', 'Lambda Sco', 'Alpha Cas', 'Beta Ari', 'Alpha And', 'Delta Aqr', 'Gamma Lyr', 'Mu UMa', 'Lambda UMa', 'Kappa UMa', 'Iota UMa', 'Beta Cnc', 'Alpha Dra', 'Alpha Ser', 'Alpha Lyr', 'Delta Gem', 'Beta Col', 'Delta CMa', 'Delta Oph', 'Epsilon Oph', 'Epsilon Vir', 'Gamma Eri', 'Beta Vir', 'Alpha Lib', 'Beta Lib', 'Acamar', 'Achernar', 'Acrab', 'Acubens', 'Adhafera', 'Adhara', 'Ain', 'Albali', 'Alchibah', 'Aldebaran', 'Alderamin', 'Alfirk', 'Algedi', 'Algenib', 'Algieba', 'Algebar', 'Algol', 'Algorab', 'Alhena', 'Alioth', 'Alkaid', 'Alkes', 'Almak', 'Almeisan', 'Alnair', 'Alnair', 'Alnilam', 'Alnitak', 'Alphard', 'Alphecca', 'Alpheratz', 'Alsafi', 'Alsuhail', 'Altair', 'Altais', 'Alterf', 'Aludra', 'Alula Australis', 'Alula Borealis', 'Alya', 'Angetenar', 'Ankaa', 'Arkab', 'Arneb', 'Arrakis', 'Atik', 'Auva', 'Azha', 'Baham', 'Baten Kaitos', 'Beid', 'Benetnash', 'Betelgeuse', 'Botein', 'Caph', 'Celbalrai', 'Chort', 'Cursa', 'Dabih', 'Deneb', 'Deneb', 'Deneb Algedi', 'Deneb Kaitos', 'Denebola', 'Diphda', 'Dschubba', 'Dubhe', 'Dziban', 'Edasich', 'El Nath', 'Eltanin', 'Enif', 'Errai', 'Fomalhaut', 'Furud', 'Gienah', 'Gienah', 'Gomeisa', 'Hadar', 'Hamal', 'Heka', 'Homam', 'Izar', 'Jabbah', 'Kaffaljidhma', 'Kaus Australis', 'Kaus Borealis', 'Kaus Media', 'Keid', 'Kitalpha', 'Kokab', 'Kurhah', 'Lesath', 'Maasym', 'Maaz', 'Marfik', 'Markab', 'Matar', 'Mebsuta', 'Megrez', 'Meissa', 'Mekbuda', 'Menkalinan', 'Menkar', 'Menkib', 'Merak', 'Mintaka', 'Mirak', 'Mirfak', 'Mizar', 'Mothallah', 'Muphrid', 'Murzim', 'Nashira', 'Nekkar', 'Nasl', 'Nihal', 'Nusakan', 'Okda', 'Phact', 'Phad', 'Pherkad', 'Rasalased', 'Rasalgethi', 'Rasalhague', 'Rastaban', 'Rigel', 'Rigilkent', 'Risha', 'Rukbah', 'Rukbat', 'Sabik', 'Sadachbia', 'Sadalbari', 'Sadalmelik', 'Sadalsuud', 'Sadr', 'Saiph', 'Scheat', 'Shaula', 'Shedir', 'Sheratan', 'Sirrah', 'Skat', 'Sulafat', 'Tania Australis', 'Tania Borealis', 'Talitha Australis', 'Talitha Borealis', 'Tarf', 'Thuban', 'Unukalhai', 'Vega', 'Wasat', 'Wazn', 'Wezen', 'Yed Prior', 'Yed Posterior', 'Zaniah', 'Zaurac', 'Zavijava', 'Zubenelgenubi', 'Zubeneshamali'])[idx + 1];
$$;


ALTER FUNCTION public.get_constellation_name_entry(idx integer) OWNER TO postgres;

--
-- Name: get_first_syllable(integer); Type: FUNCTION; Schema: public; Owner: postgres
--

CREATE FUNCTION public.get_first_syllable(idx integer) RETURNS text
    LANGUAGE sql IMMUTABLE
    AS $$
    SELECT
        (ARRAY['A', 'Ab', 'Ac', 'Add', 'Ad', 'Af', 'Aggr', 'Ax', 'Az', 'Bat', 'Be', 'Byt', 'Cyth', 'Agr', 'Ast', 'As', 'Al', 'Adw', 'Adr', 'Ar', 'B', 'Br', 'C', 'Cr', 'Ch', 'Cad', 'D', 'Dr', 'Dw', 'Ed', 'Eth', 'Et', 'Er', 'El', 'Eow', 'F', 'Fr', 'Ferr', 'G', 'Gr', 'Gw', 'Gw', 'Gal', 'Gl', 'H', 'Ha', 'Ib', 'Jer', 'K', 'Ka', 'Ked', 'L', 'Loth', 'Lar', 'Leg', 'M', 'Mir', 'N', 'Jer', 'K', 'Ka', 'Ked', 'L', 'Loth', 'Lar', 'Leg', 'M', 'Mir', 'N', 'Nyd', 'Ol', 'Oc', 'On', 'P', 'Pr', 'R', 'Rh', 'S', 'Sev', 'T', 'Tr', 'Th', 'Th', 'V', 'Y', 'Yb', 'Z', 'W', 'Wic', 'Wac', 'Wer', 'Fert', 'D''al', 'Fl''a', 'L''Dre', 'Ra', 'Rea', 'Og', 'O''g', 'Ndea', 'Faw', 'Cef', 'Cyth', 'Wyh', 'Gyh', 'G''As', 'Red', 'Aas', 'Aaw', 'Ewwa', 'Syw'])[idx + 1];
$$;


ALTER FUNCTION public.get_first_syllable(idx integer) OWNER TO postgres;

--
-- Name: get_last_syllable(integer); Type: FUNCTION; Schema: public; Owner: postgres
--

CREATE FUNCTION public.get_last_syllable(idx integer) RETURNS text
    LANGUAGE sql IMMUTABLE
    AS $$
    SELECT
        (ARRAY['and', 'be', 'bwyn', 'baen', 'bard', 'ctred', 'cred', 'ch', 'can', 'dan', 'don', 'der', 'dric', 'dfrid', 'dus', 'gord', 'gan', 'li', 'le', 'lgrin', 'lin', 'lith', 'lath', 'loth', 'ld', 'ldric', 'ldan', 'mas', 'mos', 'mar', 'ond', 'ydd', 'idd', 'nnon', 'wan', 'yth', 'nad', 'nn', 'nor', 'nd', 'ron', 'rd', 'sh', 'seth', 'ean', 'th', 'threm', 'tha', 'tan', 'tem', 'ron', 'rd', 'sh', 'seth', 'ean', 'th', 'threm', 'tha', 'tan', 'tem', 'tam', 'vix', 'vud', 'wix', 'wan', 'win', 'wyn', 'wyr', 'wyth', 'zer', 'zan', 'qela', 'rli', 'wa', 'kera', 'ji', 'jia', 'jioe', 'jiti', 'jote', 'kie', 'hireg', 'jira', 'fila', 'vili', 'xilli', 'cira', 'digo', 'no', 'noje', 'woli', 'yolye', 'tua', 'tue', 'tye', 'toa', 'toi', 'toe', 'tore', 'apd', 'pe', 'btyn', 'brrin', 'berd', 'cfed', 'cadf', 'cac', 'cane', 'fdan', 'fdon'])[idx + 1];
$$;


ALTER FUNCTION public.get_last_syllable(idx integer) OWNER TO postgres;

--
-- Name: get_middle_syllable(integer); Type: FUNCTION; Schema: public; Owner: postgres
--

CREATE FUNCTION public.get_middle_syllable(idx integer) RETURNS text
    LANGUAGE sql IMMUTABLE
    AS $$
    SELECT
        (ARRAY['a', 'ase', 'ae', 'ae', 'au', 'ao', 'are', 'ale', 'ali', 'ay', 'ardo', 'e', 'ere', 'ehe', 'eje', 'eo', 'ei', 'ea', 'ea', 'eye', 'eri', 'era', 'ela', 'eli', 'enda', 'erra', 'i', 'ia', 'ioe', 'itti', 'otte', 'ie', 'ire', 'eli', 'enda', 'erra', 'i', 'ia', 'ioe', 'itti', 'otte', 'ie', 'ire', 'ira', 'ila', 'ili', 'illi', 'ira', 'igo', 'o', 'oje', 'oli', 'olye', 'ua', 'ue', 'uyye', 'oa', 'oi', 'oe', 'ore'])[idx + 1];
$$;


ALTER FUNCTION public.get_middle_syllable(idx integer) OWNER TO postgres;

--
-- Name: handle_ship_destruction(bigint, bigint, bigint, text, bigint, bigint, numeric, bigint, bigint, bigint, bigint); Type: FUNCTION; Schema: public; Owner: postgres
--

CREATE FUNCTION public.handle_ship_destruction(p_victim_player_id bigint, p_victim_ship_id bigint, p_killer_player_id bigint, p_cause text, p_sector_id bigint, p_xp_loss_flat bigint, p_xp_loss_percent numeric, p_max_pods_per_day bigint, p_big_sleep_seconds bigint DEFAULT 0, p_now_ts bigint DEFAULT (EXTRACT(epoch FROM now()))::bigint, p_owner_role_id bigint DEFAULT 1) RETURNS TABLE(result_code integer, decision text, new_experience bigint, xp_lost bigint, escape_pod_ship_id bigint, engine_event_id bigint)
    LANGUAGE plpgsql
    AS $$
DECLARE
    v_current_xp bigint;
    v_new_xp bigint;
    v_xp_loss bigint;
    v_shiptype_id bigint;
    v_has_escape_pod boolean;
    v_podded_count_today bigint;
    v_podded_last_reset bigint;
    v_escape_pod_shiptype_id bigint;
    v_cfg_val text;
    v_payload text;
BEGIN
    /*
     Concurrency / correctness:
     - lock victim player row (FOR UPDATE)
     - lock victim ship row (FOR UPDATE)
     - lock podded_status row (FOR UPDATE)
     */
    -- 0) Lock and read player XP
    SELECT
        experience INTO v_current_xp
    FROM
        public.players
    WHERE
        id = p_victim_player_id
    FOR UPDATE;
    IF NOT FOUND THEN
        RAISE EXCEPTION 'handle_ship_destruction: victim player % not found', p_victim_player_id
            USING ERRCODE = 'NO_DATA_FOUND';
        END IF;
        -- 1) Lock ship, read its type_id (for eligibility), then mark destroyed
        SELECT
            type_id INTO v_shiptype_id
        FROM
            public.ships
        WHERE
            id = p_victim_ship_id
        FOR UPDATE;
        IF NOT FOUND THEN
            RAISE EXCEPTION 'handle_ship_destruction: victim ship % not found', p_victim_ship_id
                USING ERRCODE = 'NO_DATA_FOUND';
            END IF;
            -- eligibility from shiptypes.has_escape_pod (NULL shiptype => treat as false)
            SELECT
                COALESCE(st.has_escape_pod, FALSE) INTO v_has_escape_pod
            FROM
                public.shiptypes st
            WHERE
                st.shiptypes_id = v_shiptype_id;
            -- mark destroyed
            UPDATE
                public.ships
            SET
                destroyed = 1
            WHERE
                id = p_victim_ship_id;
            -- detach ship from player active ship slot (only if currently set to this ship)
            UPDATE
                public.players
            SET
                ship_id = NULL
            WHERE
                id = p_victim_player_id
                AND ship_id = p_victim_ship_id;
            -- 2) increment times_blown_up (now a real column)
            UPDATE
                public.players
            SET
                times_blown_up = COALESCE(times_blown_up, 0) + 1
            WHERE
                id = p_victim_player_id;
            -- 3) apply XP penalty
            v_xp_loss := p_xp_loss_flat + floor(v_current_xp * (p_xp_loss_percent / 100.0))::bigint;
            v_new_xp := v_current_xp - v_xp_loss;
            IF v_new_xp < 0 THEN
                v_new_xp := 0;
            END IF;
            UPDATE
                public.players
            SET
                experience = v_new_xp
            WHERE
                id = p_victim_player_id;
            -- 4) podded_status row ensure + lock
            INSERT INTO public.podded_status (player_id, status, podded_count_today, podded_last_reset)
                VALUES (p_victim_player_id, 'active', 0, p_now_ts)
            ON CONFLICT (player_id)
                DO NOTHING;
            SELECT
                podded_count_today,
                podded_last_reset INTO v_podded_count_today,
                v_podded_last_reset
            FROM
                public.podded_status
            WHERE
                player_id = p_victim_player_id
            FOR UPDATE;
            -- reset daily pod counter if older than 24h or NULL
            IF v_podded_last_reset IS NULL OR (p_now_ts - v_podded_last_reset) >= 86400 THEN
                v_podded_count_today := 0;
                v_podded_last_reset := p_now_ts;
                UPDATE
                    public.podded_status
                SET
                    podded_count_today = 0,
                    podded_last_reset = p_now_ts
                WHERE
                    player_id = p_victim_player_id;
            END IF;
            -- 5) decide pod vs big sleep
            escape_pod_ship_id := NULL;
            IF v_has_escape_pod AND v_podded_count_today < p_max_pods_per_day THEN
                decision := 'podded';
                -- load escape pod shiptype id from config
                SELECT
                    value INTO v_cfg_val
                FROM
                    public.config
                WHERE
                    key = 'death.escape_pod_shiptype_id'
                    AND type = 'int';
                IF v_cfg_val IS NULL THEN
                    -- hard fail: you asked for pod spawn, but config missing => safer to big sleep
                    decision := 'big_sleep';
                ELSE
                    v_escape_pod_shiptype_id := v_cfg_val::bigint;
                    -- spawn escape pod ship (keep this INSERT minimal; relies on defaults / nullable cols)
                    INSERT INTO public.ships (name, type_id, sector_id, destroyed)
                        VALUES ('Escape Pod', v_escape_pod_shiptype_id, 1, 0)
                    RETURNING
                        ship_id INTO escape_pod_ship_id;
                    -- assign as active ship
                    UPDATE
                        public.players
                    SET
                        ship_id = escape_pod_ship_id
                    WHERE
                        player_id = p_victim_player_id;
                    -- ownership (role_id is your canonical "owner" role)
                    INSERT INTO public.ship_ownership (ship_id, player_id, role_id, is_primary)
                        VALUES (escape_pod_ship_id, p_victim_player_id, p_owner_role_id, TRUE)
                    ON CONFLICT (ship_id, player_id, role_id)
                        DO UPDATE SET
                            is_primary = EXCLUDED.is_primary;
                    -- update podded status + increment daily pod count
                    UPDATE
                        public.podded_status
                    SET
                        status = 'podded',
                        big_sleep_until = NULL,
                        reason = NULL,
                        podded_count_today = v_podded_count_today + 1,
                        podded_last_reset = v_podded_last_reset
                    WHERE
                        player_id = p_victim_player_id;
                END IF;
            END IF;
            IF decision IS DISTINCT FROM 'podded' THEN
                decision := 'big_sleep';
                UPDATE
                    public.podded_status
                SET
                    status = 'big_sleep',
                    big_sleep_until = CASE WHEN p_big_sleep_seconds > 0 THEN
                        p_now_ts + p_big_sleep_seconds
                    ELSE
                        NULL
                    END,
                    reason = COALESCE(p_cause, 'other')
                WHERE
                    player_id = p_victim_player_id;
            END IF;
            -- 6) log engine event (ship.destroyed)
            v_payload := json_build_object('victim_ship_id', p_victim_ship_id, 'victim_player_id', p_victim_player_id, 'killer_player_id', p_killer_player_id, 'cause', COALESCE(p_cause, 'other'), 'sector_id', p_sector_id, 'decision', decision, 'pod_sector_id', CASE WHEN decision = 'podded' THEN
                    1
                ELSE
                    NULL
                END, 'escape_pod_ship_id', escape_pod_ship_id, 'xp_lost', v_xp_loss, 'new_xp', v_new_xp)::text;
            INSERT INTO public.engine_events (ts, type, actor_player_id, sector_id, payload, idem_key)
                VALUES (p_now_ts, 'ship.destroyed', NULL, p_sector_id, v_payload, NULL)
            RETURNING
                engine_events_id INTO engine_event_id;
            -- outputs
            result_code := 0;
            new_experience := v_new_xp;
            xp_lost := v_xp_loss;
            RETURN NEXT;
END;
$$;


ALTER FUNCTION public.handle_ship_destruction(p_victim_player_id bigint, p_victim_ship_id bigint, p_killer_player_id bigint, p_cause text, p_sector_id bigint, p_xp_loss_flat bigint, p_xp_loss_percent numeric, p_max_pods_per_day bigint, p_big_sleep_seconds bigint, p_now_ts bigint, p_owner_role_id bigint) OWNER TO postgres;

--
-- Name: market_fill_order(bigint, bigint, bigint); Type: FUNCTION; Schema: public; Owner: postgres
--

CREATE FUNCTION public.market_fill_order(p_order_id bigint, p_fill_qty bigint, p_actor_id bigint) RETURNS public.db_result
    LANGUAGE plpgsql
    AS $$
DECLARE
  v_remaining bigint;
BEGIN
  PERFORM assert_column_exists('commodity_orders'::regclass, 'id');
  -- TODO schema: remaining_qty/status/filled_qty/quantity fields
  PERFORM assert_column_exists('commodity_orders'::regclass, 'remaining_qty');
  PERFORM assert_column_exists('commodity_orders'::regclass, 'status');

  PERFORM 1 FROM commodity_orders WHERE id = p_order_id FOR UPDATE;
  IF NOT FOUND THEN RETURN (404,'order not found',NULL); END IF;

  SELECT remaining_qty INTO v_remaining FROM commodity_orders WHERE id = p_order_id;

  IF p_fill_qty <= 0 THEN RETURN (412,'fill_qty must be > 0',NULL); END IF;
  IF v_remaining < p_fill_qty THEN RETURN (409,'insufficient remaining qty',NULL); END IF;

  -- Insert trade row (template: adjust columns)
  PERFORM assert_column_exists('commodity_trades'::regclass, 'order_id');
  PERFORM assert_column_exists('commodity_trades'::regclass, 'qty');
  PERFORM assert_column_exists('commodity_trades'::regclass, 'actor_id');

  INSERT INTO commodity_trades (order_id, qty, actor_id)
  VALUES (p_order_id, p_fill_qty, p_actor_id);

  UPDATE commodity_orders
  SET remaining_qty = remaining_qty - p_fill_qty,
      status = CASE WHEN remaining_qty - p_fill_qty = 0 THEN 'filled' ELSE status END
  WHERE id = p_order_id;

  RETURN (0,'ok',NULL);
END;
$$;


ALTER FUNCTION public.market_fill_order(p_order_id bigint, p_fill_qty bigint, p_actor_id bigint) OWNER TO postgres;

--
-- Name: player_land(bigint, bigint); Type: FUNCTION; Schema: public; Owner: postgres
--

CREATE FUNCTION public.player_land(p_player_id bigint, p_planet_id bigint) RETURNS public.db_result
    LANGUAGE plpgsql
    AS $_$
DECLARE
  v_sector bigint;
BEGIN
  PERFORM assert_column_exists('players'::regclass, 'id');
  PERFORM assert_column_exists('players'::regclass, 'sector');
  PERFORM assert_column_exists('planets'::regclass, 'id');
  PERFORM assert_column_exists('planets'::regclass, 'sector');

  PERFORM 1 FROM players WHERE id = p_player_id FOR UPDATE;
  IF NOT FOUND THEN RETURN (404,'player not found',NULL); END IF;

  SELECT sector INTO v_sector FROM players WHERE id = p_player_id;

  PERFORM 1 FROM planets WHERE id = p_planet_id AND sector = v_sector FOR UPDATE;
  IF NOT FOUND THEN RETURN (412,'planet not in current sector',NULL); END IF;

  IF EXISTS (SELECT 1 FROM pg_attribute WHERE attrelid='players'::regclass AND attname='lastplanet' AND NOT attisdropped) THEN
    EXECUTE 'UPDATE players SET lastplanet = $1 WHERE id = $2' USING p_planet_id, p_player_id;
  END IF;

  RETURN (0,'ok',NULL);
END;
$_$;


ALTER FUNCTION public.player_land(p_player_id bigint, p_planet_id bigint) OWNER TO postgres;

--
-- Name: player_launch(bigint); Type: FUNCTION; Schema: public; Owner: postgres
--

CREATE FUNCTION public.player_launch(p_player_id bigint) RETURNS public.db_result
    LANGUAGE plpgsql
    AS $_$
BEGIN
  PERFORM assert_column_exists('players'::regclass, 'id');

  PERFORM 1 FROM players WHERE id = p_player_id FOR UPDATE;
  IF NOT FOUND THEN RETURN (404,'player not found',NULL); END IF;

  IF EXISTS (SELECT 1 FROM pg_attribute WHERE attrelid='players'::regclass AND attname='lastplanet' AND NOT attisdropped) THEN
    EXECUTE 'UPDATE players SET lastplanet = NULL WHERE id = $1' USING p_player_id;
  END IF;

  RETURN (0,'ok',NULL);
END;
$_$;


ALTER FUNCTION public.player_launch(p_player_id bigint) OWNER TO postgres;

--
-- Name: player_move(bigint, bigint); Type: FUNCTION; Schema: public; Owner: postgres
--

CREATE FUNCTION public.player_move(p_player_id bigint, p_to_sector bigint) RETURNS public.db_result
    LANGUAGE plpgsql
    AS $_$
BEGIN
  PERFORM assert_column_exists('players'::regclass, 'id');
  PERFORM assert_column_exists('players'::regclass, 'sector');

  PERFORM 1 FROM players WHERE id = p_player_id FOR UPDATE;
  IF NOT FOUND THEN
    RETURN (404, 'player not found', NULL);
  END IF;

  UPDATE players SET sector = p_to_sector WHERE id = p_player_id;

  -- Optional clears if columns exist (best-effort)
  IF EXISTS (SELECT 1 FROM pg_attribute WHERE attrelid='players'::regclass AND attname='lastplanet' AND NOT attisdropped) THEN
    EXECUTE 'UPDATE players SET lastplanet = NULL WHERE id = $1' USING p_player_id;
  END IF;

  RETURN (0, 'ok', NULL);
END;
$_$;


ALTER FUNCTION public.player_move(p_player_id bigint, p_to_sector bigint) OWNER TO postgres;

--
-- Name: randomname(); Type: FUNCTION; Schema: public; Owner: postgres
--

CREATE FUNCTION public.randomname() RETURNS text
    LANGUAGE plpgsql
    AS $$
DECLARE
    v_f int;
    v_m int;
    v_l int;
    v_len_f int := 111;
    v_len_m int := 60;
    v_len_l int := 110;
BEGIN
    v_f := floor(random() * v_len_f)::int;
    v_m := floor(random() * v_len_m)::int;
    v_l := floor(random() * v_len_l)::int;
    RETURN get_first_syllable (v_f) || get_middle_syllable (v_m) || get_last_syllable (v_l);
END;
$$;


ALTER FUNCTION public.randomname() OWNER TO postgres;

--
-- Name: register_player(text, text, text, boolean, integer); Type: FUNCTION; Schema: public; Owner: postgres
--

CREATE FUNCTION public.register_player(p_username text, p_password text, p_ship_name text DEFAULT NULL::text, p_is_npc boolean DEFAULT false, p_sector_id integer DEFAULT 1) RETURNS bigint
    LANGUAGE plpgsql
    AS $$
DECLARE
    v_player_id bigint;
    v_ship_id bigint;
    v_ship_name text;
    v_starting_credits bigint;
    v_turns_per_day integer;
BEGIN
    -- Use provided ship name or default
    v_ship_name := COALESCE(p_ship_name, 'Used Scout Marauder');
    -- Get config values
    SELECT
        CAST(value AS bigint) INTO v_starting_credits
    FROM
        config
    WHERE
        key = 'startingcredits';
    SELECT
        CAST(value AS integer) INTO v_turns_per_day
    FROM
        config
    WHERE
        key = 'turnsperday';
    v_starting_credits := COALESCE(v_starting_credits, 5000);
    v_turns_per_day := COALESCE(v_turns_per_day, 500);
    -- Insert new player (will get auto-generated ID)
    INSERT INTO players (name, passwd, sector_id, credits, type, is_npc, loggedin, commission_id)
        VALUES (p_username, p_password, p_sector_id, v_starting_credits, 2, p_is_npc, now(), 1)
    RETURNING
        player_id INTO v_player_id;
    -- Create starter ship (Scout Marauder, type_id = 2)
    INSERT INTO ships (name, type_id, sector_id, holds, fighters, shields)
        VALUES (v_ship_name, 2, p_sector_id, 10, 1, 1)
    RETURNING
        ship_id INTO v_ship_id;
    -- Assign ship to player
    UPDATE
        players
    SET
        ship_id = v_ship_id
    WHERE
        player_id = v_player_id;
    -- Create ship ownership record
    INSERT INTO ship_ownership (ship_id, player_id, role_id, is_primary)
        VALUES (v_ship_id, v_player_id, 1, TRUE);
    -- Create bank account for player
    INSERT INTO bank_accounts (owner_type, owner_id, currency, balance)
        VALUES ('player', v_player_id, 'CRD', 0)
    ON CONFLICT
        DO NOTHING;
    -- Initialize turns for player
    INSERT INTO turns (player_id, turns_remaining, last_update)
        VALUES (v_player_id, v_turns_per_day, now())
    ON CONFLICT
        DO NOTHING;
    -- Create player preferences record
    INSERT INTO player_prefs (player_prefs_id, key, type, value)
        VALUES (v_player_id, 'default_preferences_initialized', 'bool', 'true')
    ON CONFLICT
        DO NOTHING;
    RETURN v_player_id;
END;
$$;


ALTER FUNCTION public.register_player(p_username text, p_password text, p_ship_name text, p_is_npc boolean, p_sector_id integer) OWNER TO postgres;

--
-- Name: seed_factions(); Type: FUNCTION; Schema: public; Owner: postgres
--

CREATE FUNCTION public.seed_factions() RETURNS bigint
    LANGUAGE plpgsql
    AS $$
DECLARE
    v_added bigint := 0;
    v_rows bigint;
    -- Temp variable to capture ROW_COUNT
    v_lock_key bigint := 72002004;
BEGIN
    PERFORM
        bigbang_lock (v_lock_key);
    INSERT INTO corporations (name, owner_id, tag, description)
    SELECT
        'Imperial',
        NULL,
        'IMP',
        'Imperial faction'
    WHERE
        NOT EXISTS (
            SELECT
                1
            FROM
                corporations
            WHERE
                lower(name) = lower('Imperial'));
    -- Corrected: Capture row count first, then add to total
    GET DIAGNOSTICS v_rows = ROW_COUNT;
    v_added := v_added + v_rows;
    INSERT INTO corporations (name, owner_id, tag, description)
    SELECT
        'Ferringhi',
        NULL,
        'FER',
        'Ferringhi faction'
    WHERE
        NOT EXISTS (
            SELECT
                1
            FROM
                corporations
            WHERE
                lower(name) = lower('Ferringhi'));
    -- Corrected: Capture row count first, then add to total
    GET DIAGNOSTICS v_rows = ROW_COUNT;
    v_added := v_added + v_rows;
    PERFORM
        bigbang_unlock (v_lock_key);
    RETURN v_added;
END;
$$;


ALTER FUNCTION public.seed_factions() OWNER TO postgres;

--
-- Name: setup_ferringhi_alliance(); Type: FUNCTION; Schema: public; Owner: postgres
--

CREATE FUNCTION public.setup_ferringhi_alliance() RETURNS void
    LANGUAGE plpgsql
    AS $$
DECLARE
    v_ferringhi_sector bigint;
    v_ptype bigint;
    v_ferringhi_corp_id bigint;
BEGIN
    -- Get Ferringhi planet type
    SELECT planettypes_id INTO v_ptype
    FROM planettypes WHERE code = 'M' LIMIT 1;
    
    -- Get Ferringhi corporation ID
    SELECT corporation_id INTO v_ferringhi_corp_id
    FROM corporations WHERE lower(name) = lower('Ferringhi') LIMIT 1;
    
    -- Find the longest tunnel endpoint for Ferringhi placement
    SELECT COALESCE(exit_sector, 469) INTO v_ferringhi_sector
    FROM longest_tunnels LIMIT 1;
    
    -- If no tunnels exist, default to sector 469
    IF v_ferringhi_sector IS NULL OR v_ferringhi_sector = 0 THEN
        v_ferringhi_sector := 469;
    END IF;
    
    -- Create Ferringhi Homeworld planet
    INSERT INTO planets (num, sector_id, name, owner_id, owner_type, class, type, created_at, created_by)
    SELECT 2, v_ferringhi_sector, 'Ferringhi Homeworld', COALESCE(v_ferringhi_corp_id, 0), 
           'player', 'M', v_ptype, now(), 0
    ON CONFLICT DO NOTHING;
    
    -- Deploy defensive fighters and mines at Ferringhi sector
    INSERT INTO sector_assets (sector_id, owner_id, corporation_id, asset_type, offensive_setting, quantity, deployed_at)
    SELECT v_ferringhi_sector, NULL, COALESCE(v_ferringhi_corp_id, 2), 2, 3, 50000, now()
    ON CONFLICT DO NOTHING;
    
    INSERT INTO sector_assets (sector_id, owner_id, corporation_id, asset_type, quantity, deployed_at)
    SELECT v_ferringhi_sector, NULL, COALESCE(v_ferringhi_corp_id, 2), 1, 250, now()
    ON CONFLICT DO NOTHING;
END;
$$;


ALTER FUNCTION public.setup_ferringhi_alliance() OWNER TO postgres;

--
-- Name: setup_npc_homeworlds(); Type: FUNCTION; Schema: public; Owner: postgres
--

CREATE FUNCTION public.setup_npc_homeworlds() RETURNS void
    LANGUAGE plpgsql
    AS $$
BEGIN
    -- Terra is always in sector 1, no need for dynamic placement
    INSERT INTO planets (num, sector_id, name, owner_id, owner_type, class, type, created_at, created_by)
    SELECT 1, 1, 'Terra', 0, 'player', 'M', planettypes_id, now(), 0
    FROM planettypes WHERE code = 'M' LIMIT 1;
END;
$$;


ALTER FUNCTION public.setup_npc_homeworlds() OWNER TO postgres;

--
-- Name: setup_orion_syndicate(); Type: FUNCTION; Schema: public; Owner: postgres
--

CREATE FUNCTION public.setup_orion_syndicate() RETURNS void
    LANGUAGE plpgsql
    AS $$
DECLARE
    v_orion_sector bigint;
    v_ptype bigint;
    v_orion_corp_id bigint;
    v_port_id bigint;
BEGIN
    -- Get Orion planet type
    SELECT planettypes_id INTO v_ptype
    FROM planettypes WHERE code = 'M' LIMIT 1;
    
    -- Get Orion Syndicate corporation ID
    SELECT corporation_id INTO v_orion_corp_id
    FROM corporations WHERE lower(name) = lower('Orion Syndicate') LIMIT 1;
    
    -- Find the 2nd longest tunnel endpoint for Orion placement (LIMIT 1 OFFSET 1)
    SELECT COALESCE(exit_sector, 386) INTO v_orion_sector
    FROM longest_tunnels LIMIT 1 OFFSET 1;
    
    -- If no second tunnel or default conflict, use random sector
    IF v_orion_sector IS NULL OR v_orion_sector = 0 THEN
        v_orion_sector := (random() * 989)::bigint + 11;
    END IF;
    
    -- Create Orion Hideout planet
    INSERT INTO planets (num, sector_id, name, owner_id, owner_type, class, type, created_at, created_by)
    SELECT 3, v_orion_sector, 'Orion Hideout', COALESCE(v_orion_corp_id, 0), 
           'player', 'M', v_ptype, now(), 0
    ON CONFLICT DO NOTHING;
    
    -- Deploy defensive fighters and mines at Orion sector
    INSERT INTO sector_assets (sector_id, owner_id, corporation_id, asset_type, offensive_setting, quantity, deployed_at)
    SELECT v_orion_sector, NULL, COALESCE(v_orion_corp_id, 1), 2, 2, 50000, now()
    ON CONFLICT DO NOTHING;
    
    INSERT INTO sector_assets (sector_id, owner_id, corporation_id, asset_type, quantity, deployed_at)
    SELECT v_orion_sector, NULL, COALESCE(v_orion_corp_id, 1), 1, 250, now()
    ON CONFLICT DO NOTHING;
    
    -- Create Orion Black Market port (type 10 = blackmarket stardock)
    INSERT INTO ports (sector_id, name, type)
    SELECT v_orion_sector, 'Orion Black Market', 10
    ON CONFLICT DO NOTHING;
END;
$$;


ALTER FUNCTION public.setup_orion_syndicate() OWNER TO postgres;

--
-- Name: ship_claim(bigint, bigint); Type: FUNCTION; Schema: public; Owner: postgres
--

CREATE FUNCTION public.ship_claim(p_player_id bigint, p_ship_id bigint) RETURNS public.db_result
    LANGUAGE plpgsql
    AS $$
DECLARE
  r db_result;
BEGIN
  r := (0, 'ok', NULL);

  PERFORM assert_column_exists('ships'::regclass, 'id');
  PERFORM assert_column_exists('players'::regclass, 'id');

  -- TODO (schema): required columns
  -- ships.owner_id, ships.destroyed (or status), players.active_ship_id
  PERFORM assert_column_exists('ships'::regclass, 'owner_id');
  PERFORM assert_column_exists('players'::regclass, 'active_ship_id');

  -- Lock rows
  PERFORM 1 FROM ships   WHERE id = p_ship_id   FOR UPDATE;
  IF NOT FOUND THEN
    RETURN (404, 'ship not found', NULL);
  END IF;

  PERFORM 1 FROM players WHERE id = p_player_id FOR UPDATE;
  IF NOT FOUND THEN
    RETURN (404, 'player not found', NULL);
  END IF;

  -- Ensure unowned
  IF EXISTS (SELECT 1 FROM ships WHERE id=p_ship_id AND owner_id IS NOT NULL AND owner_id <> 0) THEN
    RETURN (409, 'ship already owned', NULL);
  END IF;

  UPDATE ships SET owner_id = p_player_id WHERE id = p_ship_id;
  UPDATE players SET active_ship_id = p_ship_id WHERE id = p_player_id;

  RETURN r;
END;
$$;


ALTER FUNCTION public.ship_claim(p_player_id bigint, p_ship_id bigint) OWNER TO postgres;

--
-- Name: ship_create_initial(bigint, bigint); Type: FUNCTION; Schema: public; Owner: postgres
--

CREATE FUNCTION public.ship_create_initial(p_player_id bigint, p_sector_id bigint) RETURNS public.db_result
    LANGUAGE plpgsql
    AS $$
DECLARE
  v_ship_id bigint;
BEGIN
  PERFORM assert_column_exists('players'::regclass, 'id');
  PERFORM assert_column_exists('players'::regclass, 'active_ship_id');
  PERFORM assert_column_exists('ships'::regclass, 'id');
  PERFORM assert_column_exists('ships'::regclass, 'owner_id');
  PERFORM assert_column_exists('ships'::regclass, 'sector');

  PERFORM 1 FROM players WHERE id = p_player_id FOR UPDATE;
  IF NOT FOUND THEN
    RETURN (404, 'player not found', NULL);
  END IF;

  INSERT INTO ships (owner_id, sector)
  VALUES (p_player_id, p_sector_id)
  RETURNING id INTO v_ship_id;

  UPDATE players SET active_ship_id = v_ship_id WHERE id = p_player_id;

  RETURN (0, 'ok', v_ship_id);
END;
$$;


ALTER FUNCTION public.ship_create_initial(p_player_id bigint, p_sector_id bigint) OWNER TO postgres;

--
-- Name: ship_destroy(bigint); Type: FUNCTION; Schema: public; Owner: postgres
--

CREATE FUNCTION public.ship_destroy(p_ship_id bigint) RETURNS public.db_result
    LANGUAGE plpgsql
    AS $$
BEGIN
  PERFORM assert_column_exists('ships'::regclass, 'id');
  PERFORM assert_column_exists('ships'::regclass, 'destroyed');
  PERFORM assert_column_exists('players'::regclass, 'active_ship_id');

  PERFORM 1 FROM ships WHERE id = p_ship_id FOR UPDATE;
  IF NOT FOUND THEN
    RETURN (404, 'ship not found', NULL);
  END IF;

  UPDATE ships SET destroyed = 1 WHERE id = p_ship_id;
  UPDATE players SET active_ship_id = NULL WHERE active_ship_id = p_ship_id;

  RETURN (0, 'ok', NULL);
END;
$$;


ALTER FUNCTION public.ship_destroy(p_ship_id bigint) OWNER TO postgres;

--
-- Name: ship_update_cargo(bigint, text, bigint); Type: FUNCTION; Schema: public; Owner: postgres
--

CREATE FUNCTION public.ship_update_cargo(p_ship_id bigint, p_commodity text, p_delta bigint) RETURNS public.db_result
    LANGUAGE plpgsql
    AS $_$
DECLARE
  v_current_load bigint;
  v_holds        bigint;
  v_new_qty      bigint;
  v_total_new    bigint;
BEGIN
  -- Validate commodity column name to prevent injection
  IF p_commodity NOT IN ('ore','organics','equipment','colonists','slaves','weapons','drugs') THEN
    RETURN (400, 'invalid commodity', NULL);
  END IF;

  -- Lock and fetch
  SELECT (ore + organics + equipment + colonists + slaves + weapons + drugs), holds
  INTO v_current_load, v_holds
  FROM ships
  WHERE id = p_ship_id
  FOR UPDATE;

  IF NOT FOUND THEN
    RETURN (404, 'ship not found', NULL);
  END IF;

  -- Get current qty of target
  EXECUTE format('SELECT %I FROM ships WHERE id = $1', p_commodity)
  INTO v_new_qty
  USING p_ship_id;

  v_new_qty := v_new_qty + p_delta;
  v_total_new := v_current_load + p_delta;

  IF v_new_qty < 0 THEN
    RETURN (412, 'negative quantity', NULL);
  END IF;

  IF v_total_new > v_holds THEN
    RETURN (409, 'holds full', NULL);
  END IF;

  EXECUTE format('UPDATE ships SET %I = $1 WHERE id = $2', p_commodity)
  USING v_new_qty, p_ship_id;

  RETURN (0, 'ok', v_new_qty);
END;
$_$;


ALTER FUNCTION public.ship_update_cargo(p_ship_id bigint, p_commodity text, p_delta bigint) OWNER TO postgres;

--
-- Name: spawn_initial_fleet(); Type: FUNCTION; Schema: public; Owner: postgres
--

CREATE FUNCTION public.spawn_initial_fleet() RETURNS void
    LANGUAGE plpgsql
    AS $$
BEGIN
END;
$$;


ALTER FUNCTION public.spawn_initial_fleet() OWNER TO postgres;

SET default_tablespace = '';

SET default_table_access_method = heap;

--
-- Name: ai_economy_agents; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.ai_economy_agents (
    ai_economy_agents_id bigint NOT NULL,
    name text NOT NULL,
    role text NOT NULL,
    config_json text NOT NULL
);


ALTER TABLE public.ai_economy_agents OWNER TO postgres;

--
-- Name: ai_economy_agents_ai_economy_agents_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.ai_economy_agents_ai_economy_agents_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.ai_economy_agents_ai_economy_agents_id_seq OWNER TO postgres;

--
-- Name: ai_economy_agents_ai_economy_agents_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.ai_economy_agents_ai_economy_agents_id_seq OWNED BY public.ai_economy_agents.ai_economy_agents_id;


--
-- Name: alignment_band; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.alignment_band (
    alignment_band_id bigint NOT NULL,
    code text NOT NULL,
    name text NOT NULL,
    min_align bigint NOT NULL,
    max_align bigint NOT NULL,
    is_good boolean DEFAULT true NOT NULL,
    is_evil boolean DEFAULT false NOT NULL,
    can_buy_iss boolean DEFAULT true NOT NULL,
    can_rob_ports boolean DEFAULT false NOT NULL,
    notes text
);


ALTER TABLE public.alignment_band OWNER TO postgres;

--
-- Name: alignment_band_alignment_band_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.alignment_band_alignment_band_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.alignment_band_alignment_band_id_seq OWNER TO postgres;

--
-- Name: alignment_band_alignment_band_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.alignment_band_alignment_band_id_seq OWNED BY public.alignment_band.alignment_band_id;


--
-- Name: anomaly_reports; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.anomaly_reports (
    anomaly_reports_id bigint NOT NULL,
    ts timestamp with time zone DEFAULT CURRENT_TIMESTAMP NOT NULL,
    severity text NOT NULL,
    subject text NOT NULL,
    details text NOT NULL,
    resolved bigint DEFAULT 0 NOT NULL,
    CONSTRAINT anomaly_reports_resolved_check CHECK ((resolved = ANY (ARRAY[(0)::bigint, (1)::bigint]))),
    CONSTRAINT anomaly_reports_severity_check CHECK ((severity = ANY (ARRAY['low'::text, 'medium'::text, 'high'::text, 'critical'::text])))
);


ALTER TABLE public.anomaly_reports OWNER TO postgres;

--
-- Name: anomaly_reports_anomaly_reports_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.anomaly_reports_anomaly_reports_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.anomaly_reports_anomaly_reports_id_seq OWNER TO postgres;

--
-- Name: anomaly_reports_anomaly_reports_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.anomaly_reports_anomaly_reports_id_seq OWNED BY public.anomaly_reports.anomaly_reports_id;


--
-- Name: bank_accounts; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.bank_accounts (
    bank_accounts_id bigint NOT NULL,
    owner_type text NOT NULL,
    owner_id bigint NOT NULL,
    currency text DEFAULT 'CRD'::text NOT NULL,
    balance bigint DEFAULT 0 NOT NULL,
    interest_rate_bp bigint DEFAULT 0 NOT NULL,
    last_interest_tick bigint,
    tx_alert_threshold bigint DEFAULT 0,
    is_active bigint DEFAULT 1 NOT NULL,
    CONSTRAINT bank_accounts_balance_check CHECK ((balance >= 0))
);


ALTER TABLE public.bank_accounts OWNER TO postgres;

--
-- Name: bank_accounts_bank_accounts_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.bank_accounts_bank_accounts_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.bank_accounts_bank_accounts_id_seq OWNER TO postgres;

--
-- Name: bank_accounts_bank_accounts_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.bank_accounts_bank_accounts_id_seq OWNED BY public.bank_accounts.bank_accounts_id;


--
-- Name: bank_fee_schedules; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.bank_fee_schedules (
    bank_fee_schedules_id bigint NOT NULL,
    tx_type text NOT NULL,
    fee_code text NOT NULL,
    owner_type text,
    currency text DEFAULT 'CRD'::text NOT NULL,
    value bigint NOT NULL,
    is_percentage bigint DEFAULT 0 NOT NULL,
    min_tx_amount bigint DEFAULT 0,
    max_tx_amount bigint,
    effective_from bigint NOT NULL,
    effective_to bigint,
    CONSTRAINT bank_fee_schedules_is_percentage_check CHECK ((is_percentage = ANY (ARRAY[(0)::bigint, (1)::bigint])))
);


ALTER TABLE public.bank_fee_schedules OWNER TO postgres;

--
-- Name: bank_fee_schedules_bank_fee_schedules_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.bank_fee_schedules_bank_fee_schedules_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.bank_fee_schedules_bank_fee_schedules_id_seq OWNER TO postgres;

--
-- Name: bank_fee_schedules_bank_fee_schedules_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.bank_fee_schedules_bank_fee_schedules_id_seq OWNED BY public.bank_fee_schedules.bank_fee_schedules_id;


--
-- Name: bank_flags; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.bank_flags (
    player_id bigint NOT NULL,
    is_frozen bigint DEFAULT 0 NOT NULL,
    risk_tier text DEFAULT 'normal'::text NOT NULL,
    CONSTRAINT bank_flags_is_frozen_check CHECK ((is_frozen = ANY (ARRAY[(0)::bigint, (1)::bigint]))),
    CONSTRAINT bank_flags_risk_tier_check CHECK ((risk_tier = ANY (ARRAY['normal'::text, 'elevated'::text, 'high'::text, 'blocked'::text])))
);


ALTER TABLE public.bank_flags OWNER TO postgres;

--
-- Name: bank_flags_player_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.bank_flags_player_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.bank_flags_player_id_seq OWNER TO postgres;

--
-- Name: bank_flags_player_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.bank_flags_player_id_seq OWNED BY public.bank_flags.player_id;


--
-- Name: bank_interest_policy; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.bank_interest_policy (
    bank_interest_policy_id bigint NOT NULL,
    apr_bps bigint DEFAULT 0 NOT NULL,
    min_balance bigint DEFAULT 0 NOT NULL,
    max_balance bigint DEFAULT '9223372036854775807'::bigint NOT NULL,
    last_run_at timestamp with time zone,
    compounding text DEFAULT 'none'::text NOT NULL,
    currency text DEFAULT 'CRD'::text NOT NULL,
    CONSTRAINT bank_interest_policy_apr_bps_check CHECK ((apr_bps >= 0)),
    CONSTRAINT bank_interest_policy_bank_interest_policy_id_check CHECK ((bank_interest_policy_id = 1)),
    CONSTRAINT bank_interest_policy_min_balance_check CHECK ((min_balance >= 0))
);


ALTER TABLE public.bank_interest_policy OWNER TO postgres;

--
-- Name: bank_interest_policy_bank_interest_policy_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.bank_interest_policy_bank_interest_policy_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.bank_interest_policy_bank_interest_policy_id_seq OWNER TO postgres;

--
-- Name: bank_interest_policy_bank_interest_policy_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.bank_interest_policy_bank_interest_policy_id_seq OWNED BY public.bank_interest_policy.bank_interest_policy_id;


--
-- Name: bank_orders; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.bank_orders (
    bank_orders_id bigint NOT NULL,
    player_id bigint NOT NULL,
    kind text NOT NULL,
    schedule text NOT NULL,
    next_run_at timestamp with time zone,
    enabled bigint DEFAULT 1 NOT NULL,
    amount bigint NOT NULL,
    currency text DEFAULT 'CRD'::text NOT NULL,
    to_entity text NOT NULL,
    to_id bigint NOT NULL,
    memo text,
    CONSTRAINT bank_orders_amount_check CHECK ((amount > 0)),
    CONSTRAINT bank_orders_enabled_check CHECK ((enabled = ANY (ARRAY[(0)::bigint, (1)::bigint]))),
    CONSTRAINT bank_orders_kind_check CHECK ((kind = ANY (ARRAY['recurring'::text, 'once'::text]))),
    CONSTRAINT bank_orders_to_entity_check CHECK ((to_entity = ANY (ARRAY['player'::text, 'corp'::text, 'gov'::text, 'npc'::text])))
);


ALTER TABLE public.bank_orders OWNER TO postgres;

--
-- Name: bank_orders_bank_orders_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.bank_orders_bank_orders_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.bank_orders_bank_orders_id_seq OWNER TO postgres;

--
-- Name: bank_orders_bank_orders_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.bank_orders_bank_orders_id_seq OWNED BY public.bank_orders.bank_orders_id;


--
-- Name: bank_transactions; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.bank_transactions (
    bank_transactions_id bigint NOT NULL,
    account_id bigint NOT NULL,
    tx_type text NOT NULL,
    direction text NOT NULL,
    amount bigint NOT NULL,
    currency text NOT NULL,
    tx_group_id text,
    related_account_id bigint,
    description text,
    ts bigint NOT NULL,
    balance_after bigint DEFAULT 0,
    idempotency_key text,
    engine_event_id bigint,
    CONSTRAINT bank_transactions_amount_check CHECK ((amount > 0)),
    CONSTRAINT bank_transactions_direction_check CHECK ((direction = ANY (ARRAY['CREDIT'::text, 'DEBIT'::text]))),
    CONSTRAINT bank_transactions_tx_type_check CHECK ((tx_type = ANY (ARRAY['DEPOSIT'::text, 'WITHDRAWAL'::text, 'TRANSFER'::text, 'INTEREST'::text, 'FEE'::text, 'WIRE'::text, 'TAX'::text, 'TRADE_BUY'::text, 'TRADE_SELL'::text, 'TRADE_BUY_FEE'::text, 'TRADE_SELL_FEE'::text, 'WITHDRAWAL_FEE'::text, 'ADJUSTMENT'::text])))
);


ALTER TABLE public.bank_transactions OWNER TO postgres;

--
-- Name: bank_transactions_bank_transactions_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.bank_transactions_bank_transactions_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.bank_transactions_bank_transactions_id_seq OWNER TO postgres;

--
-- Name: bank_transactions_bank_transactions_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.bank_transactions_bank_transactions_id_seq OWNED BY public.bank_transactions.bank_transactions_id;


--
-- Name: sector_warps; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.sector_warps (
    from_sector bigint NOT NULL,
    to_sector bigint NOT NULL
);


ALTER TABLE public.sector_warps OWNER TO postgres;

--
-- Name: bidirectional_edges; Type: VIEW; Schema: public; Owner: postgres
--

CREATE VIEW public.bidirectional_edges AS
 SELECT s.from_sector,
    s.to_sector
   FROM (public.sector_warps s
     JOIN public.sector_warps r ON (((r.from_sector = s.to_sector) AND (r.to_sector = s.from_sector))))
  WHERE (s.from_sector < s.to_sector);


ALTER VIEW public.bidirectional_edges OWNER TO postgres;

--
-- Name: black_accounts; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.black_accounts (
    black_accounts_id bigint NOT NULL,
    owner_type text NOT NULL,
    owner_id bigint NOT NULL,
    balance bigint DEFAULT 0 NOT NULL,
    CONSTRAINT black_accounts_balance_check CHECK ((balance >= 0)),
    CONSTRAINT black_accounts_owner_type_check CHECK ((owner_type = ANY (ARRAY['player'::text, 'corp'::text, 'npc'::text])))
);


ALTER TABLE public.black_accounts OWNER TO postgres;

--
-- Name: black_accounts_black_accounts_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.black_accounts_black_accounts_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.black_accounts_black_accounts_id_seq OWNER TO postgres;

--
-- Name: black_accounts_black_accounts_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.black_accounts_black_accounts_id_seq OWNED BY public.black_accounts.black_accounts_id;


--
-- Name: bounties; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.bounties (
    bounties_id bigint NOT NULL,
    posted_by_type text NOT NULL,
    posted_by_id bigint,
    target_type text NOT NULL,
    target_id bigint NOT NULL,
    reward bigint NOT NULL,
    escrow_bank_tx bigint,
    status text DEFAULT 'open'::text NOT NULL,
    posted_ts timestamp with time zone DEFAULT CURRENT_TIMESTAMP NOT NULL,
    claimed_by bigint,
    paid_bank_tx bigint,
    CONSTRAINT bounties_posted_by_type_check CHECK ((posted_by_type = ANY (ARRAY['player'::text, 'corp'::text, 'gov'::text, 'npc'::text]))),
    CONSTRAINT bounties_reward_check CHECK ((reward >= 0)),
    CONSTRAINT bounties_status_check CHECK ((status = ANY (ARRAY['open'::text, 'claimed'::text, 'cancelled'::text, 'expired'::text]))),
    CONSTRAINT bounties_target_type_check CHECK ((target_type = ANY (ARRAY['player'::text, 'corp'::text, 'npc'::text])))
);


ALTER TABLE public.bounties OWNER TO postgres;

--
-- Name: bounties_bounties_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.bounties_bounties_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.bounties_bounties_id_seq OWNER TO postgres;

--
-- Name: bounties_bounties_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.bounties_bounties_id_seq OWNED BY public.bounties.bounties_id;


--
-- Name: charities; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.charities (
    charities_id bigint NOT NULL,
    name text NOT NULL,
    description text
);


ALTER TABLE public.charities OWNER TO postgres;

--
-- Name: charities_charities_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.charities_charities_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.charities_charities_id_seq OWNER TO postgres;

--
-- Name: charities_charities_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.charities_charities_id_seq OWNED BY public.charities.charities_id;


--
-- Name: charters; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.charters (
    charters_id bigint NOT NULL,
    name text NOT NULL,
    granted_by text DEFAULT 'federation'::text NOT NULL,
    monopoly_scope text,
    start_ts text NOT NULL,
    expiry_ts text
);


ALTER TABLE public.charters OWNER TO postgres;

--
-- Name: charters_charters_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.charters_charters_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.charters_charters_id_seq OWNER TO postgres;

--
-- Name: charters_charters_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.charters_charters_id_seq OWNED BY public.charters.charters_id;


--
-- Name: citadel_requirements; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.citadel_requirements (
    planet_type_id bigint NOT NULL,
    citadel_level bigint NOT NULL,
    ore_cost bigint DEFAULT 0 NOT NULL,
    organics_cost bigint DEFAULT 0 NOT NULL,
    equipment_cost bigint DEFAULT 0 NOT NULL,
    colonist_cost bigint DEFAULT 0 NOT NULL,
    time_cost_days bigint DEFAULT 0 NOT NULL
);


ALTER TABLE public.citadel_requirements OWNER TO postgres;

--
-- Name: citadels; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.citadels (
    citadel_id bigint NOT NULL,
    planet_id bigint NOT NULL,
    level bigint,
    treasury bigint,
    militaryreactionlevel bigint,
    qcannonatmosphere bigint,
    qcannonsector bigint,
    planetaryshields bigint,
    transporterlvl bigint,
    interdictor bigint,
    upgradepercent double precision,
    upgradestart bigint,
    owner_id bigint,
    shields bigint,
    torps bigint,
    fighters bigint,
    qtorps bigint,
    qcannon bigint,
    qcannontype bigint,
    qtorpstype bigint,
    military bigint,
    construction_start_time bigint DEFAULT 0,
    construction_end_time bigint DEFAULT 0,
    target_level bigint DEFAULT 0,
    construction_status text DEFAULT 'idle'::text
);


ALTER TABLE public.citadels OWNER TO postgres;

--
-- Name: citadels_citadel_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.citadels_citadel_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.citadels_citadel_id_seq OWNER TO postgres;

--
-- Name: citadels_citadel_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.citadels_citadel_id_seq OWNED BY public.citadels.citadel_id;


--
-- Name: cluster_commodity_index; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.cluster_commodity_index (
    cluster_id bigint NOT NULL,
    commodity_code text NOT NULL,
    mid_price bigint NOT NULL,
    last_updated text DEFAULT CURRENT_TIMESTAMP NOT NULL
);


ALTER TABLE public.cluster_commodity_index OWNER TO postgres;

--
-- Name: cluster_player_status; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.cluster_player_status (
    cluster_id bigint NOT NULL,
    player_id bigint NOT NULL,
    suspicion bigint DEFAULT 0 NOT NULL,
    bust_count bigint DEFAULT 0 NOT NULL,
    last_bust_at text,
    wanted_level bigint DEFAULT 0 NOT NULL,
    banned bigint DEFAULT 0 NOT NULL
);


ALTER TABLE public.cluster_player_status OWNER TO postgres;

--
-- Name: cluster_sectors; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.cluster_sectors (
    cluster_id bigint NOT NULL,
    sector_id bigint NOT NULL
);


ALTER TABLE public.cluster_sectors OWNER TO postgres;

--
-- Name: clusters; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.clusters (
    clusters_id bigint NOT NULL,
    name text NOT NULL,
    role text NOT NULL,
    kind text NOT NULL,
    center_sector bigint,
    law_severity bigint DEFAULT 1 NOT NULL,
    alignment bigint DEFAULT 0 NOT NULL,
    created_at timestamp with time zone DEFAULT CURRENT_TIMESTAMP NOT NULL
);


ALTER TABLE public.clusters OWNER TO postgres;

--
-- Name: clusters_clusters_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.clusters_clusters_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.clusters_clusters_id_seq OWNER TO postgres;

--
-- Name: clusters_clusters_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.clusters_clusters_id_seq OWNED BY public.clusters.clusters_id;


--
-- Name: collateral; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.collateral (
    collateral_id bigint NOT NULL,
    loan_id bigint NOT NULL,
    asset_type text NOT NULL,
    asset_id bigint NOT NULL,
    appraised_value bigint DEFAULT 0 NOT NULL,
    CONSTRAINT collateral_appraised_value_check CHECK ((appraised_value >= 0)),
    CONSTRAINT collateral_asset_type_check CHECK ((asset_type = ANY (ARRAY['ship'::text, 'planet'::text, 'cargo'::text, 'stock'::text, 'other'::text])))
);


ALTER TABLE public.collateral OWNER TO postgres;

--
-- Name: collateral_collateral_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.collateral_collateral_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.collateral_collateral_id_seq OWNER TO postgres;

--
-- Name: collateral_collateral_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.collateral_collateral_id_seq OWNED BY public.collateral.collateral_id;


--
-- Name: commission; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.commission (
    commission_id bigint NOT NULL,
    is_evil boolean DEFAULT false NOT NULL,
    min_exp bigint NOT NULL,
    description text NOT NULL,
    CONSTRAINT commission_is_evil_check CHECK ((is_evil = ANY (ARRAY[true, false])))
);


ALTER TABLE public.commission OWNER TO postgres;

--
-- Name: commission_commission_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.commission_commission_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.commission_commission_id_seq OWNER TO postgres;

--
-- Name: commission_commission_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.commission_commission_id_seq OWNED BY public.commission.commission_id;


--
-- Name: commodities; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.commodities (
    commodities_id bigint NOT NULL,
    code text NOT NULL,
    name text NOT NULL,
    illegal bigint DEFAULT 0 NOT NULL,
    base_price bigint DEFAULT 0 NOT NULL,
    volatility bigint DEFAULT 0 NOT NULL,
    CONSTRAINT commodities_base_price_check CHECK ((base_price >= 0)),
    CONSTRAINT commodities_volatility_check CHECK ((volatility >= 0))
);


ALTER TABLE public.commodities OWNER TO postgres;

--
-- Name: commodities_commodities_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.commodities_commodities_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.commodities_commodities_id_seq OWNER TO postgres;

--
-- Name: commodities_commodities_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.commodities_commodities_id_seq OWNED BY public.commodities.commodities_id;


--
-- Name: commodity_orders; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.commodity_orders (
    commodity_orders_id bigint NOT NULL,
    actor_type text NOT NULL,
    actor_id bigint NOT NULL,
    location_type text NOT NULL,
    location_id bigint NOT NULL,
    commodity_id bigint NOT NULL,
    side text NOT NULL,
    quantity bigint NOT NULL,
    filled_quantity bigint DEFAULT 0 NOT NULL,
    price bigint NOT NULL,
    status text DEFAULT 'open'::text NOT NULL,
    ts timestamp with time zone DEFAULT CURRENT_TIMESTAMP NOT NULL,
    expires_at timestamp with time zone,
    CONSTRAINT commodity_orders_actor_type_check CHECK ((actor_type = ANY (ARRAY['player'::text, 'corp'::text, 'npc_planet'::text, 'port'::text]))),
    CONSTRAINT commodity_orders_filled_quantity_check CHECK ((filled_quantity >= 0)),
    CONSTRAINT commodity_orders_location_type_check CHECK ((location_type = ANY (ARRAY['planet'::text, 'port'::text]))),
    CONSTRAINT commodity_orders_price_check CHECK ((price >= 0)),
    CONSTRAINT commodity_orders_quantity_check CHECK ((quantity > 0)),
    CONSTRAINT commodity_orders_side_check CHECK ((side = ANY (ARRAY['buy'::text, 'sell'::text]))),
    CONSTRAINT commodity_orders_status_check CHECK ((status = ANY (ARRAY['open'::text, 'filled'::text, 'cancelled'::text, 'expired'::text])))
);


ALTER TABLE public.commodity_orders OWNER TO postgres;

--
-- Name: commodity_orders_commodity_orders_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.commodity_orders_commodity_orders_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.commodity_orders_commodity_orders_id_seq OWNER TO postgres;

--
-- Name: commodity_orders_commodity_orders_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.commodity_orders_commodity_orders_id_seq OWNED BY public.commodity_orders.commodity_orders_id;


--
-- Name: commodity_trades; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.commodity_trades (
    id bigint NOT NULL,
    commodity_id bigint NOT NULL,
    buyer_actor_type text NOT NULL,
    buyer_actor_id bigint NOT NULL,
    buyer_location_type text NOT NULL,
    buyer_location_id bigint NOT NULL,
    seller_actor_type text NOT NULL,
    seller_actor_id bigint NOT NULL,
    seller_location_type text NOT NULL,
    seller_location_id bigint NOT NULL,
    quantity bigint NOT NULL,
    price bigint NOT NULL,
    ts timestamp with time zone DEFAULT CURRENT_TIMESTAMP NOT NULL,
    settlement_tx_buy bigint,
    settlement_tx_sell bigint,
    CONSTRAINT commodity_trades_buyer_actor_type_check CHECK ((buyer_actor_type = ANY (ARRAY['player'::text, 'corp'::text, 'npc_planet'::text, 'port'::text]))),
    CONSTRAINT commodity_trades_buyer_location_type_check CHECK ((buyer_location_type = ANY (ARRAY['planet'::text, 'port'::text]))),
    CONSTRAINT commodity_trades_price_check CHECK ((price >= 0)),
    CONSTRAINT commodity_trades_quantity_check CHECK ((quantity > 0)),
    CONSTRAINT commodity_trades_seller_actor_type_check CHECK ((seller_actor_type = ANY (ARRAY['player'::text, 'corp'::text, 'npc_planet'::text, 'port'::text]))),
    CONSTRAINT commodity_trades_seller_location_type_check CHECK ((seller_location_type = ANY (ARRAY['planet'::text, 'port'::text])))
);


ALTER TABLE public.commodity_trades OWNER TO postgres;

--
-- Name: commodity_trades_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.commodity_trades_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.commodity_trades_id_seq OWNER TO postgres;

--
-- Name: commodity_trades_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.commodity_trades_id_seq OWNED BY public.commodity_trades.id;


--
-- Name: config; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.config (
    key text NOT NULL,
    value text NOT NULL,
    type text NOT NULL,
    CONSTRAINT config_type_check CHECK ((type = ANY (ARRAY['int'::text, 'bool'::text, 'string'::text, 'double'::text])))
);


ALTER TABLE public.config OWNER TO postgres;

--
-- Name: contracts_illicit; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.contracts_illicit (
    contracts_illicit_id bigint NOT NULL,
    contractor_type text NOT NULL,
    contractor_id bigint NOT NULL,
    target_type text NOT NULL,
    target_id bigint NOT NULL,
    reward bigint NOT NULL,
    escrow_black_id bigint,
    status text DEFAULT 'open'::text NOT NULL,
    ts timestamp with time zone DEFAULT CURRENT_TIMESTAMP NOT NULL,
    CONSTRAINT contracts_illicit_contractor_type_check CHECK ((contractor_type = ANY (ARRAY['player'::text, 'corp'::text, 'npc'::text]))),
    CONSTRAINT contracts_illicit_reward_check CHECK ((reward >= 0)),
    CONSTRAINT contracts_illicit_status_check CHECK ((status = ANY (ARRAY['open'::text, 'fulfilled'::text, 'failed'::text, 'cancelled'::text]))),
    CONSTRAINT contracts_illicit_target_type_check CHECK ((target_type = ANY (ARRAY['player'::text, 'corp'::text, 'npc'::text])))
);


ALTER TABLE public.contracts_illicit OWNER TO postgres;

--
-- Name: contracts_illicit_contracts_illicit_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.contracts_illicit_contracts_illicit_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.contracts_illicit_contracts_illicit_id_seq OWNER TO postgres;

--
-- Name: contracts_illicit_contracts_illicit_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.contracts_illicit_contracts_illicit_id_seq OWNED BY public.contracts_illicit.contracts_illicit_id;


--
-- Name: corp_accounts; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.corp_accounts (
    corp_id bigint NOT NULL,
    currency text DEFAULT 'CRD'::text NOT NULL,
    balance bigint DEFAULT 0 NOT NULL,
    last_interest_at timestamp with time zone,
    CONSTRAINT corp_accounts_balance_check CHECK ((balance >= 0))
);


ALTER TABLE public.corp_accounts OWNER TO postgres;

--
-- Name: corp_accounts_corp_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.corp_accounts_corp_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.corp_accounts_corp_id_seq OWNER TO postgres;

--
-- Name: corp_accounts_corp_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.corp_accounts_corp_id_seq OWNED BY public.corp_accounts.corp_id;


--
-- Name: corp_interest_policy; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.corp_interest_policy (
    id bigint NOT NULL,
    apr_bps bigint DEFAULT 0 NOT NULL,
    compounding text DEFAULT 'none'::text NOT NULL,
    last_run_at timestamp with time zone DEFAULT CURRENT_TIMESTAMP NOT NULL,
    currency text DEFAULT 'CRD'::text NOT NULL,
    CONSTRAINT corp_interest_policy_apr_bps_check CHECK ((apr_bps >= 0)),
    CONSTRAINT corp_interest_policy_compounding_check CHECK ((compounding = ANY (ARRAY['none'::text, 'daily'::text, 'weekly'::text, 'monthly'::text]))),
    CONSTRAINT corp_interest_policy_id_check CHECK ((id = 1))
);


ALTER TABLE public.corp_interest_policy OWNER TO postgres;

--
-- Name: corp_interest_policy_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.corp_interest_policy_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.corp_interest_policy_id_seq OWNER TO postgres;

--
-- Name: corp_interest_policy_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.corp_interest_policy_id_seq OWNED BY public.corp_interest_policy.id;


--
-- Name: corp_invites; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.corp_invites (
    corp_invites_id bigint NOT NULL,
    corp_id bigint NOT NULL,
    player_id bigint NOT NULL,
    invited_at timestamp with time zone DEFAULT CURRENT_TIMESTAMP NOT NULL,
    expires_at timestamp with time zone DEFAULT CURRENT_TIMESTAMP NOT NULL
);


ALTER TABLE public.corp_invites OWNER TO postgres;

--
-- Name: corp_invites_corp_invites_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.corp_invites_corp_invites_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.corp_invites_corp_invites_id_seq OWNER TO postgres;

--
-- Name: corp_invites_corp_invites_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.corp_invites_corp_invites_id_seq OWNED BY public.corp_invites.corp_invites_id;


--
-- Name: corp_log; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.corp_log (
    id bigint NOT NULL,
    corporation_id bigint NOT NULL,
    actor_id bigint,
    event_type text NOT NULL,
    payload text NOT NULL,
    created_at timestamp with time zone DEFAULT CURRENT_TIMESTAMP NOT NULL
);


ALTER TABLE public.corp_log OWNER TO postgres;

--
-- Name: corp_log_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.corp_log_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.corp_log_id_seq OWNER TO postgres;

--
-- Name: corp_log_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.corp_log_id_seq OWNED BY public.corp_log.id;


--
-- Name: corp_mail; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.corp_mail (
    corp_mail_id bigint NOT NULL,
    corporation_id bigint NOT NULL,
    sender_id bigint,
    subject text,
    body text NOT NULL,
    posted_at timestamp with time zone DEFAULT CURRENT_TIMESTAMP NOT NULL
);


ALTER TABLE public.corp_mail OWNER TO postgres;

--
-- Name: corp_mail_corp_mail_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.corp_mail_corp_mail_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.corp_mail_corp_mail_id_seq OWNER TO postgres;

--
-- Name: corp_mail_corp_mail_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.corp_mail_corp_mail_id_seq OWNED BY public.corp_mail.corp_mail_id;


--
-- Name: corp_mail_cursors; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.corp_mail_cursors (
    corporation_id bigint NOT NULL,
    player_id bigint NOT NULL,
    last_seen_id bigint DEFAULT 0 NOT NULL
);


ALTER TABLE public.corp_mail_cursors OWNER TO postgres;

--
-- Name: corp_members; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.corp_members (
    corporation_id bigint NOT NULL,
    player_id bigint NOT NULL,
    role text DEFAULT 'Member'::text NOT NULL,
    join_date timestamp with time zone DEFAULT CURRENT_TIMESTAMP NOT NULL,
    CONSTRAINT corp_members_role_check CHECK ((role = ANY (ARRAY['Leader'::text, 'Officer'::text, 'Member'::text])))
);


ALTER TABLE public.corp_members OWNER TO postgres;

--
-- Name: corp_recruiting; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.corp_recruiting (
    corp_id bigint NOT NULL,
    tagline text NOT NULL,
    min_alignment bigint,
    play_style text,
    created_at timestamp with time zone DEFAULT CURRENT_TIMESTAMP NOT NULL,
    expires_at timestamp with time zone DEFAULT CURRENT_TIMESTAMP NOT NULL
);


ALTER TABLE public.corp_recruiting OWNER TO postgres;

--
-- Name: corp_recruiting_corp_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.corp_recruiting_corp_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.corp_recruiting_corp_id_seq OWNER TO postgres;

--
-- Name: corp_recruiting_corp_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.corp_recruiting_corp_id_seq OWNED BY public.corp_recruiting.corp_id;


--
-- Name: corp_shareholders; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.corp_shareholders (
    corp_id bigint NOT NULL,
    player_id bigint NOT NULL,
    shares bigint NOT NULL,
    CONSTRAINT corp_shareholders_shares_check CHECK ((shares >= 0))
);


ALTER TABLE public.corp_shareholders OWNER TO postgres;

--
-- Name: corp_tx; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.corp_tx (
    corp_tx_id bigint NOT NULL,
    corp_id bigint NOT NULL,
    ts timestamp with time zone DEFAULT CURRENT_TIMESTAMP NOT NULL,
    kind text NOT NULL,
    amount bigint NOT NULL,
    balance_after bigint,
    currency text DEFAULT 'CRD'::text NOT NULL,
    memo text,
    idempotency_key text,
    CONSTRAINT corp_tx_amount_check CHECK ((amount > 0)),
    CONSTRAINT corp_tx_kind_check CHECK ((kind = ANY (ARRAY['deposit'::text, 'withdraw'::text, 'transfer_in'::text, 'transfer_out'::text, 'interest'::text, 'dividend'::text, 'salary'::text, 'adjustment'::text])))
);


ALTER TABLE public.corp_tx OWNER TO postgres;

--
-- Name: corp_tx_corp_tx_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.corp_tx_corp_tx_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.corp_tx_corp_tx_id_seq OWNER TO postgres;

--
-- Name: corp_tx_corp_tx_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.corp_tx_corp_tx_id_seq OWNED BY public.corp_tx.corp_tx_id;


--
-- Name: corporations; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.corporations (
    corporation_id bigint NOT NULL,
    name text NOT NULL,
    owner_id bigint,
    tag text,
    description text,
    tax_arrears bigint DEFAULT 0 NOT NULL,
    credit_rating bigint DEFAULT 0 NOT NULL,
    created_at timestamp with time zone DEFAULT CURRENT_TIMESTAMP NOT NULL,
    updated_at timestamp with time zone DEFAULT CURRENT_TIMESTAMP NOT NULL,
    CONSTRAINT corporations_tag_check CHECK (((tag IS NULL) OR (((length(tag) >= 2) AND (length(tag) <= 5)) AND (tag ~ '^[A-Za-z0-9].*$'::text))))
);


ALTER TABLE public.corporations OWNER TO postgres;

--
-- Name: corporations_corporation_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.corporations_corporation_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.corporations_corporation_id_seq OWNER TO postgres;

--
-- Name: corporations_corporation_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.corporations_corporation_id_seq OWNED BY public.corporations.corporation_id;


--
-- Name: credit_ratings; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.credit_ratings (
    entity_type text NOT NULL,
    entity_id bigint NOT NULL,
    score bigint DEFAULT 600 NOT NULL,
    last_update text,
    CONSTRAINT credit_ratings_entity_type_check CHECK ((entity_type = ANY (ARRAY['player'::text, 'corp'::text]))),
    CONSTRAINT credit_ratings_score_check CHECK (((score >= 300) AND (score <= 900)))
);


ALTER TABLE public.credit_ratings OWNER TO postgres;

--
-- Name: cron_tasks; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.cron_tasks (
    cron_tasks_id bigint NOT NULL,
    name text NOT NULL,
    schedule text NOT NULL,
    last_run_at timestamp with time zone,
    next_due_at timestamp with time zone NOT NULL,
    enabled boolean DEFAULT true,
    payload text
);


ALTER TABLE public.cron_tasks OWNER TO postgres;

--
-- Name: cron_tasks_cron_tasks_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.cron_tasks_cron_tasks_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.cron_tasks_cron_tasks_id_seq OWNER TO postgres;

--
-- Name: cron_tasks_cron_tasks_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.cron_tasks_cron_tasks_id_seq OWNED BY public.cron_tasks.cron_tasks_id;


--
-- Name: cronjobs; Type: VIEW; Schema: public; Owner: postgres
--

CREATE VIEW public.cronjobs AS
 SELECT cron_tasks_id AS id,
    name,
    next_due_at AS next_due_utc,
    last_run_at AS last_run_utc
   FROM public.cron_tasks
  ORDER BY next_due_at;


ALTER VIEW public.cronjobs OWNER TO postgres;

--
-- Name: currencies; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.currencies (
    code text NOT NULL,
    name text NOT NULL,
    minor_unit bigint DEFAULT 1 NOT NULL,
    is_default boolean DEFAULT false NOT NULL,
    CONSTRAINT currencies_minor_unit_check CHECK ((minor_unit > 0))
);


ALTER TABLE public.currencies OWNER TO postgres;

--
-- Name: donations; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.donations (
    donations_id bigint NOT NULL,
    charity_id bigint NOT NULL,
    donor_type text NOT NULL,
    donor_id bigint NOT NULL,
    amount bigint NOT NULL,
    ts timestamp with time zone DEFAULT CURRENT_TIMESTAMP NOT NULL,
    bank_tx_id bigint,
    CONSTRAINT donations_amount_check CHECK ((amount >= 0)),
    CONSTRAINT donations_donor_type_check CHECK ((donor_type = ANY (ARRAY['player'::text, 'corp'::text])))
);


ALTER TABLE public.donations OWNER TO postgres;

--
-- Name: donations_donations_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.donations_donations_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.donations_donations_id_seq OWNER TO postgres;

--
-- Name: donations_donations_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.donations_donations_id_seq OWNED BY public.donations.donations_id;


--
-- Name: economic_indicators; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.economic_indicators (
    economic_indicators_id bigint NOT NULL,
    ts timestamp with time zone DEFAULT CURRENT_TIMESTAMP NOT NULL,
    inflation_bps bigint DEFAULT 0 NOT NULL,
    liquidity bigint DEFAULT 0 NOT NULL,
    credit_velocity double precision DEFAULT 0.0 NOT NULL
);


ALTER TABLE public.economic_indicators OWNER TO postgres;

--
-- Name: economic_indicators_economic_indicators_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.economic_indicators_economic_indicators_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.economic_indicators_economic_indicators_id_seq OWNER TO postgres;

--
-- Name: economic_indicators_economic_indicators_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.economic_indicators_economic_indicators_id_seq OWNED BY public.economic_indicators.economic_indicators_id;


--
-- Name: economy_curve; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.economy_curve (
    economy_curve_id bigint NOT NULL,
    curve_name text NOT NULL,
    base_restock_rate double precision NOT NULL,
    price_elasticity double precision NOT NULL,
    target_stock bigint NOT NULL,
    volatility_factor double precision NOT NULL
);


ALTER TABLE public.economy_curve OWNER TO postgres;

--
-- Name: economy_curve_economy_curve_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.economy_curve_economy_curve_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.economy_curve_economy_curve_id_seq OWNER TO postgres;

--
-- Name: economy_curve_economy_curve_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.economy_curve_economy_curve_id_seq OWNED BY public.economy_curve.economy_curve_id;


--
-- Name: economy_policies; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.economy_policies (
    economy_policies_id bigint NOT NULL,
    name text NOT NULL,
    config_json text NOT NULL,
    active bigint DEFAULT 1 NOT NULL,
    CONSTRAINT economy_policies_active_check CHECK ((active = ANY (ARRAY[(0)::bigint, (1)::bigint])))
);


ALTER TABLE public.economy_policies OWNER TO postgres;

--
-- Name: economy_policies_economy_policies_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.economy_policies_economy_policies_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.economy_policies_economy_policies_id_seq OWNER TO postgres;

--
-- Name: economy_policies_economy_policies_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.economy_policies_economy_policies_id_seq OWNED BY public.economy_policies.economy_policies_id;


--
-- Name: economy_snapshots; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.economy_snapshots (
    economy_snapshots_id bigint NOT NULL,
    ts timestamp with time zone DEFAULT CURRENT_TIMESTAMP NOT NULL,
    money_supply bigint DEFAULT 0 NOT NULL,
    total_deposits bigint DEFAULT 0 NOT NULL,
    total_loans bigint DEFAULT 0 NOT NULL,
    total_insured bigint DEFAULT 0 NOT NULL,
    notes text
);


ALTER TABLE public.economy_snapshots OWNER TO postgres;

--
-- Name: economy_snapshots_economy_snapshots_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.economy_snapshots_economy_snapshots_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.economy_snapshots_economy_snapshots_id_seq OWNER TO postgres;

--
-- Name: economy_snapshots_economy_snapshots_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.economy_snapshots_economy_snapshots_id_seq OWNED BY public.economy_snapshots.economy_snapshots_id;


--
-- Name: eligible_tows; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.eligible_tows (
    ship_id bigint NOT NULL,
    sector_id bigint,
    owner_id bigint,
    fighters bigint,
    alignment bigint,
    experience bigint
);


ALTER TABLE public.eligible_tows OWNER TO postgres;

--
-- Name: eligible_tows_ship_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.eligible_tows_ship_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.eligible_tows_ship_id_seq OWNER TO postgres;

--
-- Name: eligible_tows_ship_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.eligible_tows_ship_id_seq OWNED BY public.eligible_tows.ship_id;


--
-- Name: engine_audit; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.engine_audit (
    engine_audit_id bigint NOT NULL,
    ts bigint NOT NULL,
    cmd_type text NOT NULL,
    correlation_id text,
    actor_player_id bigint,
    details text
);


ALTER TABLE public.engine_audit OWNER TO postgres;

--
-- Name: engine_audit_engine_audit_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.engine_audit_engine_audit_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.engine_audit_engine_audit_id_seq OWNER TO postgres;

--
-- Name: engine_audit_engine_audit_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.engine_audit_engine_audit_id_seq OWNED BY public.engine_audit.engine_audit_id;


--
-- Name: engine_commands; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.engine_commands (
    engine_commands_id bigint NOT NULL,
    type text NOT NULL,
    payload text NOT NULL,
    status text DEFAULT 'ready'::text NOT NULL,
    priority bigint DEFAULT 100 NOT NULL,
    attempts bigint DEFAULT 0 NOT NULL,
    created_at timestamp with time zone DEFAULT CURRENT_TIMESTAMP NOT NULL,
    due_at timestamp with time zone DEFAULT CURRENT_TIMESTAMP NOT NULL,
    started_at timestamp with time zone,
    finished_at timestamp with time zone,
    worker text,
    idem_key text
);


ALTER TABLE public.engine_commands OWNER TO postgres;

--
-- Name: engine_commands_engine_commands_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.engine_commands_engine_commands_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.engine_commands_engine_commands_id_seq OWNER TO postgres;

--
-- Name: engine_commands_engine_commands_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.engine_commands_engine_commands_id_seq OWNED BY public.engine_commands.engine_commands_id;


--
-- Name: engine_events; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.engine_events (
    engine_events_id bigint NOT NULL,
    ts timestamp with time zone NOT NULL,
    type text NOT NULL,
    actor_player_id bigint,
    sector_id bigint,
    payload text NOT NULL,
    idem_key text,
    processed_at timestamp with time zone
);


ALTER TABLE public.engine_events OWNER TO postgres;

--
-- Name: engine_events_deadletter; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.engine_events_deadletter (
    engine_events_deadletter_id bigint NOT NULL,
    ts timestamp with time zone NOT NULL,
    type text NOT NULL,
    payload text NOT NULL,
    error text NOT NULL,
    moved_at timestamp with time zone DEFAULT CURRENT_TIMESTAMP NOT NULL
);


ALTER TABLE public.engine_events_deadletter OWNER TO postgres;

--
-- Name: engine_events_deadletter_engine_events_deadletter_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.engine_events_deadletter_engine_events_deadletter_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.engine_events_deadletter_engine_events_deadletter_id_seq OWNER TO postgres;

--
-- Name: engine_events_deadletter_engine_events_deadletter_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.engine_events_deadletter_engine_events_deadletter_id_seq OWNED BY public.engine_events_deadletter.engine_events_deadletter_id;


--
-- Name: engine_events_engine_events_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.engine_events_engine_events_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.engine_events_engine_events_id_seq OWNER TO postgres;

--
-- Name: engine_events_engine_events_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.engine_events_engine_events_id_seq OWNED BY public.engine_events.engine_events_id;


--
-- Name: engine_offset; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.engine_offset (
    key text NOT NULL,
    last_event_id bigint NOT NULL,
    last_event_ts timestamp with time zone NOT NULL
);


ALTER TABLE public.engine_offset OWNER TO postgres;

--
-- Name: engine_state; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.engine_state (
    state_key text NOT NULL,
    state_val text NOT NULL
);


ALTER TABLE public.engine_state OWNER TO postgres;

--
-- Name: entity_stock; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.entity_stock (
    entity_type text NOT NULL,
    entity_id bigint NOT NULL,
    commodity_code text NOT NULL,
    quantity bigint NOT NULL,
    price bigint,
    last_updated_ts bigint DEFAULT (EXTRACT(epoch FROM now()))::bigint NOT NULL,
    CONSTRAINT entity_stock_entity_type_check CHECK ((entity_type = ANY (ARRAY['port'::text, 'planet'::text])))
);


ALTER TABLE public.entity_stock OWNER TO postgres;

--
-- Name: event_triggers; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.event_triggers (
    event_triggers_id bigint NOT NULL,
    name text NOT NULL,
    condition_json text NOT NULL,
    action_json text NOT NULL
);


ALTER TABLE public.event_triggers OWNER TO postgres;

--
-- Name: event_triggers_event_triggers_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.event_triggers_event_triggers_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.event_triggers_event_triggers_id_seq OWNER TO postgres;

--
-- Name: event_triggers_event_triggers_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.event_triggers_event_triggers_id_seq OWNED BY public.event_triggers.event_triggers_id;


--
-- Name: expedition_backers; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.expedition_backers (
    expedition_id bigint NOT NULL,
    backer_type text NOT NULL,
    backer_id bigint NOT NULL,
    pledged_amount bigint NOT NULL,
    share_pct double precision NOT NULL,
    CONSTRAINT expedition_backers_backer_type_check CHECK ((backer_type = ANY (ARRAY['player'::text, 'corp'::text]))),
    CONSTRAINT expedition_backers_pledged_amount_check CHECK ((pledged_amount >= 0)),
    CONSTRAINT expedition_backers_share_pct_check CHECK ((share_pct >= (0)::double precision))
);


ALTER TABLE public.expedition_backers OWNER TO postgres;

--
-- Name: expedition_returns; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.expedition_returns (
    expedition_returns_id bigint NOT NULL,
    expedition_id bigint NOT NULL,
    ts timestamp with time zone DEFAULT CURRENT_TIMESTAMP NOT NULL,
    amount bigint NOT NULL,
    bank_tx_id bigint,
    CONSTRAINT expedition_returns_amount_check CHECK ((amount >= 0))
);


ALTER TABLE public.expedition_returns OWNER TO postgres;

--
-- Name: expedition_returns_expedition_returns_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.expedition_returns_expedition_returns_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.expedition_returns_expedition_returns_id_seq OWNER TO postgres;

--
-- Name: expedition_returns_expedition_returns_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.expedition_returns_expedition_returns_id_seq OWNED BY public.expedition_returns.expedition_returns_id;


--
-- Name: expeditions; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.expeditions (
    expeditions_id bigint NOT NULL,
    leader_player_id bigint NOT NULL,
    charter_id bigint,
    goal text NOT NULL,
    target_region text,
    pledged_total bigint DEFAULT 0 NOT NULL,
    duration_days bigint DEFAULT 7 NOT NULL,
    status text DEFAULT 'planning'::text NOT NULL,
    created_ts timestamp with time zone DEFAULT CURRENT_TIMESTAMP NOT NULL,
    CONSTRAINT expeditions_duration_days_check CHECK ((duration_days > 0)),
    CONSTRAINT expeditions_pledged_total_check CHECK ((pledged_total >= 0)),
    CONSTRAINT expeditions_status_check CHECK ((status = ANY (ARRAY['planning'::text, 'launched'::text, 'complete'::text, 'failed'::text, 'aborted'::text])))
);


ALTER TABLE public.expeditions OWNER TO postgres;

--
-- Name: expeditions_expeditions_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.expeditions_expeditions_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.expeditions_expeditions_id_seq OWNER TO postgres;

--
-- Name: expeditions_expeditions_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.expeditions_expeditions_id_seq OWNED BY public.expeditions.expeditions_id;


--
-- Name: fences; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.fences (
    fences_id bigint NOT NULL,
    npc_id bigint,
    sector_id bigint,
    reputation bigint DEFAULT 0 NOT NULL
);


ALTER TABLE public.fences OWNER TO postgres;

--
-- Name: fences_fences_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.fences_fences_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.fences_fences_id_seq OWNER TO postgres;

--
-- Name: fences_fences_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.fences_fences_id_seq OWNED BY public.fences.fences_id;


--
-- Name: fines; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.fines (
    fines_id bigint NOT NULL,
    issued_by text DEFAULT 'federation'::text NOT NULL,
    recipient_type text NOT NULL,
    recipient_id bigint NOT NULL,
    reason text,
    amount bigint NOT NULL,
    status text DEFAULT 'unpaid'::text NOT NULL,
    issued_ts timestamp with time zone DEFAULT CURRENT_TIMESTAMP NOT NULL,
    paid_bank_tx bigint,
    CONSTRAINT fines_amount_check CHECK ((amount >= 0)),
    CONSTRAINT fines_recipient_type_check CHECK ((recipient_type = ANY (ARRAY['player'::text, 'corp'::text]))),
    CONSTRAINT fines_status_check CHECK ((status = ANY (ARRAY['unpaid'::text, 'paid'::text, 'void'::text])))
);


ALTER TABLE public.fines OWNER TO postgres;

--
-- Name: fines_fines_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.fines_fines_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.fines_fines_id_seq OWNER TO postgres;

--
-- Name: fines_fines_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.fines_fines_id_seq OWNED BY public.fines.fines_id;


--
-- Name: futures_contracts; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.futures_contracts (
    futures_contracts_id bigint NOT NULL,
    commodity_id bigint NOT NULL,
    buyer_type text NOT NULL,
    buyer_id bigint NOT NULL,
    seller_type text NOT NULL,
    seller_id bigint NOT NULL,
    strike_price bigint NOT NULL,
    expiry_ts text NOT NULL,
    status text DEFAULT 'open'::text NOT NULL,
    CONSTRAINT futures_contracts_buyer_type_check CHECK ((buyer_type = ANY (ARRAY['player'::text, 'corp'::text]))),
    CONSTRAINT futures_contracts_seller_type_check CHECK ((seller_type = ANY (ARRAY['player'::text, 'corp'::text]))),
    CONSTRAINT futures_contracts_status_check CHECK ((status = ANY (ARRAY['open'::text, 'settled'::text, 'defaulted'::text, 'cancelled'::text]))),
    CONSTRAINT futures_contracts_strike_price_check CHECK ((strike_price >= 0))
);


ALTER TABLE public.futures_contracts OWNER TO postgres;

--
-- Name: futures_contracts_futures_contracts_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.futures_contracts_futures_contracts_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.futures_contracts_futures_contracts_id_seq OWNER TO postgres;

--
-- Name: futures_contracts_futures_contracts_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.futures_contracts_futures_contracts_id_seq OWNED BY public.futures_contracts.futures_contracts_id;


--
-- Name: gov_accounts; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.gov_accounts (
    gov_accounts_id bigint NOT NULL,
    name text NOT NULL,
    balance bigint DEFAULT 0 NOT NULL,
    CONSTRAINT gov_accounts_balance_check CHECK ((balance >= 0))
);


ALTER TABLE public.gov_accounts OWNER TO postgres;

--
-- Name: gov_accounts_gov_accounts_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.gov_accounts_gov_accounts_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.gov_accounts_gov_accounts_id_seq OWNER TO postgres;

--
-- Name: gov_accounts_gov_accounts_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.gov_accounts_gov_accounts_id_seq OWNED BY public.gov_accounts.gov_accounts_id;


--
-- Name: grants; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.grants (
    grants_id bigint NOT NULL,
    name text NOT NULL,
    recipient_type text NOT NULL,
    recipient_id bigint NOT NULL,
    amount bigint NOT NULL,
    awarded_ts timestamp with time zone DEFAULT CURRENT_TIMESTAMP NOT NULL,
    bank_tx_id bigint,
    CONSTRAINT grants_amount_check CHECK ((amount >= 0)),
    CONSTRAINT grants_recipient_type_check CHECK ((recipient_type = ANY (ARRAY['player'::text, 'corp'::text])))
);


ALTER TABLE public.grants OWNER TO postgres;

--
-- Name: grants_grants_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.grants_grants_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.grants_grants_id_seq OWNER TO postgres;

--
-- Name: grants_grants_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.grants_grants_id_seq OWNED BY public.grants.grants_id;


--
-- Name: guild_dues; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.guild_dues (
    guild_dues_id bigint NOT NULL,
    guild_id bigint NOT NULL,
    amount bigint NOT NULL,
    period text DEFAULT 'monthly'::text NOT NULL,
    CONSTRAINT guild_dues_amount_check CHECK ((amount >= 0)),
    CONSTRAINT guild_dues_period_check CHECK ((period = ANY (ARRAY['weekly'::text, 'monthly'::text, 'quarterly'::text, 'yearly'::text])))
);


ALTER TABLE public.guild_dues OWNER TO postgres;

--
-- Name: guild_dues_guild_dues_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.guild_dues_guild_dues_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.guild_dues_guild_dues_id_seq OWNER TO postgres;

--
-- Name: guild_dues_guild_dues_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.guild_dues_guild_dues_id_seq OWNED BY public.guild_dues.guild_dues_id;


--
-- Name: guild_memberships; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.guild_memberships (
    guild_id bigint NOT NULL,
    member_type text NOT NULL,
    member_id bigint NOT NULL,
    role text DEFAULT 'member'::text NOT NULL,
    CONSTRAINT guild_memberships_member_type_check CHECK ((member_type = ANY (ARRAY['player'::text, 'corp'::text])))
);


ALTER TABLE public.guild_memberships OWNER TO postgres;

--
-- Name: guilds; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.guilds (
    guilds_id bigint NOT NULL,
    name text NOT NULL,
    description text
);


ALTER TABLE public.guilds OWNER TO postgres;

--
-- Name: guilds_guilds_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.guilds_guilds_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.guilds_guilds_id_seq OWNER TO postgres;

--
-- Name: guilds_guilds_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.guilds_guilds_id_seq OWNED BY public.guilds.guilds_id;


--
-- Name: hardware_items; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.hardware_items (
    hardware_items_id bigint NOT NULL,
    code text NOT NULL,
    name text NOT NULL,
    price bigint NOT NULL,
    requires_stardock boolean DEFAULT true,
    sold_in_class0 boolean DEFAULT true,
    max_per_ship bigint,
    category text NOT NULL,
    enabled boolean DEFAULT false
);


ALTER TABLE public.hardware_items OWNER TO postgres;

--
-- Name: hardware_items_hardware_items_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.hardware_items_hardware_items_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.hardware_items_hardware_items_id_seq OWNER TO postgres;

--
-- Name: hardware_items_hardware_items_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.hardware_items_hardware_items_id_seq OWNED BY public.hardware_items.hardware_items_id;


--
-- Name: idempotency; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.idempotency (
    key text NOT NULL,
    cmd text NOT NULL,
    req_fp text NOT NULL,
    response text,
    created_at timestamp with time zone DEFAULT CURRENT_TIMESTAMP NOT NULL,
    updated_at timestamp with time zone DEFAULT CURRENT_TIMESTAMP NOT NULL
);


ALTER TABLE public.idempotency OWNER TO postgres;

--
-- Name: insurance_claims; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.insurance_claims (
    insurance_claims_id bigint NOT NULL,
    policy_id bigint NOT NULL,
    event_id text,
    amount bigint NOT NULL,
    status text DEFAULT 'open'::text NOT NULL,
    ts timestamp with time zone DEFAULT CURRENT_TIMESTAMP NOT NULL,
    paid_bank_tx bigint,
    CONSTRAINT insurance_claims_amount_check CHECK ((amount >= 0)),
    CONSTRAINT insurance_claims_status_check CHECK ((status = ANY (ARRAY['open'::text, 'paid'::text, 'denied'::text])))
);


ALTER TABLE public.insurance_claims OWNER TO postgres;

--
-- Name: insurance_claims_insurance_claims_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.insurance_claims_insurance_claims_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.insurance_claims_insurance_claims_id_seq OWNER TO postgres;

--
-- Name: insurance_claims_insurance_claims_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.insurance_claims_insurance_claims_id_seq OWNED BY public.insurance_claims.insurance_claims_id;


--
-- Name: insurance_funds; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.insurance_funds (
    fund_id bigint NOT NULL,
    owner_type text NOT NULL,
    owner_id bigint,
    balance bigint DEFAULT 0 NOT NULL,
    CONSTRAINT insurance_funds_balance_check CHECK ((balance >= 0)),
    CONSTRAINT insurance_funds_owner_type_check CHECK ((owner_type = ANY (ARRAY['system'::text, 'corp'::text, 'player'::text])))
);


ALTER TABLE public.insurance_funds OWNER TO postgres;

--
-- Name: insurance_funds_fund_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.insurance_funds_fund_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.insurance_funds_fund_id_seq OWNER TO postgres;

--
-- Name: insurance_funds_fund_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.insurance_funds_fund_id_seq OWNED BY public.insurance_funds.fund_id;


--
-- Name: insurance_policies; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.insurance_policies (
    insurance_policies_id bigint NOT NULL,
    holder_type text NOT NULL,
    holder_id bigint NOT NULL,
    subject_type text NOT NULL,
    subject_id bigint NOT NULL,
    premium bigint NOT NULL,
    payout bigint NOT NULL,
    fund_id bigint,
    start_ts text NOT NULL,
    expiry_ts text,
    active bigint DEFAULT 1 NOT NULL,
    CONSTRAINT insurance_policies_active_check CHECK ((active = ANY (ARRAY[(0)::bigint, (1)::bigint]))),
    CONSTRAINT insurance_policies_holder_type_check CHECK ((holder_type = ANY (ARRAY['player'::text, 'corp'::text]))),
    CONSTRAINT insurance_policies_payout_check CHECK ((payout >= 0)),
    CONSTRAINT insurance_policies_premium_check CHECK ((premium >= 0)),
    CONSTRAINT insurance_policies_subject_type_check CHECK ((subject_type = ANY (ARRAY['ship'::text, 'cargo'::text, 'planet'::text])))
);


ALTER TABLE public.insurance_policies OWNER TO postgres;

--
-- Name: insurance_policies_insurance_policies_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.insurance_policies_insurance_policies_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.insurance_policies_insurance_policies_id_seq OWNER TO postgres;

--
-- Name: insurance_policies_insurance_policies_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.insurance_policies_insurance_policies_id_seq OWNED BY public.insurance_policies.insurance_policies_id;


--
-- Name: laundering_ops; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.laundering_ops (
    laundering_ops_id bigint NOT NULL,
    from_black_id bigint,
    to_player_id bigint,
    amount bigint NOT NULL,
    risk_pct bigint DEFAULT 25 NOT NULL,
    status text DEFAULT 'pending'::text NOT NULL,
    ts timestamp with time zone DEFAULT CURRENT_TIMESTAMP NOT NULL,
    CONSTRAINT laundering_ops_amount_check CHECK ((amount > 0)),
    CONSTRAINT laundering_ops_risk_pct_check CHECK (((risk_pct >= 0) AND (risk_pct <= 100))),
    CONSTRAINT laundering_ops_status_check CHECK ((status = ANY (ARRAY['pending'::text, 'cleaned'::text, 'seized'::text, 'failed'::text])))
);


ALTER TABLE public.laundering_ops OWNER TO postgres;

--
-- Name: laundering_ops_laundering_ops_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.laundering_ops_laundering_ops_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.laundering_ops_laundering_ops_id_seq OWNER TO postgres;

--
-- Name: laundering_ops_laundering_ops_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.laundering_ops_laundering_ops_id_seq OWNED BY public.laundering_ops.laundering_ops_id;


--
-- Name: law_enforcement; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.law_enforcement (
    law_enforcement_id bigint NOT NULL,
    robbery_evil_threshold bigint DEFAULT '-10'::integer,
    robbery_xp_per_hold bigint DEFAULT 20,
    robbery_credits_per_xp bigint DEFAULT 10,
    robbery_bust_chance_base double precision DEFAULT 0.05,
    robbery_turn_cost bigint DEFAULT 1,
    good_guy_bust_bonus double precision DEFAULT 0.10,
    pro_criminal_bust_delta double precision DEFAULT '-0.02'::numeric,
    evil_cluster_bust_bonus double precision DEFAULT 0.05,
    good_align_penalty_mult double precision DEFAULT 3.0,
    robbery_real_bust_ttl_days bigint DEFAULT 7,
    CONSTRAINT law_enforcement_law_enforcement_id_check CHECK ((law_enforcement_id = 1))
);


ALTER TABLE public.law_enforcement OWNER TO postgres;

--
-- Name: law_enforcement_law_enforcement_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.law_enforcement_law_enforcement_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.law_enforcement_law_enforcement_id_seq OWNER TO postgres;

--
-- Name: law_enforcement_law_enforcement_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.law_enforcement_law_enforcement_id_seq OWNED BY public.law_enforcement.law_enforcement_id;


--
-- Name: limpet_attached; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.limpet_attached (
    limpet_attached_id bigint NOT NULL,
    ship_id bigint NOT NULL,
    owner_player_id bigint NOT NULL,
    created_ts timestamp with time zone NOT NULL
);


ALTER TABLE public.limpet_attached OWNER TO postgres;

--
-- Name: limpet_attached_limpet_attached_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.limpet_attached_limpet_attached_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.limpet_attached_limpet_attached_id_seq OWNER TO postgres;

--
-- Name: limpet_attached_limpet_attached_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.limpet_attached_limpet_attached_id_seq OWNED BY public.limpet_attached.limpet_attached_id;


--
-- Name: loan_payments; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.loan_payments (
    loan_payments_id bigint NOT NULL,
    loan_id bigint NOT NULL,
    ts timestamp with time zone DEFAULT CURRENT_TIMESTAMP NOT NULL,
    amount bigint NOT NULL,
    status text DEFAULT 'posted'::text NOT NULL,
    bank_tx_id bigint,
    CONSTRAINT loan_payments_amount_check CHECK ((amount > 0)),
    CONSTRAINT loan_payments_status_check CHECK ((status = ANY (ARRAY['posted'::text, 'reversed'::text])))
);


ALTER TABLE public.loan_payments OWNER TO postgres;

--
-- Name: loan_payments_loan_payments_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.loan_payments_loan_payments_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.loan_payments_loan_payments_id_seq OWNER TO postgres;

--
-- Name: loan_payments_loan_payments_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.loan_payments_loan_payments_id_seq OWNED BY public.loan_payments.loan_payments_id;


--
-- Name: loans; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.loans (
    loans_id bigint NOT NULL,
    lender_type text NOT NULL,
    lender_id bigint,
    borrower_type text NOT NULL,
    borrower_id bigint NOT NULL,
    principal bigint NOT NULL,
    rate_bps bigint DEFAULT 0 NOT NULL,
    term_days bigint NOT NULL,
    next_due text,
    status text DEFAULT 'active'::text NOT NULL,
    created_ts timestamp with time zone DEFAULT CURRENT_TIMESTAMP NOT NULL,
    CONSTRAINT loans_borrower_type_check CHECK ((borrower_type = ANY (ARRAY['player'::text, 'corp'::text]))),
    CONSTRAINT loans_lender_type_check CHECK ((lender_type = ANY (ARRAY['player'::text, 'corp'::text, 'bank'::text]))),
    CONSTRAINT loans_principal_check CHECK ((principal > 0)),
    CONSTRAINT loans_rate_bps_check CHECK ((rate_bps >= 0)),
    CONSTRAINT loans_status_check CHECK ((status = ANY (ARRAY['active'::text, 'paid'::text, 'defaulted'::text, 'written_off'::text]))),
    CONSTRAINT loans_term_days_check CHECK ((term_days > 0))
);


ALTER TABLE public.loans OWNER TO postgres;

--
-- Name: loans_loans_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.loans_loans_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.loans_loans_id_seq OWNER TO postgres;

--
-- Name: loans_loans_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.loans_loans_id_seq OWNED BY public.loans.loans_id;


--
-- Name: locks; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.locks (
    lock_name text NOT NULL,
    owner text,
    until_ms bigint
);


ALTER TABLE public.locks OWNER TO postgres;

--
-- Name: longest_tunnels; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.longest_tunnels (
    tunnel_id integer NOT NULL,
    entry_sector integer,
    exit_sector integer,
    tunnel_path text,
    tunnel_length_edges integer
);


ALTER TABLE public.longest_tunnels OWNER TO postgres;

--
-- Name: longest_tunnels_tunnel_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.longest_tunnels_tunnel_id_seq
    AS integer
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.longest_tunnels_tunnel_id_seq OWNER TO postgres;

--
-- Name: longest_tunnels_tunnel_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.longest_tunnels_tunnel_id_seq OWNED BY public.longest_tunnels.tunnel_id;


--
-- Name: mail; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.mail (
    mail_id bigint NOT NULL,
    thread_id bigint,
    sender_id bigint NOT NULL,
    recipient_id bigint NOT NULL,
    subject text,
    body text NOT NULL,
    sent_at timestamp with time zone DEFAULT CURRENT_TIMESTAMP NOT NULL,
    read_at timestamp with time zone DEFAULT CURRENT_TIMESTAMP NOT NULL,
    archived smallint DEFAULT 0 NOT NULL,
    deleted smallint DEFAULT 0 NOT NULL,
    idempotency_key text
);


ALTER TABLE public.mail OWNER TO postgres;

--
-- Name: mail_mail_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.mail_mail_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.mail_mail_id_seq OWNER TO postgres;

--
-- Name: mail_mail_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.mail_mail_id_seq OWNED BY public.mail.mail_id;


--
-- Name: msl_sectors; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.msl_sectors (
    sector_id bigint NOT NULL
);


ALTER TABLE public.msl_sectors OWNER TO postgres;

--
-- Name: msl_sectors_sector_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.msl_sectors_sector_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.msl_sectors_sector_id_seq OWNER TO postgres;

--
-- Name: msl_sectors_sector_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.msl_sectors_sector_id_seq OWNED BY public.msl_sectors.sector_id;


--
-- Name: news_feed; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.news_feed (
    news_id bigint NOT NULL,
    published_ts bigint NOT NULL,
    news_category text NOT NULL,
    article_text text NOT NULL,
    author_id bigint,
    source_ids text
);


ALTER TABLE public.news_feed OWNER TO postgres;

--
-- Name: news_feed_news_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.news_feed_news_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.news_feed_news_id_seq OWNER TO postgres;

--
-- Name: news_feed_news_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.news_feed_news_id_seq OWNED BY public.news_feed.news_id;


--
-- Name: notice_seen; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.notice_seen (
    notice_id bigint NOT NULL,
    player_id bigint NOT NULL,
    seen_at timestamp with time zone DEFAULT CURRENT_TIMESTAMP NOT NULL
);


ALTER TABLE public.notice_seen OWNER TO postgres;

--
-- Name: npc_shipnames; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.npc_shipnames (
    npc_shipnames_id bigint,
    name text
);


ALTER TABLE public.npc_shipnames OWNER TO postgres;

--
-- Name: one_way_edges; Type: VIEW; Schema: public; Owner: postgres
--

CREATE VIEW public.one_way_edges AS
 SELECT s.from_sector,
    s.to_sector
   FROM (public.sector_warps s
     LEFT JOIN public.sector_warps r ON (((r.from_sector = s.to_sector) AND (r.to_sector = s.from_sector))))
  WHERE (r.from_sector IS NULL);


ALTER VIEW public.one_way_edges OWNER TO postgres;

--
-- Name: planets; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.planets (
    planet_id bigint NOT NULL,
    num bigint,
    sector_id bigint NOT NULL,
    name text NOT NULL,
    owner_id bigint NOT NULL,
    owner_type text DEFAULT 'player'::text NOT NULL,
    class text DEFAULT 'M'::text NOT NULL,
    population bigint,
    type bigint,
    creator text,
    colonist bigint,
    fighters bigint,
    created_at timestamp with time zone DEFAULT CURRENT_TIMESTAMP NOT NULL,
    created_by bigint NOT NULL,
    genesis_flag boolean,
    citadel_level bigint DEFAULT 0,
    ore_on_hand bigint DEFAULT 0 NOT NULL,
    organics_on_hand bigint DEFAULT 0 NOT NULL,
    equipment_on_hand bigint DEFAULT 0 NOT NULL,
    colonists_ore bigint DEFAULT 0 NOT NULL,
    colonists_org bigint DEFAULT 0 NOT NULL,
    colonists_eq bigint DEFAULT 0 NOT NULL,
    colonists_mil bigint DEFAULT 0 NOT NULL,
    colonists_unassigned bigint DEFAULT 0 NOT NULL,
    terraform_turns_left bigint DEFAULT 1 NOT NULL
);


ALTER TABLE public.planets OWNER TO postgres;

--
-- Name: players; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.players (
    player_id bigint NOT NULL,
    type bigint DEFAULT 2,
    number bigint,
    name text NOT NULL,
    passwd text NOT NULL,
    sector_id bigint DEFAULT 1,
    ship_id bigint,
    experience bigint DEFAULT 0,
    alignment bigint DEFAULT 0,
    commission_id bigint DEFAULT 1,
    credits bigint DEFAULT 1500,
    flags bigint,
    login_time timestamp with time zone DEFAULT CURRENT_TIMESTAMP NOT NULL,
    last_update timestamp with time zone DEFAULT CURRENT_TIMESTAMP NOT NULL,
    times_blown_up bigint DEFAULT 0 NOT NULL,
    intransit boolean,
    beginmove bigint,
    movingto bigint,
    loggedin timestamp with time zone DEFAULT CURRENT_TIMESTAMP NOT NULL,
    lastplanet bigint,
    score bigint,
    is_npc boolean DEFAULT false,
    last_news_read_timestamp timestamp with time zone DEFAULT CURRENT_TIMESTAMP NOT NULL
);


ALTER TABLE public.players OWNER TO postgres;

--
-- Name: planet_citadels; Type: VIEW; Schema: public; Owner: postgres
--

CREATE VIEW public.planet_citadels AS
 SELECT c.citadel_id,
    c.level AS citadel_level,
    p.planet_id,
    p.name AS planet_name,
    p.sector_id,
    c.owner_id,
    pl.name AS owner_name
   FROM ((public.citadels c
     JOIN public.planets p ON ((p.planet_id = c.planet_id)))
     LEFT JOIN public.players pl ON ((pl.player_id = c.owner_id)));


ALTER VIEW public.planet_citadels OWNER TO postgres;

--
-- Name: planet_goods; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.planet_goods (
    planet_id bigint NOT NULL,
    commodity text NOT NULL,
    quantity bigint DEFAULT 0 NOT NULL,
    max_capacity bigint NOT NULL,
    production_rate bigint NOT NULL,
    CONSTRAINT planet_goods_commodity_check CHECK ((commodity = ANY (ARRAY['ore'::text, 'organics'::text, 'equipment'::text, 'food'::text, 'fuel'::text])))
);


ALTER TABLE public.planet_goods OWNER TO postgres;

--
-- Name: planet_production; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.planet_production (
    planet_type_id bigint NOT NULL,
    commodity_code text NOT NULL,
    base_prod_rate bigint NOT NULL,
    base_cons_rate bigint NOT NULL
);


ALTER TABLE public.planet_production OWNER TO postgres;

--
-- Name: planets_planet_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.planets_planet_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.planets_planet_id_seq OWNER TO postgres;

--
-- Name: planets_planet_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.planets_planet_id_seq OWNED BY public.planets.planet_id;


--
-- Name: planettypes; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.planettypes (
    planettypes_id bigint NOT NULL,
    code text,
    typedescription text,
    typename text,
    citadelupgradetime_lvl1 bigint,
    citadelupgradetime_lvl2 bigint,
    citadelupgradetime_lvl3 bigint,
    citadelupgradetime_lvl4 bigint,
    citadelupgradetime_lvl5 bigint,
    citadelupgradetime_lvl6 bigint,
    citadelupgradeore_lvl1 bigint,
    citadelupgradeore_lvl2 bigint,
    citadelupgradeore_lvl3 bigint,
    citadelupgradeore_lvl4 bigint,
    citadelupgradeore_lvl5 bigint,
    citadelupgradeore_lvl6 bigint,
    citadelupgradeorganics_lvl1 bigint,
    citadelupgradeorganics_lvl2 bigint,
    citadelupgradeorganics_lvl3 bigint,
    citadelupgradeorganics_lvl4 bigint,
    citadelupgradeorganics_lvl5 bigint,
    citadelupgradeorganics_lvl6 bigint,
    citadelupgradeequipment_lvl1 bigint,
    citadelupgradeequipment_lvl2 bigint,
    citadelupgradeequipment_lvl3 bigint,
    citadelupgradeequipment_lvl4 bigint,
    citadelupgradeequipment_lvl5 bigint,
    citadelupgradeequipment_lvl6 bigint,
    citadelupgradecolonist_lvl1 bigint,
    citadelupgradecolonist_lvl2 bigint,
    citadelupgradecolonist_lvl3 bigint,
    citadelupgradecolonist_lvl4 bigint,
    citadelupgradecolonist_lvl5 bigint,
    citadelupgradecolonist_lvl6 bigint,
    maxcolonist_ore bigint,
    maxcolonist_organics bigint,
    maxcolonist_equipment bigint,
    fighters bigint,
    fuelproduction bigint,
    organicsproduction bigint,
    equipmentproduction bigint,
    fighterproduction bigint,
    maxore bigint,
    maxorganics bigint,
    maxequipment bigint,
    maxfighters bigint,
    breeding double precision,
    genesis_weight bigint DEFAULT 10 NOT NULL
);


ALTER TABLE public.planettypes OWNER TO postgres;

--
-- Name: planettypes_planettypes_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.planettypes_planettypes_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.planettypes_planettypes_id_seq OWNER TO postgres;

--
-- Name: planettypes_planettypes_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.planettypes_planettypes_id_seq OWNED BY public.planettypes.planettypes_id;


--
-- Name: player_avoid; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.player_avoid (
    player_id bigint NOT NULL,
    sector_id bigint NOT NULL,
    updated_at timestamp with time zone DEFAULT CURRENT_TIMESTAMP NOT NULL
);


ALTER TABLE public.player_avoid OWNER TO postgres;

--
-- Name: player_block; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.player_block (
    blocker_id bigint NOT NULL,
    blocked_id bigint NOT NULL,
    created_at timestamp with time zone DEFAULT CURRENT_TIMESTAMP NOT NULL
);


ALTER TABLE public.player_block OWNER TO postgres;

--
-- Name: player_bookmarks; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.player_bookmarks (
    player_bookmarks_id bigint NOT NULL,
    player_id bigint NOT NULL,
    name text NOT NULL,
    sector_id bigint NOT NULL,
    updated_at timestamp with time zone DEFAULT CURRENT_TIMESTAMP NOT NULL
);


ALTER TABLE public.player_bookmarks OWNER TO postgres;

--
-- Name: player_bookmarks_player_bookmarks_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.player_bookmarks_player_bookmarks_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.player_bookmarks_player_bookmarks_id_seq OWNER TO postgres;

--
-- Name: player_bookmarks_player_bookmarks_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.player_bookmarks_player_bookmarks_id_seq OWNED BY public.player_bookmarks.player_bookmarks_id;


--
-- Name: sectors; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.sectors (
    sector_id bigint NOT NULL,
    name text,
    beacon text,
    nebulae text
);


ALTER TABLE public.sectors OWNER TO postgres;

--
-- Name: ships; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.ships (
    ship_id bigint NOT NULL,
    name text NOT NULL,
    type_id bigint,
    attack bigint,
    holds bigint DEFAULT 1,
    mines bigint,
    limpets bigint,
    fighters bigint DEFAULT 1,
    genesis bigint,
    detonators bigint DEFAULT 0 NOT NULL,
    probes bigint DEFAULT 0 NOT NULL,
    photons bigint,
    sector_id bigint,
    shields bigint DEFAULT 1,
    installed_shields bigint DEFAULT 1,
    beacons bigint,
    colonists bigint,
    equipment bigint,
    organics bigint,
    ore bigint,
    slaves bigint DEFAULT 0,
    weapons bigint DEFAULT 0,
    drugs bigint DEFAULT 0,
    flags bigint,
    cloaking_devices bigint,
    has_transwarp bigint DEFAULT 0 NOT NULL,
    has_planet_scanner bigint DEFAULT 0 NOT NULL,
    has_long_range_scanner bigint DEFAULT 0 NOT NULL,
    cloaked timestamp without time zone,
    ported bigint,
    onplanet bigint,
    destroyed bigint DEFAULT 0,
    hull bigint DEFAULT 100 NOT NULL,
    perms bigint DEFAULT 731 NOT NULL,
    CONSTRAINT check_current_cargo_limit CHECK (((((colonists + equipment) + organics) + ore) <= holds))
);


ALTER TABLE public.ships OWNER TO postgres;

--
-- Name: shiptypes; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.shiptypes (
    shiptypes_id bigint NOT NULL,
    name text NOT NULL,
    basecost bigint,
    required_alignment bigint,
    required_commission bigint,
    required_experience bigint,
    maxattack bigint,
    initialholds bigint,
    maxholds bigint,
    maxfighters bigint,
    turns bigint,
    maxmines bigint,
    maxlimpets bigint,
    maxgenesis bigint,
    max_detonators bigint DEFAULT 0 NOT NULL,
    max_probes bigint DEFAULT 0 NOT NULL,
    can_transwarp boolean DEFAULT false,
    transportrange bigint,
    maxshields bigint,
    offense bigint,
    defense bigint,
    maxbeacons bigint,
    can_long_range_scan boolean DEFAULT false,
    can_planet_scan boolean DEFAULT false,
    maxphotons bigint,
    max_cloaks bigint DEFAULT 0 NOT NULL,
    can_purchase boolean DEFAULT true,
    has_escape_pod boolean DEFAULT false NOT NULL,
    enabled boolean DEFAULT true
);


ALTER TABLE public.shiptypes OWNER TO postgres;

--
-- Name: player_info_v1; Type: VIEW; Schema: public; Owner: postgres
--

CREATE VIEW public.player_info_v1 AS
 SELECT p.player_id,
    p.name AS player_name,
    p.number AS player_number,
    sh.sector_id,
    sctr.name AS sector_name,
    p.credits AS petty_cash,
    p.alignment,
    p.experience,
    p.ship_id AS ship_number,
    sh.ship_id,
    sh.name AS ship_name,
    sh.type_id AS ship_type_id,
    st.name AS ship_type_name,
    st.maxholds AS ship_holds_capacity,
    sh.holds AS ship_holds_current,
    sh.fighters AS ship_fighters,
    sh.mines AS ship_mines,
    sh.limpets AS ship_limpets,
    sh.genesis AS ship_genesis,
    sh.photons AS ship_photons,
    sh.beacons AS ship_beacons,
    sh.colonists AS ship_colonists,
    sh.equipment AS ship_equipment,
    sh.organics AS ship_organics,
    sh.ore AS ship_ore,
    sh.ported AS ship_ported,
    sh.onplanet AS ship_onplanet,
    (COALESCE(p.credits, (0)::bigint) + (COALESCE(sh.fighters, (0)::bigint) * 2)) AS approx_worth
   FROM (((public.players p
     LEFT JOIN public.ships sh ON ((sh.ship_id = p.ship_id)))
     LEFT JOIN public.shiptypes st ON ((st.shiptypes_id = sh.type_id)))
     LEFT JOIN public.sectors sctr ON ((sctr.sector_id = sh.sector_id)));


ALTER VIEW public.player_info_v1 OWNER TO postgres;

--
-- Name: player_last_rob; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.player_last_rob (
    player_id bigint NOT NULL,
    port_id bigint NOT NULL,
    last_attempt_at timestamp with time zone NOT NULL,
    was_success bigint NOT NULL
);


ALTER TABLE public.player_last_rob OWNER TO postgres;

--
-- Name: player_last_rob_player_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.player_last_rob_player_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.player_last_rob_player_id_seq OWNER TO postgres;

--
-- Name: player_last_rob_player_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.player_last_rob_player_id_seq OWNED BY public.player_last_rob.player_id;


--
-- Name: player_locations; Type: VIEW; Schema: public; Owner: postgres
--

CREATE VIEW public.player_locations AS
 SELECT p.player_id,
    p.name AS player_name,
    sh.sector_id,
    sh.ship_id,
        CASE
            WHEN (sh.ported = 1) THEN 'docked_at_port'::text
            WHEN (sh.onplanet = 1) THEN 'landed_on_planet'::text
            WHEN (sh.sector_id IS NOT NULL) THEN 'in_space'::text
            ELSE 'unknown'::text
        END AS location_kind,
    sh.ported AS is_ported,
    sh.onplanet AS is_onplanet
   FROM (public.players p
     LEFT JOIN public.ships sh ON ((sh.ship_id = p.ship_id)));


ALTER VIEW public.player_locations OWNER TO postgres;

--
-- Name: player_notes; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.player_notes (
    player_notes_id bigint NOT NULL,
    player_id bigint NOT NULL,
    scope text NOT NULL,
    key text NOT NULL,
    note text NOT NULL,
    updated_at timestamp with time zone DEFAULT CURRENT_TIMESTAMP NOT NULL
);


ALTER TABLE public.player_notes OWNER TO postgres;

--
-- Name: player_notes_player_notes_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.player_notes_player_notes_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.player_notes_player_notes_id_seq OWNER TO postgres;

--
-- Name: player_notes_player_notes_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.player_notes_player_notes_id_seq OWNED BY public.player_notes.player_notes_id;


--
-- Name: player_prefs; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.player_prefs (
    player_prefs_id bigint NOT NULL,
    key text NOT NULL,
    type text NOT NULL,
    value text NOT NULL,
    updated_at timestamp with time zone DEFAULT CURRENT_TIMESTAMP NOT NULL,
    CONSTRAINT player_prefs_type_check CHECK ((type = ANY (ARRAY['bool'::text, 'int'::text, 'string'::text, 'json'::text])))
);


ALTER TABLE public.player_prefs OWNER TO postgres;

--
-- Name: player_types; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.player_types (
    type bigint NOT NULL,
    description text
);


ALTER TABLE public.player_types OWNER TO postgres;

--
-- Name: player_types_type_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.player_types_type_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.player_types_type_seq OWNER TO postgres;

--
-- Name: player_types_type_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.player_types_type_seq OWNED BY public.player_types.type;


--
-- Name: players_player_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.players_player_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.players_player_id_seq OWNER TO postgres;

--
-- Name: players_player_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.players_player_id_seq OWNED BY public.players.player_id;


--
-- Name: podded_status; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.podded_status (
    player_id bigint NOT NULL,
    status text DEFAULT 'active'::text NOT NULL,
    big_sleep_until bigint,
    reason text,
    podded_count_today bigint DEFAULT 0 NOT NULL,
    podded_last_reset bigint
);


ALTER TABLE public.podded_status OWNER TO postgres;

--
-- Name: podded_status_player_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.podded_status_player_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.podded_status_player_id_seq OWNER TO postgres;

--
-- Name: podded_status_player_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.podded_status_player_id_seq OWNED BY public.podded_status.player_id;


--
-- Name: port_busts; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.port_busts (
    port_id bigint NOT NULL,
    player_id bigint NOT NULL,
    last_bust_at timestamp with time zone NOT NULL,
    bust_type text NOT NULL,
    active bigint DEFAULT 1 NOT NULL
);


ALTER TABLE public.port_busts OWNER TO postgres;

--
-- Name: port_trade; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.port_trade (
    port_trade_id bigint NOT NULL,
    port_id bigint NOT NULL,
    maxproduct bigint,
    commodity text,
    mode text,
    CONSTRAINT port_trade_commodity_check CHECK ((commodity = ANY (ARRAY['ore'::text, 'organics'::text, 'equipment'::text]))),
    CONSTRAINT port_trade_mode_check CHECK ((mode = ANY (ARRAY['buy'::text, 'sell'::text])))
);


ALTER TABLE public.port_trade OWNER TO postgres;

--
-- Name: ports; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.ports (
    port_id bigint NOT NULL,
    number bigint,
    name text NOT NULL,
    sector_id bigint NOT NULL,
    size bigint,
    techlevel bigint,
    petty_cash bigint DEFAULT 0 NOT NULL,
    invisible boolean DEFAULT false,
    type bigint DEFAULT 1,
    economy_curve_id bigint DEFAULT 1 NOT NULL
);


ALTER TABLE public.ports OWNER TO postgres;

--
-- Name: port_trade_code; Type: VIEW; Schema: public; Owner: postgres
--

CREATE VIEW public.port_trade_code AS
 WITH m AS (
         SELECT p_1.port_id,
            max(
                CASE
                    WHEN (t.commodity = 'ore'::text) THEN
                    CASE t.mode
                        WHEN 'buy'::text THEN 'B'::text
                        ELSE 'S'::text
                    END
                    ELSE NULL::text
                END) AS ore,
            max(
                CASE
                    WHEN (t.commodity = 'organics'::text) THEN
                    CASE t.mode
                        WHEN 'buy'::text THEN 'B'::text
                        ELSE 'S'::text
                    END
                    ELSE NULL::text
                END) AS org,
            max(
                CASE
                    WHEN (t.commodity = 'equipment'::text) THEN
                    CASE t.mode
                        WHEN 'buy'::text THEN 'B'::text
                        ELSE 'S'::text
                    END
                    ELSE NULL::text
                END) AS eqp
           FROM (public.ports p_1
             LEFT JOIN public.port_trade t ON ((t.port_id = p_1.port_id)))
          GROUP BY p_1.port_id
        )
 SELECT p.port_id AS id,
    p.number,
    p.name,
    p.sector_id,
    p.size,
    p.techlevel,
    p.petty_cash,
    ((COALESCE(m.ore, '-'::text) || COALESCE(m.org, '-'::text)) || COALESCE(m.eqp, '-'::text)) AS trade_code
   FROM (public.ports p
     LEFT JOIN m ON ((m.port_id = p.port_id)));


ALTER VIEW public.port_trade_code OWNER TO postgres;

--
-- Name: port_trade_port_trade_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.port_trade_port_trade_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.port_trade_port_trade_id_seq OWNER TO postgres;

--
-- Name: port_trade_port_trade_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.port_trade_port_trade_id_seq OWNED BY public.port_trade.port_trade_id;


--
-- Name: ports_port_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.ports_port_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.ports_port_id_seq OWNER TO postgres;

--
-- Name: ports_port_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.ports_port_id_seq OWNED BY public.ports.port_id;


--
-- Name: research_contributors; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.research_contributors (
    project_id bigint NOT NULL,
    actor_type text NOT NULL,
    actor_id bigint NOT NULL,
    amount bigint NOT NULL,
    CONSTRAINT research_contributors_actor_type_check CHECK ((actor_type = ANY (ARRAY['player'::text, 'corp'::text]))),
    CONSTRAINT research_contributors_amount_check CHECK ((amount >= 0))
);


ALTER TABLE public.research_contributors OWNER TO postgres;

--
-- Name: research_projects; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.research_projects (
    research_projects_id bigint NOT NULL,
    sponsor_type text NOT NULL,
    sponsor_id bigint,
    title text NOT NULL,
    field text NOT NULL,
    cost bigint NOT NULL,
    progress bigint DEFAULT 0 NOT NULL,
    status text DEFAULT 'active'::text NOT NULL,
    created_ts timestamp with time zone DEFAULT CURRENT_TIMESTAMP NOT NULL,
    CONSTRAINT research_projects_cost_check CHECK ((cost >= 0)),
    CONSTRAINT research_projects_progress_check CHECK (((progress >= 0) AND (progress <= 100))),
    CONSTRAINT research_projects_sponsor_type_check CHECK ((sponsor_type = ANY (ARRAY['player'::text, 'corp'::text, 'gov'::text]))),
    CONSTRAINT research_projects_status_check CHECK ((status = ANY (ARRAY['active'::text, 'paused'::text, 'complete'::text, 'failed'::text])))
);


ALTER TABLE public.research_projects OWNER TO postgres;

--
-- Name: research_projects_research_projects_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.research_projects_research_projects_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.research_projects_research_projects_id_seq OWNER TO postgres;

--
-- Name: research_projects_research_projects_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.research_projects_research_projects_id_seq OWNED BY public.research_projects.research_projects_id;


--
-- Name: research_results; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.research_results (
    research_results_id bigint NOT NULL,
    project_id bigint NOT NULL,
    blueprint_code text NOT NULL,
    unlocked_ts timestamp with time zone DEFAULT CURRENT_TIMESTAMP NOT NULL
);


ALTER TABLE public.research_results OWNER TO postgres;

--
-- Name: research_results_research_results_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.research_results_research_results_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.research_results_research_results_id_seq OWNER TO postgres;

--
-- Name: research_results_research_results_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.research_results_research_results_id_seq OWNED BY public.research_results.research_results_id;


--
-- Name: risk_profiles; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.risk_profiles (
    risk_profiles_id bigint NOT NULL,
    entity_type text NOT NULL,
    entity_id bigint NOT NULL,
    risk_score bigint DEFAULT 0 NOT NULL,
    CONSTRAINT risk_profiles_entity_type_check CHECK ((entity_type = ANY (ARRAY['player'::text, 'corp'::text])))
);


ALTER TABLE public.risk_profiles OWNER TO postgres;

--
-- Name: risk_profiles_risk_profiles_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.risk_profiles_risk_profiles_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.risk_profiles_risk_profiles_id_seq OWNER TO postgres;

--
-- Name: risk_profiles_risk_profiles_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.risk_profiles_risk_profiles_id_seq OWNED BY public.risk_profiles.risk_profiles_id;


--
-- Name: s2s_keys; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.s2s_keys (
    key_id text NOT NULL,
    key_b64 text NOT NULL,
    is_default_tx bigint DEFAULT 0 NOT NULL,
    active boolean DEFAULT true,
    created_ts timestamp with time zone NOT NULL
);


ALTER TABLE public.s2s_keys OWNER TO postgres;

--
-- Name: sector_adjacency; Type: VIEW; Schema: public; Owner: postgres
--

CREATE VIEW public.sector_adjacency AS
 SELECT s.sector_id,
    COALESCE(string_agg((w.to_sector)::text, ','::text), ''::text) AS neighbors
   FROM (public.sectors s
     LEFT JOIN public.sector_warps w ON ((w.from_sector = s.sector_id)))
  GROUP BY s.sector_id;


ALTER VIEW public.sector_adjacency OWNER TO postgres;

--
-- Name: sector_assets; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.sector_assets (
    sector_assets_id bigint NOT NULL,
    sector_id bigint NOT NULL,
    owner_id bigint,
    corporation_id bigint DEFAULT 0 NOT NULL,
    asset_type bigint NOT NULL,
    offensive_setting bigint DEFAULT 0,
    quantity bigint,
    ttl bigint,
    deployed_at timestamp with time zone DEFAULT CURRENT_TIMESTAMP NOT NULL
);


ALTER TABLE public.sector_assets OWNER TO postgres;

--
-- Name: sector_assets_sector_assets_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.sector_assets_sector_assets_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.sector_assets_sector_assets_id_seq OWNER TO postgres;

--
-- Name: sector_assets_sector_assets_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.sector_assets_sector_assets_id_seq OWNED BY public.sector_assets.sector_assets_id;


--
-- Name: sector_degrees; Type: VIEW; Schema: public; Owner: postgres
--

CREATE VIEW public.sector_degrees AS
 WITH outdeg AS (
         SELECT s.sector_id,
            count(w.to_sector) AS outdeg
           FROM (public.sectors s
             LEFT JOIN public.sector_warps w ON ((w.from_sector = s.sector_id)))
          GROUP BY s.sector_id
        ), indeg AS (
         SELECT s.sector_id,
            count(w.from_sector) AS indeg
           FROM (public.sectors s
             LEFT JOIN public.sector_warps w ON ((w.to_sector = s.sector_id)))
          GROUP BY s.sector_id
        )
 SELECT o.sector_id,
    o.outdeg,
    i.indeg
   FROM (outdeg o
     JOIN indeg i USING (sector_id));


ALTER VIEW public.sector_degrees OWNER TO postgres;

--
-- Name: sector_gdp; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.sector_gdp (
    sector_id bigint NOT NULL,
    gdp bigint DEFAULT 0 NOT NULL,
    last_update text
);


ALTER TABLE public.sector_gdp OWNER TO postgres;

--
-- Name: sector_gdp_sector_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.sector_gdp_sector_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.sector_gdp_sector_id_seq OWNER TO postgres;

--
-- Name: sector_gdp_sector_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.sector_gdp_sector_id_seq OWNED BY public.sector_gdp.sector_id;


--
-- Name: sector_planets; Type: VIEW; Schema: public; Owner: postgres
--

CREATE VIEW public.sector_planets AS
 SELECT s.sector_id,
    count(p.planet_id) AS planet_count,
    COALESCE(string_agg(p.name, ', '::text), ''::text) AS planets
   FROM (public.sectors s
     LEFT JOIN public.planets p ON ((p.sector_id = s.sector_id)))
  GROUP BY s.sector_id;


ALTER VIEW public.sector_planets OWNER TO postgres;

--
-- Name: sector_ports; Type: VIEW; Schema: public; Owner: postgres
--

CREATE VIEW public.sector_ports AS
 SELECT s.sector_id,
    count(p.port_id) AS port_count,
    COALESCE(string_agg(((p.name || ':'::text) || pt.trade_code), ' | '::text), ''::text) AS ports
   FROM ((public.sectors s
     LEFT JOIN public.port_trade_code pt ON ((pt.sector_id = s.sector_id)))
     LEFT JOIN public.ports p ON ((p.port_id = pt.id)))
  GROUP BY s.sector_id;


ALTER VIEW public.sector_ports OWNER TO postgres;

--
-- Name: sector_summary; Type: VIEW; Schema: public; Owner: postgres
--

CREATE VIEW public.sector_summary AS
 WITH pc AS (
         SELECT planets.sector_id,
            count(*) AS planet_count
           FROM public.planets
          GROUP BY planets.sector_id
        ), prt AS (
         SELECT ports.sector_id,
            count(*) AS port_count
           FROM public.ports
          GROUP BY ports.sector_id
        )
 SELECT s.sector_id,
    COALESCE(d.outdeg, (0)::bigint) AS outdeg,
    COALESCE(d.indeg, (0)::bigint) AS indeg,
    COALESCE(prt.port_count, (0)::bigint) AS ports,
    COALESCE(pc.planet_count, (0)::bigint) AS planets
   FROM (((public.sectors s
     LEFT JOIN public.sector_degrees d ON ((d.sector_id = s.sector_id)))
     LEFT JOIN prt ON ((prt.sector_id = s.sector_id)))
     LEFT JOIN pc ON ((pc.sector_id = s.sector_id)));


ALTER VIEW public.sector_summary OWNER TO postgres;

--
-- Name: ships_by_sector; Type: VIEW; Schema: public; Owner: postgres
--

CREATE VIEW public.ships_by_sector AS
 SELECT s.sector_id,
    count(sh.ship_id) AS ship_count
   FROM (public.sectors s
     LEFT JOIN public.ships sh ON ((sh.sector_id = s.sector_id)))
  GROUP BY s.sector_id;


ALTER VIEW public.ships_by_sector OWNER TO postgres;

--
-- Name: sector_ops; Type: VIEW; Schema: public; Owner: postgres
--

CREATE VIEW public.sector_ops AS
 WITH weighted_assets AS (
         SELECT sector_assets.sector_id,
            COALESCE(sum((sector_assets.quantity *
                CASE sector_assets.asset_type
                    WHEN 1 THEN 10
                    WHEN 2 THEN 5
                    WHEN 3 THEN 1
                    WHEN 4 THEN 10
                    ELSE 0
                END)), (0)::numeric) AS asset_score
           FROM public.sector_assets
          GROUP BY sector_assets.sector_id
        )
 SELECT ss.sector_id,
    ss.outdeg,
    ss.indeg,
    sp.port_count,
    spp.planet_count,
    sbs.ship_count,
    (((((COALESCE(spp.planet_count, (0)::bigint) * 500) + (COALESCE(sp.port_count, (0)::bigint) * 100)) + (COALESCE(sbs.ship_count, (0)::bigint) * 40)))::numeric + COALESCE(wa.asset_score, (0)::numeric)) AS total_density_score,
    wa.asset_score AS weighted_asset_score
   FROM ((((public.sector_summary ss
     LEFT JOIN public.sector_ports sp ON ((sp.sector_id = ss.sector_id)))
     LEFT JOIN public.sector_planets spp ON ((spp.sector_id = ss.sector_id)))
     LEFT JOIN public.ships_by_sector sbs ON ((sbs.sector_id = ss.sector_id)))
     LEFT JOIN weighted_assets wa ON ((wa.sector_id = ss.sector_id)));


ALTER VIEW public.sector_ops OWNER TO postgres;

--
-- Name: sector_search_index; Type: VIEW; Schema: public; Owner: postgres
--

CREATE VIEW public.sector_search_index AS
 SELECT 'sector'::text AS kind,
    s.sector_id AS id,
    s.name,
    s.sector_id,
    s.name AS sector_name,
    s.name AS search_term_1
   FROM public.sectors s
UNION ALL
 SELECT 'port'::text AS kind,
    p.port_id AS id,
    p.name,
    p.sector_id,
    s.name AS sector_name,
    p.name AS search_term_1
   FROM (public.ports p
     JOIN public.sectors s ON ((s.sector_id = p.sector_id)));


ALTER VIEW public.sector_search_index OWNER TO postgres;

--
-- Name: sectors_dead_in; Type: VIEW; Schema: public; Owner: postgres
--

CREATE VIEW public.sectors_dead_in AS
 SELECT sector_id
   FROM public.sector_degrees
  WHERE (indeg = 0);


ALTER VIEW public.sectors_dead_in OWNER TO postgres;

--
-- Name: sectors_dead_out; Type: VIEW; Schema: public; Owner: postgres
--

CREATE VIEW public.sectors_dead_out AS
 SELECT sector_id
   FROM public.sector_degrees
  WHERE (outdeg = 0);


ALTER VIEW public.sectors_dead_out OWNER TO postgres;

--
-- Name: sectors_isolated; Type: VIEW; Schema: public; Owner: postgres
--

CREATE VIEW public.sectors_isolated AS
 SELECT sector_id
   FROM public.sector_degrees
  WHERE ((outdeg = 0) AND (indeg = 0));


ALTER VIEW public.sectors_isolated OWNER TO postgres;

--
-- Name: sectors_sector_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.sectors_sector_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.sectors_sector_id_seq OWNER TO postgres;

--
-- Name: sectors_sector_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.sectors_sector_id_seq OWNED BY public.sectors.sector_id;


--
-- Name: sessions; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.sessions (
    token text NOT NULL,
    player_id bigint NOT NULL,
    expires timestamp with time zone NOT NULL,
    created_at timestamp with time zone DEFAULT CURRENT_TIMESTAMP NOT NULL
);


ALTER TABLE public.sessions OWNER TO postgres;

--
-- Name: ship_markers; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.ship_markers (
    ship_id bigint NOT NULL,
    owner_player bigint NOT NULL,
    owner_corp bigint DEFAULT 0 NOT NULL,
    marker_type text NOT NULL
);


ALTER TABLE public.ship_markers OWNER TO postgres;

--
-- Name: ship_ownership; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.ship_ownership (
    ship_id bigint NOT NULL,
    player_id bigint NOT NULL,
    role_id bigint NOT NULL,
    is_primary boolean DEFAULT true,
    acquired_at timestamp with time zone DEFAULT CURRENT_TIMESTAMP NOT NULL
);


ALTER TABLE public.ship_ownership OWNER TO postgres;

--
-- Name: ship_roles; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.ship_roles (
    role_id bigint NOT NULL,
    role text,
    role_description text
);


ALTER TABLE public.ship_roles OWNER TO postgres;

--
-- Name: ship_roles_role_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.ship_roles_role_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.ship_roles_role_id_seq OWNER TO postgres;

--
-- Name: ship_roles_role_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.ship_roles_role_id_seq OWNED BY public.ship_roles.role_id;


--
-- Name: ships_ship_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.ships_ship_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.ships_ship_id_seq OWNER TO postgres;

--
-- Name: ships_ship_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.ships_ship_id_seq OWNED BY public.ships.ship_id;


--
-- Name: shiptypes_shiptypes_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.shiptypes_shiptypes_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.shiptypes_shiptypes_id_seq OWNER TO postgres;

--
-- Name: shiptypes_shiptypes_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.shiptypes_shiptypes_id_seq OWNED BY public.shiptypes.shiptypes_id;


--
-- Name: shipyard_inventory; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.shipyard_inventory (
    port_id bigint NOT NULL,
    ship_type_id bigint NOT NULL,
    enabled bigint DEFAULT 1 NOT NULL
);


ALTER TABLE public.shipyard_inventory OWNER TO postgres;

--
-- Name: stardock_assets; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.stardock_assets (
    sector_id bigint NOT NULL,
    owner_id bigint NOT NULL,
    fighters bigint DEFAULT 0 NOT NULL,
    defenses bigint DEFAULT 0 NOT NULL,
    ship_capacity bigint DEFAULT 1 NOT NULL,
    created_at timestamp with time zone DEFAULT CURRENT_TIMESTAMP NOT NULL
);


ALTER TABLE public.stardock_assets OWNER TO postgres;

--
-- Name: stardock_assets_sector_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.stardock_assets_sector_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.stardock_assets_sector_id_seq OWNER TO postgres;

--
-- Name: stardock_assets_sector_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.stardock_assets_sector_id_seq OWNED BY public.stardock_assets.sector_id;


--
-- Name: stardock_location; Type: VIEW; Schema: public; Owner: postgres
--

CREATE VIEW public.stardock_location AS
 SELECT port_id,
    number,
    name,
    sector_id
   FROM public.ports
  WHERE ((type = 9) OR (name ~~ '%Stardock%'::text));


ALTER VIEW public.stardock_location OWNER TO postgres;

--
-- Name: stock_dividends; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.stock_dividends (
    id bigint NOT NULL,
    stock_id bigint NOT NULL,
    amount_per_share bigint NOT NULL,
    declared_ts text NOT NULL,
    paid_ts text,
    CONSTRAINT stock_dividends_amount_per_share_check CHECK ((amount_per_share >= 0))
);


ALTER TABLE public.stock_dividends OWNER TO postgres;

--
-- Name: stock_dividends_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.stock_dividends_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.stock_dividends_id_seq OWNER TO postgres;

--
-- Name: stock_dividends_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.stock_dividends_id_seq OWNED BY public.stock_dividends.id;


--
-- Name: stock_index_members; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.stock_index_members (
    index_id bigint NOT NULL,
    stock_id bigint NOT NULL,
    weight double precision DEFAULT 1.0 NOT NULL
);


ALTER TABLE public.stock_index_members OWNER TO postgres;

--
-- Name: stock_indices; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.stock_indices (
    id bigint NOT NULL,
    name text NOT NULL
);


ALTER TABLE public.stock_indices OWNER TO postgres;

--
-- Name: stock_indices_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.stock_indices_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.stock_indices_id_seq OWNER TO postgres;

--
-- Name: stock_indices_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.stock_indices_id_seq OWNED BY public.stock_indices.id;


--
-- Name: stock_orders; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.stock_orders (
    id bigint NOT NULL,
    player_id bigint NOT NULL,
    stock_id bigint NOT NULL,
    type text NOT NULL,
    quantity bigint NOT NULL,
    price bigint NOT NULL,
    status text DEFAULT 'open'::text NOT NULL,
    ts timestamp with time zone DEFAULT CURRENT_TIMESTAMP NOT NULL,
    CONSTRAINT stock_orders_price_check CHECK ((price >= 0)),
    CONSTRAINT stock_orders_quantity_check CHECK ((quantity > 0)),
    CONSTRAINT stock_orders_status_check CHECK ((status = ANY (ARRAY['open'::text, 'filled'::text, 'cancelled'::text, 'expired'::text]))),
    CONSTRAINT stock_orders_type_check CHECK ((type = ANY (ARRAY['buy'::text, 'sell'::text])))
);


ALTER TABLE public.stock_orders OWNER TO postgres;

--
-- Name: stock_orders_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.stock_orders_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.stock_orders_id_seq OWNER TO postgres;

--
-- Name: stock_orders_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.stock_orders_id_seq OWNED BY public.stock_orders.id;


--
-- Name: stock_trades; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.stock_trades (
    id bigint NOT NULL,
    stock_id bigint NOT NULL,
    buyer_id bigint NOT NULL,
    seller_id bigint NOT NULL,
    quantity bigint NOT NULL,
    price bigint NOT NULL,
    ts timestamp with time zone DEFAULT CURRENT_TIMESTAMP NOT NULL,
    settlement_tx_buy bigint,
    settlement_tx_sell bigint,
    CONSTRAINT stock_trades_price_check CHECK ((price >= 0)),
    CONSTRAINT stock_trades_quantity_check CHECK ((quantity > 0))
);


ALTER TABLE public.stock_trades OWNER TO postgres;

--
-- Name: stock_trades_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.stock_trades_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.stock_trades_id_seq OWNER TO postgres;

--
-- Name: stock_trades_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.stock_trades_id_seq OWNED BY public.stock_trades.id;


--
-- Name: stocks; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.stocks (
    id bigint NOT NULL,
    corp_id bigint NOT NULL,
    ticker text NOT NULL,
    total_shares bigint NOT NULL,
    par_value bigint DEFAULT 0 NOT NULL,
    current_price bigint DEFAULT 0 NOT NULL,
    last_dividend_ts text,
    CONSTRAINT stocks_current_price_check CHECK ((current_price >= 0)),
    CONSTRAINT stocks_par_value_check CHECK ((par_value >= 0)),
    CONSTRAINT stocks_total_shares_check CHECK ((total_shares > 0))
);


ALTER TABLE public.stocks OWNER TO postgres;

--
-- Name: stocks_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.stocks_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.stocks_id_seq OWNER TO postgres;

--
-- Name: stocks_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.stocks_id_seq OWNED BY public.stocks.id;


--
-- Name: subscriptions; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.subscriptions (
    subscriptions_id bigint NOT NULL,
    player_id bigint NOT NULL,
    event_type text NOT NULL,
    delivery text NOT NULL,
    filter_json text,
    ephemeral bigint DEFAULT 0 NOT NULL,
    locked bigint DEFAULT 0 NOT NULL,
    enabled bigint DEFAULT 1 NOT NULL
);


ALTER TABLE public.subscriptions OWNER TO postgres;

--
-- Name: subscriptions_subscriptions_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.subscriptions_subscriptions_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.subscriptions_subscriptions_id_seq OWNER TO postgres;

--
-- Name: subscriptions_subscriptions_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.subscriptions_subscriptions_id_seq OWNED BY public.subscriptions.subscriptions_id;


--
-- Name: subspace; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.subspace (
    subspace_id bigint NOT NULL,
    sender_id bigint,
    message text NOT NULL,
    kind text DEFAULT 'chat'::text NOT NULL,
    posted_at timestamp with time zone DEFAULT CURRENT_TIMESTAMP NOT NULL
);


ALTER TABLE public.subspace OWNER TO postgres;

--
-- Name: subspace_cursors; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.subspace_cursors (
    player_id bigint NOT NULL,
    last_seen_id bigint DEFAULT 0 NOT NULL
);


ALTER TABLE public.subspace_cursors OWNER TO postgres;

--
-- Name: subspace_cursors_player_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.subspace_cursors_player_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.subspace_cursors_player_id_seq OWNER TO postgres;

--
-- Name: subspace_cursors_player_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.subspace_cursors_player_id_seq OWNED BY public.subspace_cursors.player_id;


--
-- Name: subspace_subspace_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.subspace_subspace_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.subspace_subspace_id_seq OWNER TO postgres;

--
-- Name: subspace_subspace_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.subspace_subspace_id_seq OWNED BY public.subspace.subspace_id;


--
-- Name: system_events; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.system_events (
    system_events_id bigint NOT NULL,
    scope text NOT NULL,
    event_type text NOT NULL,
    payload text NOT NULL,
    created_at timestamp with time zone DEFAULT CURRENT_TIMESTAMP NOT NULL
);


ALTER TABLE public.system_events OWNER TO postgres;

--
-- Name: system_events_system_events_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.system_events_system_events_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.system_events_system_events_id_seq OWNER TO postgres;

--
-- Name: system_events_system_events_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.system_events_system_events_id_seq OWNED BY public.system_events.system_events_id;


--
-- Name: system_notice; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.system_notice (
    system_notice_id bigint NOT NULL,
    created_at timestamp with time zone DEFAULT CURRENT_TIMESTAMP NOT NULL,
    title text NOT NULL,
    body text NOT NULL,
    severity text NOT NULL,
    expires_at timestamp with time zone,
    CONSTRAINT system_notice_severity_check CHECK ((severity = ANY (ARRAY['info'::text, 'warn'::text, 'error'::text])))
);


ALTER TABLE public.system_notice OWNER TO postgres;

--
-- Name: system_notice_system_notice_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.system_notice_system_notice_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.system_notice_system_notice_id_seq OWNER TO postgres;

--
-- Name: system_notice_system_notice_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.system_notice_system_notice_id_seq OWNED BY public.system_notice.system_notice_id;


--
-- Name: tavern_deadpool_bets; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.tavern_deadpool_bets (
    tavern_deadpool_bets_id bigint NOT NULL,
    bettor_id bigint NOT NULL,
    target_id bigint NOT NULL,
    amount bigint NOT NULL,
    odds_bp bigint NOT NULL,
    placed_at timestamp with time zone DEFAULT CURRENT_TIMESTAMP NOT NULL,
    expires_at timestamp with time zone DEFAULT CURRENT_TIMESTAMP NOT NULL,
    resolved bigint DEFAULT 0 NOT NULL,
    resolved_at timestamp with time zone,
    result text
);


ALTER TABLE public.tavern_deadpool_bets OWNER TO postgres;

--
-- Name: tavern_deadpool_bets_tavern_deadpool_bets_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.tavern_deadpool_bets_tavern_deadpool_bets_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.tavern_deadpool_bets_tavern_deadpool_bets_id_seq OWNER TO postgres;

--
-- Name: tavern_deadpool_bets_tavern_deadpool_bets_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.tavern_deadpool_bets_tavern_deadpool_bets_id_seq OWNED BY public.tavern_deadpool_bets.tavern_deadpool_bets_id;


--
-- Name: tavern_graffiti; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.tavern_graffiti (
    tavern_graffiti_id bigint NOT NULL,
    player_id bigint NOT NULL,
    text text NOT NULL,
    created_at timestamp with time zone DEFAULT CURRENT_TIMESTAMP NOT NULL
);


ALTER TABLE public.tavern_graffiti OWNER TO postgres;

--
-- Name: tavern_graffiti_tavern_graffiti_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.tavern_graffiti_tavern_graffiti_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.tavern_graffiti_tavern_graffiti_id_seq OWNER TO postgres;

--
-- Name: tavern_graffiti_tavern_graffiti_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.tavern_graffiti_tavern_graffiti_id_seq OWNED BY public.tavern_graffiti.tavern_graffiti_id;


--
-- Name: tavern_loans; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.tavern_loans (
    player_id bigint NOT NULL,
    principal bigint NOT NULL,
    interest_rate bigint NOT NULL,
    due_date timestamp with time zone NOT NULL,
    is_defaulted bigint DEFAULT 0 NOT NULL
);


ALTER TABLE public.tavern_loans OWNER TO postgres;

--
-- Name: tavern_loans_player_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.tavern_loans_player_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.tavern_loans_player_id_seq OWNER TO postgres;

--
-- Name: tavern_loans_player_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.tavern_loans_player_id_seq OWNED BY public.tavern_loans.player_id;


--
-- Name: tavern_lottery_state; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.tavern_lottery_state (
    draw_date text NOT NULL,
    winning_number bigint,
    jackpot bigint NOT NULL,
    carried_over bigint DEFAULT 0 NOT NULL
);


ALTER TABLE public.tavern_lottery_state OWNER TO postgres;

--
-- Name: tavern_lottery_tickets; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.tavern_lottery_tickets (
    tavern_lottery_tickets_id bigint NOT NULL,
    draw_date text NOT NULL,
    player_id bigint NOT NULL,
    number bigint NOT NULL,
    cost bigint NOT NULL,
    purchased_at timestamp with time zone DEFAULT CURRENT_TIMESTAMP NOT NULL
);


ALTER TABLE public.tavern_lottery_tickets OWNER TO postgres;

--
-- Name: tavern_lottery_tickets_tavern_lottery_tickets_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.tavern_lottery_tickets_tavern_lottery_tickets_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.tavern_lottery_tickets_tavern_lottery_tickets_id_seq OWNER TO postgres;

--
-- Name: tavern_lottery_tickets_tavern_lottery_tickets_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.tavern_lottery_tickets_tavern_lottery_tickets_id_seq OWNED BY public.tavern_lottery_tickets.tavern_lottery_tickets_id;


--
-- Name: tavern_names; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.tavern_names (
    tavern_names_id bigint NOT NULL,
    name text NOT NULL,
    enabled boolean DEFAULT true,
    weight bigint DEFAULT 1 NOT NULL
);


ALTER TABLE public.tavern_names OWNER TO postgres;

--
-- Name: tavern_names_tavern_names_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.tavern_names_tavern_names_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.tavern_names_tavern_names_id_seq OWNER TO postgres;

--
-- Name: tavern_names_tavern_names_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.tavern_names_tavern_names_id_seq OWNED BY public.tavern_names.tavern_names_id;


--
-- Name: tavern_notices; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.tavern_notices (
    tavern_notices_id bigint NOT NULL,
    author_id bigint NOT NULL,
    corp_id bigint,
    text text NOT NULL,
    created_at timestamp with time zone DEFAULT CURRENT_TIMESTAMP NOT NULL,
    expires_at timestamp with time zone DEFAULT CURRENT_TIMESTAMP NOT NULL
);


ALTER TABLE public.tavern_notices OWNER TO postgres;

--
-- Name: tavern_notices_tavern_notices_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.tavern_notices_tavern_notices_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.tavern_notices_tavern_notices_id_seq OWNER TO postgres;

--
-- Name: tavern_notices_tavern_notices_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.tavern_notices_tavern_notices_id_seq OWNED BY public.tavern_notices.tavern_notices_id;


--
-- Name: tavern_raffle_state; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.tavern_raffle_state (
    tavern_raffle_state_id bigint NOT NULL,
    pot bigint NOT NULL,
    last_winner_id bigint,
    last_payout bigint,
    last_win_ts bigint,
    CONSTRAINT tavern_raffle_state_tavern_raffle_state_id_check CHECK ((tavern_raffle_state_id = 1))
);


ALTER TABLE public.tavern_raffle_state OWNER TO postgres;

--
-- Name: tavern_raffle_state_tavern_raffle_state_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.tavern_raffle_state_tavern_raffle_state_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.tavern_raffle_state_tavern_raffle_state_id_seq OWNER TO postgres;

--
-- Name: tavern_raffle_state_tavern_raffle_state_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.tavern_raffle_state_tavern_raffle_state_id_seq OWNED BY public.tavern_raffle_state.tavern_raffle_state_id;


--
-- Name: tavern_settings; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.tavern_settings (
    tavern_settings_id bigint NOT NULL,
    max_bet_per_transaction bigint DEFAULT 5000 NOT NULL,
    daily_max_wager bigint DEFAULT 50000 NOT NULL,
    enable_dynamic_wager_limit bigint DEFAULT 0 NOT NULL,
    graffiti_max_posts bigint DEFAULT 100 NOT NULL,
    notice_expires_days bigint DEFAULT 7 NOT NULL,
    buy_round_cost bigint DEFAULT 1000 NOT NULL,
    buy_round_alignment_gain bigint DEFAULT 5 NOT NULL,
    loan_shark_enabled bigint DEFAULT 1 NOT NULL,
    CONSTRAINT tavern_settings_tavern_settings_id_check CHECK ((tavern_settings_id = 1))
);


ALTER TABLE public.tavern_settings OWNER TO postgres;

--
-- Name: tavern_settings_tavern_settings_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.tavern_settings_tavern_settings_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.tavern_settings_tavern_settings_id_seq OWNER TO postgres;

--
-- Name: tavern_settings_tavern_settings_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.tavern_settings_tavern_settings_id_seq OWNED BY public.tavern_settings.tavern_settings_id;


--
-- Name: taverns; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.taverns (
    sector_id bigint NOT NULL,
    name_id bigint NOT NULL,
    enabled bigint DEFAULT 1 NOT NULL
);


ALTER TABLE public.taverns OWNER TO postgres;

--
-- Name: taverns_sector_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.taverns_sector_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.taverns_sector_id_seq OWNER TO postgres;

--
-- Name: taverns_sector_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.taverns_sector_id_seq OWNED BY public.taverns.sector_id;


--
-- Name: tax_ledgers; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.tax_ledgers (
    tax_ledgers_id bigint NOT NULL,
    policy_id bigint NOT NULL,
    payer_type text NOT NULL,
    payer_id bigint NOT NULL,
    amount bigint NOT NULL,
    ts timestamp with time zone DEFAULT CURRENT_TIMESTAMP NOT NULL,
    bank_tx_id bigint,
    CONSTRAINT tax_ledgers_amount_check CHECK ((amount >= 0)),
    CONSTRAINT tax_ledgers_payer_type_check CHECK ((payer_type = ANY (ARRAY['player'::text, 'corp'::text])))
);


ALTER TABLE public.tax_ledgers OWNER TO postgres;

--
-- Name: tax_ledgers_tax_ledgers_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.tax_ledgers_tax_ledgers_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.tax_ledgers_tax_ledgers_id_seq OWNER TO postgres;

--
-- Name: tax_ledgers_tax_ledgers_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.tax_ledgers_tax_ledgers_id_seq OWNED BY public.tax_ledgers.tax_ledgers_id;


--
-- Name: tax_policies; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.tax_policies (
    tax_policies_id bigint NOT NULL,
    name text NOT NULL,
    tax_type text NOT NULL,
    rate_bps bigint DEFAULT 0 NOT NULL,
    active bigint DEFAULT 1 NOT NULL,
    CONSTRAINT tax_policies_active_check CHECK ((active = ANY (ARRAY[(0)::bigint, (1)::bigint]))),
    CONSTRAINT tax_policies_rate_bps_check CHECK ((rate_bps >= 0)),
    CONSTRAINT tax_policies_tax_type_check CHECK ((tax_type = ANY (ARRAY['trade'::text, 'income'::text, 'corp'::text, 'wealth'::text, 'transfer'::text])))
);


ALTER TABLE public.tax_policies OWNER TO postgres;

--
-- Name: tax_policies_tax_policies_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.tax_policies_tax_policies_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.tax_policies_tax_policies_id_seq OWNER TO postgres;

--
-- Name: tax_policies_tax_policies_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.tax_policies_tax_policies_id_seq OWNED BY public.tax_policies.tax_policies_id;


--
-- Name: temples; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.temples (
    temples_id bigint NOT NULL,
    name text NOT NULL,
    sector_id bigint,
    favour bigint DEFAULT 0 NOT NULL
);


ALTER TABLE public.temples OWNER TO postgres;

--
-- Name: temples_temples_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.temples_temples_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.temples_temples_id_seq OWNER TO postgres;

--
-- Name: temples_temples_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.temples_temples_id_seq OWNED BY public.temples.temples_id;


--
-- Name: trade_idempotency; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.trade_idempotency (
    key text NOT NULL,
    player_id bigint NOT NULL,
    sector_id bigint NOT NULL,
    request_json text NOT NULL,
    response_json text NOT NULL,
    created_at timestamp with time zone DEFAULT CURRENT_TIMESTAMP NOT NULL
);


ALTER TABLE public.trade_idempotency OWNER TO postgres;

--
-- Name: trade_log; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.trade_log (
    trade_log_id bigint NOT NULL,
    player_id bigint NOT NULL,
    port_id bigint NOT NULL,
    sector_id bigint NOT NULL,
    commodity text NOT NULL,
    units bigint NOT NULL,
    price_per_unit double precision NOT NULL,
    action text NOT NULL,
    "timestamp" timestamp with time zone NOT NULL,
    CONSTRAINT trade_log_action_check CHECK ((action = ANY (ARRAY['buy'::text, 'sell'::text])))
);


ALTER TABLE public.trade_log OWNER TO postgres;

--
-- Name: trade_log_trade_log_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.trade_log_trade_log_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.trade_log_trade_log_id_seq OWNER TO postgres;

--
-- Name: trade_log_trade_log_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.trade_log_trade_log_id_seq OWNED BY public.trade_log.trade_log_id;


--
-- Name: traps; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.traps (
    id bigint NOT NULL,
    sector_id bigint NOT NULL,
    owner_player_id bigint,
    kind text NOT NULL,
    armed bigint DEFAULT 0 NOT NULL,
    arming_at timestamp with time zone DEFAULT CURRENT_TIMESTAMP NOT NULL,
    expires_at timestamp with time zone,
    trigger_at timestamp with time zone,
    payload text
);


ALTER TABLE public.traps OWNER TO postgres;

--
-- Name: traps_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.traps_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.traps_id_seq OWNER TO postgres;

--
-- Name: traps_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.traps_id_seq OWNED BY public.traps.id;


--
-- Name: turns; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.turns (
    player_id bigint NOT NULL,
    turns_remaining bigint NOT NULL,
    last_update timestamp without time zone NOT NULL
);


ALTER TABLE public.turns OWNER TO postgres;

--
-- Name: used_sectors; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.used_sectors (
    used bigint
);


ALTER TABLE public.used_sectors OWNER TO postgres;

--
-- Name: v_bank_leaderboard; Type: VIEW; Schema: public; Owner: postgres
--

CREATE VIEW public.v_bank_leaderboard AS
 SELECT ba.owner_id AS player_id,
    p.name,
    ba.balance
   FROM ((public.bank_accounts ba
     JOIN public.players p ON (((ba.owner_type = 'player'::text) AND (ba.owner_id = p.player_id))))
     LEFT JOIN public.player_prefs pp ON (((ba.owner_id = pp.player_prefs_id) AND (pp.key = 'privacy.show_leaderboard'::text))))
  WHERE (COALESCE(pp.value, 'true'::text) = 'true'::text)
  ORDER BY ba.balance DESC;


ALTER VIEW public.v_bank_leaderboard OWNER TO postgres;

--
-- Name: v_bidirectional_warps; Type: VIEW; Schema: public; Owner: postgres
--

CREATE VIEW public.v_bidirectional_warps AS
 SELECT
        CASE
            WHEN (w1.from_sector < w1.to_sector) THEN w1.from_sector
            ELSE w1.to_sector
        END AS a,
        CASE
            WHEN (w1.from_sector < w1.to_sector) THEN w1.to_sector
            ELSE w1.from_sector
        END AS b
   FROM (public.sector_warps w1
     JOIN public.sector_warps w2 ON (((w1.from_sector = w2.to_sector) AND (w1.to_sector = w2.from_sector))))
  GROUP BY
        CASE
            WHEN (w1.from_sector < w1.to_sector) THEN w1.from_sector
            ELSE w1.to_sector
        END,
        CASE
            WHEN (w1.from_sector < w1.to_sector) THEN w1.to_sector
            ELSE w1.from_sector
        END;


ALTER VIEW public.v_bidirectional_warps OWNER TO postgres;

--
-- Name: v_bounty_board; Type: VIEW; Schema: public; Owner: postgres
--

CREATE VIEW public.v_bounty_board AS
 SELECT b.bounties_id AS id,
    b.target_type,
    b.target_id,
    p_target.name AS target_name,
    b.reward,
    b.status,
    b.posted_by_type,
    b.posted_by_id,
        CASE b.posted_by_type
            WHEN 'player'::text THEN p_poster.name
            WHEN 'corp'::text THEN c_poster.name
            ELSE b.posted_by_type
        END AS poster_name,
    b.posted_ts
   FROM (((public.bounties b
     LEFT JOIN public.players p_target ON (((b.target_type = 'player'::text) AND (b.target_id = p_target.player_id))))
     LEFT JOIN public.players p_poster ON (((b.posted_by_type = 'player'::text) AND (b.posted_by_id = p_poster.player_id))))
     LEFT JOIN public.corporations c_poster ON (((b.posted_by_type = 'corp'::text) AND (b.posted_by_id = c_poster.corporation_id))))
  WHERE (b.status = 'open'::text);


ALTER VIEW public.v_bounty_board OWNER TO postgres;

--
-- Name: v_corp_treasury; Type: VIEW; Schema: public; Owner: postgres
--

CREATE VIEW public.v_corp_treasury AS
 SELECT c.corporation_id AS corp_id,
    c.name AS corp_name,
    COALESCE(ca.balance, (0)::bigint) AS bank_balance
   FROM (public.corporations c
     LEFT JOIN public.corp_accounts ca ON ((ca.corp_id = c.corporation_id)));


ALTER VIEW public.v_corp_treasury OWNER TO postgres;

--
-- Name: v_player_networth; Type: VIEW; Schema: public; Owner: postgres
--

CREATE VIEW public.v_player_networth AS
 SELECT p.player_id,
    p.name AS player_name,
    COALESCE(ba.balance, (0)::bigint) AS bank_balance
   FROM (public.players p
     LEFT JOIN public.bank_accounts ba ON (((ba.owner_type = 'player'::text) AND (ba.owner_id = p.player_id))));


ALTER VIEW public.v_player_networth OWNER TO postgres;

--
-- Name: warehouses; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.warehouses (
    warehouses_id bigint NOT NULL,
    location_type text NOT NULL,
    location_id bigint NOT NULL,
    owner_type text NOT NULL,
    owner_id bigint NOT NULL,
    CONSTRAINT warehouses_location_type_check CHECK ((location_type = ANY (ARRAY['sector'::text, 'planet'::text, 'port'::text]))),
    CONSTRAINT warehouses_owner_type_check CHECK ((owner_type = ANY (ARRAY['player'::text, 'corp'::text])))
);


ALTER TABLE public.warehouses OWNER TO postgres;

--
-- Name: warehouses_warehouses_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.warehouses_warehouses_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.warehouses_warehouses_id_seq OWNER TO postgres;

--
-- Name: warehouses_warehouses_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.warehouses_warehouses_id_seq OWNED BY public.warehouses.warehouses_id;


--
-- Name: world_summary; Type: VIEW; Schema: public; Owner: postgres
--

CREATE VIEW public.world_summary AS
 WITH a AS (
         SELECT count(*) AS sectors
           FROM public.sectors
        ), b AS (
         SELECT count(*) AS warps
           FROM public.sector_warps
        ), c AS (
         SELECT count(*) AS ports
           FROM public.ports
        ), d AS (
         SELECT count(*) AS planets
           FROM public.planets
        ), e AS (
         SELECT count(*) AS players
           FROM public.players
        ), f AS (
         SELECT count(*) AS ships
           FROM public.ships
        )
 SELECT a.sectors,
    b.warps,
    c.ports,
    d.planets,
    e.players,
    f.ships
   FROM a,
    b,
    c,
    d,
    e,
    f;


ALTER VIEW public.world_summary OWNER TO postgres;

--
-- Name: ai_economy_agents ai_economy_agents_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.ai_economy_agents ALTER COLUMN ai_economy_agents_id SET DEFAULT nextval('public.ai_economy_agents_ai_economy_agents_id_seq'::regclass);


--
-- Name: alignment_band alignment_band_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.alignment_band ALTER COLUMN alignment_band_id SET DEFAULT nextval('public.alignment_band_alignment_band_id_seq'::regclass);


--
-- Name: anomaly_reports anomaly_reports_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.anomaly_reports ALTER COLUMN anomaly_reports_id SET DEFAULT nextval('public.anomaly_reports_anomaly_reports_id_seq'::regclass);


--
-- Name: bank_accounts bank_accounts_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.bank_accounts ALTER COLUMN bank_accounts_id SET DEFAULT nextval('public.bank_accounts_bank_accounts_id_seq'::regclass);


--
-- Name: bank_fee_schedules bank_fee_schedules_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.bank_fee_schedules ALTER COLUMN bank_fee_schedules_id SET DEFAULT nextval('public.bank_fee_schedules_bank_fee_schedules_id_seq'::regclass);


--
-- Name: bank_flags player_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.bank_flags ALTER COLUMN player_id SET DEFAULT nextval('public.bank_flags_player_id_seq'::regclass);


--
-- Name: bank_interest_policy bank_interest_policy_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.bank_interest_policy ALTER COLUMN bank_interest_policy_id SET DEFAULT nextval('public.bank_interest_policy_bank_interest_policy_id_seq'::regclass);


--
-- Name: bank_orders bank_orders_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.bank_orders ALTER COLUMN bank_orders_id SET DEFAULT nextval('public.bank_orders_bank_orders_id_seq'::regclass);


--
-- Name: bank_transactions bank_transactions_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.bank_transactions ALTER COLUMN bank_transactions_id SET DEFAULT nextval('public.bank_transactions_bank_transactions_id_seq'::regclass);


--
-- Name: black_accounts black_accounts_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.black_accounts ALTER COLUMN black_accounts_id SET DEFAULT nextval('public.black_accounts_black_accounts_id_seq'::regclass);


--
-- Name: bounties bounties_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.bounties ALTER COLUMN bounties_id SET DEFAULT nextval('public.bounties_bounties_id_seq'::regclass);


--
-- Name: charities charities_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.charities ALTER COLUMN charities_id SET DEFAULT nextval('public.charities_charities_id_seq'::regclass);


--
-- Name: charters charters_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.charters ALTER COLUMN charters_id SET DEFAULT nextval('public.charters_charters_id_seq'::regclass);


--
-- Name: citadels citadel_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.citadels ALTER COLUMN citadel_id SET DEFAULT nextval('public.citadels_citadel_id_seq'::regclass);


--
-- Name: clusters clusters_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.clusters ALTER COLUMN clusters_id SET DEFAULT nextval('public.clusters_clusters_id_seq'::regclass);


--
-- Name: collateral collateral_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.collateral ALTER COLUMN collateral_id SET DEFAULT nextval('public.collateral_collateral_id_seq'::regclass);


--
-- Name: commission commission_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.commission ALTER COLUMN commission_id SET DEFAULT nextval('public.commission_commission_id_seq'::regclass);


--
-- Name: commodities commodities_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.commodities ALTER COLUMN commodities_id SET DEFAULT nextval('public.commodities_commodities_id_seq'::regclass);


--
-- Name: commodity_orders commodity_orders_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.commodity_orders ALTER COLUMN commodity_orders_id SET DEFAULT nextval('public.commodity_orders_commodity_orders_id_seq'::regclass);


--
-- Name: commodity_trades id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.commodity_trades ALTER COLUMN id SET DEFAULT nextval('public.commodity_trades_id_seq'::regclass);


--
-- Name: contracts_illicit contracts_illicit_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.contracts_illicit ALTER COLUMN contracts_illicit_id SET DEFAULT nextval('public.contracts_illicit_contracts_illicit_id_seq'::regclass);


--
-- Name: corp_accounts corp_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.corp_accounts ALTER COLUMN corp_id SET DEFAULT nextval('public.corp_accounts_corp_id_seq'::regclass);


--
-- Name: corp_interest_policy id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.corp_interest_policy ALTER COLUMN id SET DEFAULT nextval('public.corp_interest_policy_id_seq'::regclass);


--
-- Name: corp_invites corp_invites_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.corp_invites ALTER COLUMN corp_invites_id SET DEFAULT nextval('public.corp_invites_corp_invites_id_seq'::regclass);


--
-- Name: corp_log id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.corp_log ALTER COLUMN id SET DEFAULT nextval('public.corp_log_id_seq'::regclass);


--
-- Name: corp_mail corp_mail_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.corp_mail ALTER COLUMN corp_mail_id SET DEFAULT nextval('public.corp_mail_corp_mail_id_seq'::regclass);


--
-- Name: corp_recruiting corp_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.corp_recruiting ALTER COLUMN corp_id SET DEFAULT nextval('public.corp_recruiting_corp_id_seq'::regclass);


--
-- Name: corp_tx corp_tx_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.corp_tx ALTER COLUMN corp_tx_id SET DEFAULT nextval('public.corp_tx_corp_tx_id_seq'::regclass);


--
-- Name: corporations corporation_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.corporations ALTER COLUMN corporation_id SET DEFAULT nextval('public.corporations_corporation_id_seq'::regclass);


--
-- Name: cron_tasks cron_tasks_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.cron_tasks ALTER COLUMN cron_tasks_id SET DEFAULT nextval('public.cron_tasks_cron_tasks_id_seq'::regclass);


--
-- Name: donations donations_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.donations ALTER COLUMN donations_id SET DEFAULT nextval('public.donations_donations_id_seq'::regclass);


--
-- Name: economic_indicators economic_indicators_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.economic_indicators ALTER COLUMN economic_indicators_id SET DEFAULT nextval('public.economic_indicators_economic_indicators_id_seq'::regclass);


--
-- Name: economy_curve economy_curve_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.economy_curve ALTER COLUMN economy_curve_id SET DEFAULT nextval('public.economy_curve_economy_curve_id_seq'::regclass);


--
-- Name: economy_policies economy_policies_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.economy_policies ALTER COLUMN economy_policies_id SET DEFAULT nextval('public.economy_policies_economy_policies_id_seq'::regclass);


--
-- Name: economy_snapshots economy_snapshots_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.economy_snapshots ALTER COLUMN economy_snapshots_id SET DEFAULT nextval('public.economy_snapshots_economy_snapshots_id_seq'::regclass);


--
-- Name: eligible_tows ship_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.eligible_tows ALTER COLUMN ship_id SET DEFAULT nextval('public.eligible_tows_ship_id_seq'::regclass);


--
-- Name: engine_audit engine_audit_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.engine_audit ALTER COLUMN engine_audit_id SET DEFAULT nextval('public.engine_audit_engine_audit_id_seq'::regclass);


--
-- Name: engine_commands engine_commands_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.engine_commands ALTER COLUMN engine_commands_id SET DEFAULT nextval('public.engine_commands_engine_commands_id_seq'::regclass);


--
-- Name: engine_events engine_events_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.engine_events ALTER COLUMN engine_events_id SET DEFAULT nextval('public.engine_events_engine_events_id_seq'::regclass);


--
-- Name: engine_events_deadletter engine_events_deadletter_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.engine_events_deadletter ALTER COLUMN engine_events_deadletter_id SET DEFAULT nextval('public.engine_events_deadletter_engine_events_deadletter_id_seq'::regclass);


--
-- Name: event_triggers event_triggers_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.event_triggers ALTER COLUMN event_triggers_id SET DEFAULT nextval('public.event_triggers_event_triggers_id_seq'::regclass);


--
-- Name: expedition_returns expedition_returns_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.expedition_returns ALTER COLUMN expedition_returns_id SET DEFAULT nextval('public.expedition_returns_expedition_returns_id_seq'::regclass);


--
-- Name: expeditions expeditions_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.expeditions ALTER COLUMN expeditions_id SET DEFAULT nextval('public.expeditions_expeditions_id_seq'::regclass);


--
-- Name: fences fences_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.fences ALTER COLUMN fences_id SET DEFAULT nextval('public.fences_fences_id_seq'::regclass);


--
-- Name: fines fines_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.fines ALTER COLUMN fines_id SET DEFAULT nextval('public.fines_fines_id_seq'::regclass);


--
-- Name: futures_contracts futures_contracts_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.futures_contracts ALTER COLUMN futures_contracts_id SET DEFAULT nextval('public.futures_contracts_futures_contracts_id_seq'::regclass);


--
-- Name: gov_accounts gov_accounts_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.gov_accounts ALTER COLUMN gov_accounts_id SET DEFAULT nextval('public.gov_accounts_gov_accounts_id_seq'::regclass);


--
-- Name: grants grants_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.grants ALTER COLUMN grants_id SET DEFAULT nextval('public.grants_grants_id_seq'::regclass);


--
-- Name: guild_dues guild_dues_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.guild_dues ALTER COLUMN guild_dues_id SET DEFAULT nextval('public.guild_dues_guild_dues_id_seq'::regclass);


--
-- Name: guilds guilds_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.guilds ALTER COLUMN guilds_id SET DEFAULT nextval('public.guilds_guilds_id_seq'::regclass);


--
-- Name: hardware_items hardware_items_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.hardware_items ALTER COLUMN hardware_items_id SET DEFAULT nextval('public.hardware_items_hardware_items_id_seq'::regclass);


--
-- Name: insurance_claims insurance_claims_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.insurance_claims ALTER COLUMN insurance_claims_id SET DEFAULT nextval('public.insurance_claims_insurance_claims_id_seq'::regclass);


--
-- Name: insurance_funds fund_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.insurance_funds ALTER COLUMN fund_id SET DEFAULT nextval('public.insurance_funds_fund_id_seq'::regclass);


--
-- Name: insurance_policies insurance_policies_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.insurance_policies ALTER COLUMN insurance_policies_id SET DEFAULT nextval('public.insurance_policies_insurance_policies_id_seq'::regclass);


--
-- Name: laundering_ops laundering_ops_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.laundering_ops ALTER COLUMN laundering_ops_id SET DEFAULT nextval('public.laundering_ops_laundering_ops_id_seq'::regclass);


--
-- Name: law_enforcement law_enforcement_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.law_enforcement ALTER COLUMN law_enforcement_id SET DEFAULT nextval('public.law_enforcement_law_enforcement_id_seq'::regclass);


--
-- Name: limpet_attached limpet_attached_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.limpet_attached ALTER COLUMN limpet_attached_id SET DEFAULT nextval('public.limpet_attached_limpet_attached_id_seq'::regclass);


--
-- Name: loan_payments loan_payments_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.loan_payments ALTER COLUMN loan_payments_id SET DEFAULT nextval('public.loan_payments_loan_payments_id_seq'::regclass);


--
-- Name: loans loans_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.loans ALTER COLUMN loans_id SET DEFAULT nextval('public.loans_loans_id_seq'::regclass);


--
-- Name: longest_tunnels tunnel_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.longest_tunnels ALTER COLUMN tunnel_id SET DEFAULT nextval('public.longest_tunnels_tunnel_id_seq'::regclass);


--
-- Name: mail mail_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.mail ALTER COLUMN mail_id SET DEFAULT nextval('public.mail_mail_id_seq'::regclass);


--
-- Name: msl_sectors sector_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.msl_sectors ALTER COLUMN sector_id SET DEFAULT nextval('public.msl_sectors_sector_id_seq'::regclass);


--
-- Name: news_feed news_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.news_feed ALTER COLUMN news_id SET DEFAULT nextval('public.news_feed_news_id_seq'::regclass);


--
-- Name: planets planet_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.planets ALTER COLUMN planet_id SET DEFAULT nextval('public.planets_planet_id_seq'::regclass);


--
-- Name: planettypes planettypes_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.planettypes ALTER COLUMN planettypes_id SET DEFAULT nextval('public.planettypes_planettypes_id_seq'::regclass);


--
-- Name: player_bookmarks player_bookmarks_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.player_bookmarks ALTER COLUMN player_bookmarks_id SET DEFAULT nextval('public.player_bookmarks_player_bookmarks_id_seq'::regclass);


--
-- Name: player_last_rob player_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.player_last_rob ALTER COLUMN player_id SET DEFAULT nextval('public.player_last_rob_player_id_seq'::regclass);


--
-- Name: player_notes player_notes_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.player_notes ALTER COLUMN player_notes_id SET DEFAULT nextval('public.player_notes_player_notes_id_seq'::regclass);


--
-- Name: player_types type; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.player_types ALTER COLUMN type SET DEFAULT nextval('public.player_types_type_seq'::regclass);


--
-- Name: players player_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.players ALTER COLUMN player_id SET DEFAULT nextval('public.players_player_id_seq'::regclass);


--
-- Name: podded_status player_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.podded_status ALTER COLUMN player_id SET DEFAULT nextval('public.podded_status_player_id_seq'::regclass);


--
-- Name: port_trade port_trade_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.port_trade ALTER COLUMN port_trade_id SET DEFAULT nextval('public.port_trade_port_trade_id_seq'::regclass);


--
-- Name: ports port_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.ports ALTER COLUMN port_id SET DEFAULT nextval('public.ports_port_id_seq'::regclass);


--
-- Name: research_projects research_projects_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.research_projects ALTER COLUMN research_projects_id SET DEFAULT nextval('public.research_projects_research_projects_id_seq'::regclass);


--
-- Name: research_results research_results_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.research_results ALTER COLUMN research_results_id SET DEFAULT nextval('public.research_results_research_results_id_seq'::regclass);


--
-- Name: risk_profiles risk_profiles_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.risk_profiles ALTER COLUMN risk_profiles_id SET DEFAULT nextval('public.risk_profiles_risk_profiles_id_seq'::regclass);


--
-- Name: sector_assets sector_assets_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.sector_assets ALTER COLUMN sector_assets_id SET DEFAULT nextval('public.sector_assets_sector_assets_id_seq'::regclass);


--
-- Name: sector_gdp sector_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.sector_gdp ALTER COLUMN sector_id SET DEFAULT nextval('public.sector_gdp_sector_id_seq'::regclass);


--
-- Name: sectors sector_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.sectors ALTER COLUMN sector_id SET DEFAULT nextval('public.sectors_sector_id_seq'::regclass);


--
-- Name: ship_roles role_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.ship_roles ALTER COLUMN role_id SET DEFAULT nextval('public.ship_roles_role_id_seq'::regclass);


--
-- Name: ships ship_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.ships ALTER COLUMN ship_id SET DEFAULT nextval('public.ships_ship_id_seq'::regclass);


--
-- Name: shiptypes shiptypes_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.shiptypes ALTER COLUMN shiptypes_id SET DEFAULT nextval('public.shiptypes_shiptypes_id_seq'::regclass);


--
-- Name: stardock_assets sector_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.stardock_assets ALTER COLUMN sector_id SET DEFAULT nextval('public.stardock_assets_sector_id_seq'::regclass);


--
-- Name: stock_dividends id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.stock_dividends ALTER COLUMN id SET DEFAULT nextval('public.stock_dividends_id_seq'::regclass);


--
-- Name: stock_indices id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.stock_indices ALTER COLUMN id SET DEFAULT nextval('public.stock_indices_id_seq'::regclass);


--
-- Name: stock_orders id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.stock_orders ALTER COLUMN id SET DEFAULT nextval('public.stock_orders_id_seq'::regclass);


--
-- Name: stock_trades id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.stock_trades ALTER COLUMN id SET DEFAULT nextval('public.stock_trades_id_seq'::regclass);


--
-- Name: stocks id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.stocks ALTER COLUMN id SET DEFAULT nextval('public.stocks_id_seq'::regclass);


--
-- Name: subscriptions subscriptions_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.subscriptions ALTER COLUMN subscriptions_id SET DEFAULT nextval('public.subscriptions_subscriptions_id_seq'::regclass);


--
-- Name: subspace subspace_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.subspace ALTER COLUMN subspace_id SET DEFAULT nextval('public.subspace_subspace_id_seq'::regclass);


--
-- Name: subspace_cursors player_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.subspace_cursors ALTER COLUMN player_id SET DEFAULT nextval('public.subspace_cursors_player_id_seq'::regclass);


--
-- Name: system_events system_events_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.system_events ALTER COLUMN system_events_id SET DEFAULT nextval('public.system_events_system_events_id_seq'::regclass);


--
-- Name: system_notice system_notice_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.system_notice ALTER COLUMN system_notice_id SET DEFAULT nextval('public.system_notice_system_notice_id_seq'::regclass);


--
-- Name: tavern_deadpool_bets tavern_deadpool_bets_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.tavern_deadpool_bets ALTER COLUMN tavern_deadpool_bets_id SET DEFAULT nextval('public.tavern_deadpool_bets_tavern_deadpool_bets_id_seq'::regclass);


--
-- Name: tavern_graffiti tavern_graffiti_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.tavern_graffiti ALTER COLUMN tavern_graffiti_id SET DEFAULT nextval('public.tavern_graffiti_tavern_graffiti_id_seq'::regclass);


--
-- Name: tavern_loans player_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.tavern_loans ALTER COLUMN player_id SET DEFAULT nextval('public.tavern_loans_player_id_seq'::regclass);


--
-- Name: tavern_lottery_tickets tavern_lottery_tickets_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.tavern_lottery_tickets ALTER COLUMN tavern_lottery_tickets_id SET DEFAULT nextval('public.tavern_lottery_tickets_tavern_lottery_tickets_id_seq'::regclass);


--
-- Name: tavern_names tavern_names_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.tavern_names ALTER COLUMN tavern_names_id SET DEFAULT nextval('public.tavern_names_tavern_names_id_seq'::regclass);


--
-- Name: tavern_notices tavern_notices_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.tavern_notices ALTER COLUMN tavern_notices_id SET DEFAULT nextval('public.tavern_notices_tavern_notices_id_seq'::regclass);


--
-- Name: tavern_raffle_state tavern_raffle_state_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.tavern_raffle_state ALTER COLUMN tavern_raffle_state_id SET DEFAULT nextval('public.tavern_raffle_state_tavern_raffle_state_id_seq'::regclass);


--
-- Name: tavern_settings tavern_settings_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.tavern_settings ALTER COLUMN tavern_settings_id SET DEFAULT nextval('public.tavern_settings_tavern_settings_id_seq'::regclass);


--
-- Name: taverns sector_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.taverns ALTER COLUMN sector_id SET DEFAULT nextval('public.taverns_sector_id_seq'::regclass);


--
-- Name: tax_ledgers tax_ledgers_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.tax_ledgers ALTER COLUMN tax_ledgers_id SET DEFAULT nextval('public.tax_ledgers_tax_ledgers_id_seq'::regclass);


--
-- Name: tax_policies tax_policies_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.tax_policies ALTER COLUMN tax_policies_id SET DEFAULT nextval('public.tax_policies_tax_policies_id_seq'::regclass);


--
-- Name: temples temples_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.temples ALTER COLUMN temples_id SET DEFAULT nextval('public.temples_temples_id_seq'::regclass);


--
-- Name: trade_log trade_log_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.trade_log ALTER COLUMN trade_log_id SET DEFAULT nextval('public.trade_log_trade_log_id_seq'::regclass);


--
-- Name: traps id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.traps ALTER COLUMN id SET DEFAULT nextval('public.traps_id_seq'::regclass);


--
-- Name: warehouses warehouses_id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.warehouses ALTER COLUMN warehouses_id SET DEFAULT nextval('public.warehouses_warehouses_id_seq'::regclass);


--
-- Name: ai_economy_agents ai_economy_agents_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.ai_economy_agents
    ADD CONSTRAINT ai_economy_agents_pkey PRIMARY KEY (ai_economy_agents_id);


--
-- Name: alignment_band alignment_band_code_key; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.alignment_band
    ADD CONSTRAINT alignment_band_code_key UNIQUE (code);


--
-- Name: alignment_band alignment_band_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.alignment_band
    ADD CONSTRAINT alignment_band_pkey PRIMARY KEY (alignment_band_id);


--
-- Name: anomaly_reports anomaly_reports_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.anomaly_reports
    ADD CONSTRAINT anomaly_reports_pkey PRIMARY KEY (anomaly_reports_id);


--
-- Name: bank_accounts bank_accounts_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.bank_accounts
    ADD CONSTRAINT bank_accounts_pkey PRIMARY KEY (bank_accounts_id);


--
-- Name: bank_fee_schedules bank_fee_schedules_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.bank_fee_schedules
    ADD CONSTRAINT bank_fee_schedules_pkey PRIMARY KEY (bank_fee_schedules_id);


--
-- Name: bank_flags bank_flags_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.bank_flags
    ADD CONSTRAINT bank_flags_pkey PRIMARY KEY (player_id);


--
-- Name: bank_interest_policy bank_interest_policy_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.bank_interest_policy
    ADD CONSTRAINT bank_interest_policy_pkey PRIMARY KEY (bank_interest_policy_id);


--
-- Name: bank_orders bank_orders_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.bank_orders
    ADD CONSTRAINT bank_orders_pkey PRIMARY KEY (bank_orders_id);


--
-- Name: bank_transactions bank_transactions_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.bank_transactions
    ADD CONSTRAINT bank_transactions_pkey PRIMARY KEY (bank_transactions_id);


--
-- Name: black_accounts black_accounts_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.black_accounts
    ADD CONSTRAINT black_accounts_pkey PRIMARY KEY (black_accounts_id);


--
-- Name: bounties bounties_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.bounties
    ADD CONSTRAINT bounties_pkey PRIMARY KEY (bounties_id);


--
-- Name: charities charities_name_key; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.charities
    ADD CONSTRAINT charities_name_key UNIQUE (name);


--
-- Name: charities charities_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.charities
    ADD CONSTRAINT charities_pkey PRIMARY KEY (charities_id);


--
-- Name: charters charters_name_key; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.charters
    ADD CONSTRAINT charters_name_key UNIQUE (name);


--
-- Name: charters charters_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.charters
    ADD CONSTRAINT charters_pkey PRIMARY KEY (charters_id);


--
-- Name: citadel_requirements citadel_requirements_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.citadel_requirements
    ADD CONSTRAINT citadel_requirements_pkey PRIMARY KEY (planet_type_id, citadel_level);


--
-- Name: citadels citadels_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.citadels
    ADD CONSTRAINT citadels_pkey PRIMARY KEY (citadel_id);


--
-- Name: citadels citadels_planet_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.citadels
    ADD CONSTRAINT citadels_planet_id_key UNIQUE (planet_id);


--
-- Name: cluster_commodity_index cluster_commodity_index_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.cluster_commodity_index
    ADD CONSTRAINT cluster_commodity_index_pkey PRIMARY KEY (cluster_id, commodity_code);


--
-- Name: cluster_player_status cluster_player_status_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.cluster_player_status
    ADD CONSTRAINT cluster_player_status_pkey PRIMARY KEY (cluster_id, player_id);


--
-- Name: cluster_sectors cluster_sectors_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.cluster_sectors
    ADD CONSTRAINT cluster_sectors_pkey PRIMARY KEY (cluster_id, sector_id);


--
-- Name: clusters clusters_name_key; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.clusters
    ADD CONSTRAINT clusters_name_key UNIQUE (name);


--
-- Name: clusters clusters_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.clusters
    ADD CONSTRAINT clusters_pkey PRIMARY KEY (clusters_id);


--
-- Name: collateral collateral_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.collateral
    ADD CONSTRAINT collateral_pkey PRIMARY KEY (collateral_id);


--
-- Name: commission commission_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.commission
    ADD CONSTRAINT commission_pkey PRIMARY KEY (commission_id);


--
-- Name: commodities commodities_code_key; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.commodities
    ADD CONSTRAINT commodities_code_key UNIQUE (code);


--
-- Name: commodities commodities_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.commodities
    ADD CONSTRAINT commodities_pkey PRIMARY KEY (commodities_id);


--
-- Name: commodity_orders commodity_orders_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.commodity_orders
    ADD CONSTRAINT commodity_orders_pkey PRIMARY KEY (commodity_orders_id);


--
-- Name: commodity_trades commodity_trades_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.commodity_trades
    ADD CONSTRAINT commodity_trades_pkey PRIMARY KEY (id);


--
-- Name: config config_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.config
    ADD CONSTRAINT config_pkey PRIMARY KEY (key);


--
-- Name: contracts_illicit contracts_illicit_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.contracts_illicit
    ADD CONSTRAINT contracts_illicit_pkey PRIMARY KEY (contracts_illicit_id);


--
-- Name: corp_accounts corp_accounts_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.corp_accounts
    ADD CONSTRAINT corp_accounts_pkey PRIMARY KEY (corp_id);


--
-- Name: corp_interest_policy corp_interest_policy_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.corp_interest_policy
    ADD CONSTRAINT corp_interest_policy_pkey PRIMARY KEY (id);


--
-- Name: corp_invites corp_invites_corp_id_player_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.corp_invites
    ADD CONSTRAINT corp_invites_corp_id_player_id_key UNIQUE (corp_id, player_id);


--
-- Name: corp_invites corp_invites_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.corp_invites
    ADD CONSTRAINT corp_invites_pkey PRIMARY KEY (corp_invites_id);


--
-- Name: corp_log corp_log_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.corp_log
    ADD CONSTRAINT corp_log_pkey PRIMARY KEY (id);


--
-- Name: corp_mail_cursors corp_mail_cursors_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.corp_mail_cursors
    ADD CONSTRAINT corp_mail_cursors_pkey PRIMARY KEY (corporation_id, player_id);


--
-- Name: corp_mail corp_mail_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.corp_mail
    ADD CONSTRAINT corp_mail_pkey PRIMARY KEY (corp_mail_id);


--
-- Name: corp_members corp_members_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.corp_members
    ADD CONSTRAINT corp_members_pkey PRIMARY KEY (corporation_id, player_id);


--
-- Name: corp_recruiting corp_recruiting_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.corp_recruiting
    ADD CONSTRAINT corp_recruiting_pkey PRIMARY KEY (corp_id);


--
-- Name: corp_shareholders corp_shareholders_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.corp_shareholders
    ADD CONSTRAINT corp_shareholders_pkey PRIMARY KEY (corp_id, player_id);


--
-- Name: corp_tx corp_tx_idempotency_key_key; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.corp_tx
    ADD CONSTRAINT corp_tx_idempotency_key_key UNIQUE (idempotency_key);


--
-- Name: corp_tx corp_tx_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.corp_tx
    ADD CONSTRAINT corp_tx_pkey PRIMARY KEY (corp_tx_id);


--
-- Name: corporations corporations_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.corporations
    ADD CONSTRAINT corporations_pkey PRIMARY KEY (corporation_id);


--
-- Name: credit_ratings credit_ratings_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.credit_ratings
    ADD CONSTRAINT credit_ratings_pkey PRIMARY KEY (entity_type, entity_id);


--
-- Name: cron_tasks cron_tasks_name_key; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.cron_tasks
    ADD CONSTRAINT cron_tasks_name_key UNIQUE (name);


--
-- Name: cron_tasks cron_tasks_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.cron_tasks
    ADD CONSTRAINT cron_tasks_pkey PRIMARY KEY (cron_tasks_id);


--
-- Name: currencies currencies_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.currencies
    ADD CONSTRAINT currencies_pkey PRIMARY KEY (code);


--
-- Name: donations donations_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.donations
    ADD CONSTRAINT donations_pkey PRIMARY KEY (donations_id);


--
-- Name: economic_indicators economic_indicators_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.economic_indicators
    ADD CONSTRAINT economic_indicators_pkey PRIMARY KEY (economic_indicators_id);


--
-- Name: economy_curve economy_curve_curve_name_key; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.economy_curve
    ADD CONSTRAINT economy_curve_curve_name_key UNIQUE (curve_name);


--
-- Name: economy_curve economy_curve_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.economy_curve
    ADD CONSTRAINT economy_curve_pkey PRIMARY KEY (economy_curve_id);


--
-- Name: economy_policies economy_policies_name_key; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.economy_policies
    ADD CONSTRAINT economy_policies_name_key UNIQUE (name);


--
-- Name: economy_policies economy_policies_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.economy_policies
    ADD CONSTRAINT economy_policies_pkey PRIMARY KEY (economy_policies_id);


--
-- Name: economy_snapshots economy_snapshots_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.economy_snapshots
    ADD CONSTRAINT economy_snapshots_pkey PRIMARY KEY (economy_snapshots_id);


--
-- Name: eligible_tows eligible_tows_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.eligible_tows
    ADD CONSTRAINT eligible_tows_pkey PRIMARY KEY (ship_id);


--
-- Name: engine_audit engine_audit_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.engine_audit
    ADD CONSTRAINT engine_audit_pkey PRIMARY KEY (engine_audit_id);


--
-- Name: engine_commands engine_commands_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.engine_commands
    ADD CONSTRAINT engine_commands_pkey PRIMARY KEY (engine_commands_id);


--
-- Name: engine_events_deadletter engine_events_deadletter_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.engine_events_deadletter
    ADD CONSTRAINT engine_events_deadletter_pkey PRIMARY KEY (engine_events_deadletter_id);


--
-- Name: engine_events engine_events_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.engine_events
    ADD CONSTRAINT engine_events_pkey PRIMARY KEY (engine_events_id);


--
-- Name: engine_offset engine_offset_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.engine_offset
    ADD CONSTRAINT engine_offset_pkey PRIMARY KEY (key);


--
-- Name: engine_state engine_state_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.engine_state
    ADD CONSTRAINT engine_state_pkey PRIMARY KEY (state_key);


--
-- Name: entity_stock entity_stock_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.entity_stock
    ADD CONSTRAINT entity_stock_pkey PRIMARY KEY (entity_type, entity_id, commodity_code);


--
-- Name: event_triggers event_triggers_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.event_triggers
    ADD CONSTRAINT event_triggers_pkey PRIMARY KEY (event_triggers_id);


--
-- Name: expedition_backers expedition_backers_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.expedition_backers
    ADD CONSTRAINT expedition_backers_pkey PRIMARY KEY (expedition_id, backer_type, backer_id);


--
-- Name: expedition_returns expedition_returns_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.expedition_returns
    ADD CONSTRAINT expedition_returns_pkey PRIMARY KEY (expedition_returns_id);


--
-- Name: expeditions expeditions_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.expeditions
    ADD CONSTRAINT expeditions_pkey PRIMARY KEY (expeditions_id);


--
-- Name: fences fences_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.fences
    ADD CONSTRAINT fences_pkey PRIMARY KEY (fences_id);


--
-- Name: fines fines_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.fines
    ADD CONSTRAINT fines_pkey PRIMARY KEY (fines_id);


--
-- Name: futures_contracts futures_contracts_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.futures_contracts
    ADD CONSTRAINT futures_contracts_pkey PRIMARY KEY (futures_contracts_id);


--
-- Name: gov_accounts gov_accounts_name_key; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.gov_accounts
    ADD CONSTRAINT gov_accounts_name_key UNIQUE (name);


--
-- Name: gov_accounts gov_accounts_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.gov_accounts
    ADD CONSTRAINT gov_accounts_pkey PRIMARY KEY (gov_accounts_id);


--
-- Name: grants grants_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.grants
    ADD CONSTRAINT grants_pkey PRIMARY KEY (grants_id);


--
-- Name: guild_dues guild_dues_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.guild_dues
    ADD CONSTRAINT guild_dues_pkey PRIMARY KEY (guild_dues_id);


--
-- Name: guild_memberships guild_memberships_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.guild_memberships
    ADD CONSTRAINT guild_memberships_pkey PRIMARY KEY (guild_id, member_type, member_id);


--
-- Name: guilds guilds_name_key; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.guilds
    ADD CONSTRAINT guilds_name_key UNIQUE (name);


--
-- Name: guilds guilds_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.guilds
    ADD CONSTRAINT guilds_pkey PRIMARY KEY (guilds_id);


--
-- Name: hardware_items hardware_items_code_key; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.hardware_items
    ADD CONSTRAINT hardware_items_code_key UNIQUE (code);


--
-- Name: hardware_items hardware_items_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.hardware_items
    ADD CONSTRAINT hardware_items_pkey PRIMARY KEY (hardware_items_id);


--
-- Name: idempotency idempotency_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.idempotency
    ADD CONSTRAINT idempotency_pkey PRIMARY KEY (key);


--
-- Name: insurance_claims insurance_claims_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.insurance_claims
    ADD CONSTRAINT insurance_claims_pkey PRIMARY KEY (insurance_claims_id);


--
-- Name: insurance_funds insurance_funds_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.insurance_funds
    ADD CONSTRAINT insurance_funds_pkey PRIMARY KEY (fund_id);


--
-- Name: insurance_policies insurance_policies_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.insurance_policies
    ADD CONSTRAINT insurance_policies_pkey PRIMARY KEY (insurance_policies_id);


--
-- Name: laundering_ops laundering_ops_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.laundering_ops
    ADD CONSTRAINT laundering_ops_pkey PRIMARY KEY (laundering_ops_id);


--
-- Name: law_enforcement law_enforcement_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.law_enforcement
    ADD CONSTRAINT law_enforcement_pkey PRIMARY KEY (law_enforcement_id);


--
-- Name: limpet_attached limpet_attached_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.limpet_attached
    ADD CONSTRAINT limpet_attached_pkey PRIMARY KEY (limpet_attached_id);


--
-- Name: limpet_attached limpet_attached_ship_id_owner_player_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.limpet_attached
    ADD CONSTRAINT limpet_attached_ship_id_owner_player_id_key UNIQUE (ship_id, owner_player_id);


--
-- Name: loan_payments loan_payments_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.loan_payments
    ADD CONSTRAINT loan_payments_pkey PRIMARY KEY (loan_payments_id);


--
-- Name: loans loans_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.loans
    ADD CONSTRAINT loans_pkey PRIMARY KEY (loans_id);


--
-- Name: locks locks_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.locks
    ADD CONSTRAINT locks_pkey PRIMARY KEY (lock_name);


--
-- Name: longest_tunnels longest_tunnels_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.longest_tunnels
    ADD CONSTRAINT longest_tunnels_pkey PRIMARY KEY (tunnel_id);


--
-- Name: mail mail_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.mail
    ADD CONSTRAINT mail_pkey PRIMARY KEY (mail_id);


--
-- Name: msl_sectors msl_sectors_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.msl_sectors
    ADD CONSTRAINT msl_sectors_pkey PRIMARY KEY (sector_id);


--
-- Name: news_feed news_feed_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.news_feed
    ADD CONSTRAINT news_feed_pkey PRIMARY KEY (news_id);


--
-- Name: notice_seen notice_seen_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.notice_seen
    ADD CONSTRAINT notice_seen_pkey PRIMARY KEY (notice_id, player_id);


--
-- Name: planet_goods planet_goods_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.planet_goods
    ADD CONSTRAINT planet_goods_pkey PRIMARY KEY (planet_id, commodity);


--
-- Name: planet_production planet_production_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.planet_production
    ADD CONSTRAINT planet_production_pkey PRIMARY KEY (planet_type_id, commodity_code);


--
-- Name: planets planets_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.planets
    ADD CONSTRAINT planets_pkey PRIMARY KEY (planet_id);


--
-- Name: planettypes planettypes_code_key; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.planettypes
    ADD CONSTRAINT planettypes_code_key UNIQUE (code);


--
-- Name: planettypes planettypes_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.planettypes
    ADD CONSTRAINT planettypes_pkey PRIMARY KEY (planettypes_id);


--
-- Name: player_avoid player_avoid_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.player_avoid
    ADD CONSTRAINT player_avoid_pkey PRIMARY KEY (player_id, sector_id);


--
-- Name: player_block player_block_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.player_block
    ADD CONSTRAINT player_block_pkey PRIMARY KEY (blocker_id, blocked_id);


--
-- Name: player_bookmarks player_bookmarks_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.player_bookmarks
    ADD CONSTRAINT player_bookmarks_pkey PRIMARY KEY (player_bookmarks_id);


--
-- Name: player_bookmarks player_bookmarks_player_id_name_key; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.player_bookmarks
    ADD CONSTRAINT player_bookmarks_player_id_name_key UNIQUE (player_id, name);


--
-- Name: player_last_rob player_last_rob_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.player_last_rob
    ADD CONSTRAINT player_last_rob_pkey PRIMARY KEY (player_id);


--
-- Name: player_notes player_notes_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.player_notes
    ADD CONSTRAINT player_notes_pkey PRIMARY KEY (player_notes_id);


--
-- Name: player_notes player_notes_player_id_scope_key_key; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.player_notes
    ADD CONSTRAINT player_notes_player_id_scope_key_key UNIQUE (player_id, scope, key);


--
-- Name: player_types player_types_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.player_types
    ADD CONSTRAINT player_types_pkey PRIMARY KEY (type);


--
-- Name: players players_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.players
    ADD CONSTRAINT players_pkey PRIMARY KEY (player_id);


--
-- Name: podded_status podded_status_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.podded_status
    ADD CONSTRAINT podded_status_pkey PRIMARY KEY (player_id);


--
-- Name: port_busts port_busts_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.port_busts
    ADD CONSTRAINT port_busts_pkey PRIMARY KEY (port_id, player_id);


--
-- Name: port_trade port_trade_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.port_trade
    ADD CONSTRAINT port_trade_pkey PRIMARY KEY (port_trade_id);


--
-- Name: ports ports_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.ports
    ADD CONSTRAINT ports_pkey PRIMARY KEY (port_id);


--
-- Name: research_contributors research_contributors_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.research_contributors
    ADD CONSTRAINT research_contributors_pkey PRIMARY KEY (project_id, actor_type, actor_id);


--
-- Name: research_projects research_projects_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.research_projects
    ADD CONSTRAINT research_projects_pkey PRIMARY KEY (research_projects_id);


--
-- Name: research_results research_results_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.research_results
    ADD CONSTRAINT research_results_pkey PRIMARY KEY (research_results_id);


--
-- Name: risk_profiles risk_profiles_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.risk_profiles
    ADD CONSTRAINT risk_profiles_pkey PRIMARY KEY (risk_profiles_id);


--
-- Name: s2s_keys s2s_keys_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.s2s_keys
    ADD CONSTRAINT s2s_keys_pkey PRIMARY KEY (key_id);


--
-- Name: sector_assets sector_assets_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.sector_assets
    ADD CONSTRAINT sector_assets_pkey PRIMARY KEY (sector_assets_id);


--
-- Name: sector_gdp sector_gdp_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.sector_gdp
    ADD CONSTRAINT sector_gdp_pkey PRIMARY KEY (sector_id);


--
-- Name: sector_warps sector_warps_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.sector_warps
    ADD CONSTRAINT sector_warps_pkey PRIMARY KEY (from_sector, to_sector);


--
-- Name: sectors sectors_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.sectors
    ADD CONSTRAINT sectors_pkey PRIMARY KEY (sector_id);


--
-- Name: sessions sessions_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.sessions
    ADD CONSTRAINT sessions_pkey PRIMARY KEY (token);


--
-- Name: ship_markers ship_markers_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.ship_markers
    ADD CONSTRAINT ship_markers_pkey PRIMARY KEY (ship_id, owner_player, marker_type);


--
-- Name: ship_ownership ship_ownership_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.ship_ownership
    ADD CONSTRAINT ship_ownership_pkey PRIMARY KEY (ship_id, player_id, role_id);


--
-- Name: ship_roles ship_roles_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.ship_roles
    ADD CONSTRAINT ship_roles_pkey PRIMARY KEY (role_id);


--
-- Name: ships ships_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.ships
    ADD CONSTRAINT ships_pkey PRIMARY KEY (ship_id);


--
-- Name: shiptypes shiptypes_name_key; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.shiptypes
    ADD CONSTRAINT shiptypes_name_key UNIQUE (name);


--
-- Name: shiptypes shiptypes_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.shiptypes
    ADD CONSTRAINT shiptypes_pkey PRIMARY KEY (shiptypes_id);


--
-- Name: shipyard_inventory shipyard_inventory_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.shipyard_inventory
    ADD CONSTRAINT shipyard_inventory_pkey PRIMARY KEY (port_id, ship_type_id);


--
-- Name: stardock_assets stardock_assets_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.stardock_assets
    ADD CONSTRAINT stardock_assets_pkey PRIMARY KEY (sector_id);


--
-- Name: stock_dividends stock_dividends_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.stock_dividends
    ADD CONSTRAINT stock_dividends_pkey PRIMARY KEY (id);


--
-- Name: stock_index_members stock_index_members_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.stock_index_members
    ADD CONSTRAINT stock_index_members_pkey PRIMARY KEY (index_id, stock_id);


--
-- Name: stock_indices stock_indices_name_key; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.stock_indices
    ADD CONSTRAINT stock_indices_name_key UNIQUE (name);


--
-- Name: stock_indices stock_indices_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.stock_indices
    ADD CONSTRAINT stock_indices_pkey PRIMARY KEY (id);


--
-- Name: stock_orders stock_orders_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.stock_orders
    ADD CONSTRAINT stock_orders_pkey PRIMARY KEY (id);


--
-- Name: stock_trades stock_trades_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.stock_trades
    ADD CONSTRAINT stock_trades_pkey PRIMARY KEY (id);


--
-- Name: stocks stocks_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.stocks
    ADD CONSTRAINT stocks_pkey PRIMARY KEY (id);


--
-- Name: stocks stocks_ticker_key; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.stocks
    ADD CONSTRAINT stocks_ticker_key UNIQUE (ticker);


--
-- Name: subscriptions subscriptions_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.subscriptions
    ADD CONSTRAINT subscriptions_pkey PRIMARY KEY (subscriptions_id);


--
-- Name: subscriptions subscriptions_player_id_event_type_key; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.subscriptions
    ADD CONSTRAINT subscriptions_player_id_event_type_key UNIQUE (player_id, event_type);


--
-- Name: subspace_cursors subspace_cursors_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.subspace_cursors
    ADD CONSTRAINT subspace_cursors_pkey PRIMARY KEY (player_id);


--
-- Name: subspace subspace_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.subspace
    ADD CONSTRAINT subspace_pkey PRIMARY KEY (subspace_id);


--
-- Name: system_events system_events_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.system_events
    ADD CONSTRAINT system_events_pkey PRIMARY KEY (system_events_id);


--
-- Name: system_notice system_notice_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.system_notice
    ADD CONSTRAINT system_notice_pkey PRIMARY KEY (system_notice_id);


--
-- Name: tavern_deadpool_bets tavern_deadpool_bets_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.tavern_deadpool_bets
    ADD CONSTRAINT tavern_deadpool_bets_pkey PRIMARY KEY (tavern_deadpool_bets_id);


--
-- Name: tavern_graffiti tavern_graffiti_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.tavern_graffiti
    ADD CONSTRAINT tavern_graffiti_pkey PRIMARY KEY (tavern_graffiti_id);


--
-- Name: tavern_loans tavern_loans_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.tavern_loans
    ADD CONSTRAINT tavern_loans_pkey PRIMARY KEY (player_id);


--
-- Name: tavern_lottery_state tavern_lottery_state_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.tavern_lottery_state
    ADD CONSTRAINT tavern_lottery_state_pkey PRIMARY KEY (draw_date);


--
-- Name: tavern_lottery_tickets tavern_lottery_tickets_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.tavern_lottery_tickets
    ADD CONSTRAINT tavern_lottery_tickets_pkey PRIMARY KEY (tavern_lottery_tickets_id);


--
-- Name: tavern_names tavern_names_name_key; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.tavern_names
    ADD CONSTRAINT tavern_names_name_key UNIQUE (name);


--
-- Name: tavern_names tavern_names_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.tavern_names
    ADD CONSTRAINT tavern_names_pkey PRIMARY KEY (tavern_names_id);


--
-- Name: tavern_notices tavern_notices_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.tavern_notices
    ADD CONSTRAINT tavern_notices_pkey PRIMARY KEY (tavern_notices_id);


--
-- Name: tavern_raffle_state tavern_raffle_state_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.tavern_raffle_state
    ADD CONSTRAINT tavern_raffle_state_pkey PRIMARY KEY (tavern_raffle_state_id);


--
-- Name: tavern_settings tavern_settings_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.tavern_settings
    ADD CONSTRAINT tavern_settings_pkey PRIMARY KEY (tavern_settings_id);


--
-- Name: taverns taverns_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.taverns
    ADD CONSTRAINT taverns_pkey PRIMARY KEY (sector_id);


--
-- Name: tax_ledgers tax_ledgers_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.tax_ledgers
    ADD CONSTRAINT tax_ledgers_pkey PRIMARY KEY (tax_ledgers_id);


--
-- Name: tax_policies tax_policies_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.tax_policies
    ADD CONSTRAINT tax_policies_pkey PRIMARY KEY (tax_policies_id);


--
-- Name: temples temples_name_key; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.temples
    ADD CONSTRAINT temples_name_key UNIQUE (name);


--
-- Name: temples temples_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.temples
    ADD CONSTRAINT temples_pkey PRIMARY KEY (temples_id);


--
-- Name: trade_idempotency trade_idempotency_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.trade_idempotency
    ADD CONSTRAINT trade_idempotency_pkey PRIMARY KEY (key);


--
-- Name: trade_log trade_log_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.trade_log
    ADD CONSTRAINT trade_log_pkey PRIMARY KEY (trade_log_id);


--
-- Name: traps traps_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.traps
    ADD CONSTRAINT traps_pkey PRIMARY KEY (id);


--
-- Name: turns turns_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.turns
    ADD CONSTRAINT turns_pkey PRIMARY KEY (player_id);


--
-- Name: warehouses warehouses_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.warehouses
    ADD CONSTRAINT warehouses_pkey PRIMARY KEY (warehouses_id);


--
-- Name: idx_avoid_player; Type: INDEX; Schema: public; Owner: postgres
--

CREATE INDEX idx_avoid_player ON public.player_avoid USING btree (player_id);


--
-- Name: idx_bank_accounts_owner; Type: INDEX; Schema: public; Owner: postgres
--

CREATE UNIQUE INDEX idx_bank_accounts_owner ON public.bank_accounts USING btree (owner_type, owner_id, currency);


--
-- Name: idx_bank_fee_active; Type: INDEX; Schema: public; Owner: postgres
--

CREATE INDEX idx_bank_fee_active ON public.bank_fee_schedules USING btree (tx_type, owner_type, currency, effective_from, effective_to);


--
-- Name: idx_bank_transactions_account_ts; Type: INDEX; Schema: public; Owner: postgres
--

CREATE INDEX idx_bank_transactions_account_ts ON public.bank_transactions USING btree (account_id, ts DESC);


--
-- Name: idx_bank_transactions_idem; Type: INDEX; Schema: public; Owner: postgres
--

CREATE UNIQUE INDEX idx_bank_transactions_idem ON public.bank_transactions USING btree (account_id, idempotency_key) WHERE (idempotency_key IS NOT NULL);


--
-- Name: idx_bank_transactions_tx_group; Type: INDEX; Schema: public; Owner: postgres
--

CREATE INDEX idx_bank_transactions_tx_group ON public.bank_transactions USING btree (tx_group_id);


--
-- Name: idx_bookmarks_player; Type: INDEX; Schema: public; Owner: postgres
--

CREATE INDEX idx_bookmarks_player ON public.player_bookmarks USING btree (player_id);


--
-- Name: idx_citadels_planet; Type: INDEX; Schema: public; Owner: postgres
--

CREATE INDEX idx_citadels_planet ON public.citadels USING btree (planet_id);


--
-- Name: idx_cluster_sectors_sector; Type: INDEX; Schema: public; Owner: postgres
--

CREATE INDEX idx_cluster_sectors_sector ON public.cluster_sectors USING btree (sector_id);


--
-- Name: idx_commodity_orders_comm; Type: INDEX; Schema: public; Owner: postgres
--

CREATE INDEX idx_commodity_orders_comm ON public.commodity_orders USING btree (commodity_id, status);


--
-- Name: idx_corp_invites_corp; Type: INDEX; Schema: public; Owner: postgres
--

CREATE INDEX idx_corp_invites_corp ON public.corp_invites USING btree (corp_id);


--
-- Name: idx_corp_invites_player; Type: INDEX; Schema: public; Owner: postgres
--

CREATE INDEX idx_corp_invites_player ON public.corp_invites USING btree (player_id);


--
-- Name: idx_corp_log_corp_time; Type: INDEX; Schema: public; Owner: postgres
--

CREATE INDEX idx_corp_log_corp_time ON public.corp_log USING btree (corporation_id, created_at DESC);


--
-- Name: idx_corp_log_type; Type: INDEX; Schema: public; Owner: postgres
--

CREATE INDEX idx_corp_log_type ON public.corp_log USING btree (event_type, created_at DESC);


--
-- Name: idx_corp_mail_corp; Type: INDEX; Schema: public; Owner: postgres
--

CREATE INDEX idx_corp_mail_corp ON public.corp_mail USING btree (corporation_id, posted_at DESC);


--
-- Name: idx_corp_tx_corp_ts; Type: INDEX; Schema: public; Owner: postgres
--

CREATE INDEX idx_corp_tx_corp_ts ON public.corp_tx USING btree (corp_id, ts);


--
-- Name: idx_engine_cmds_idem; Type: INDEX; Schema: public; Owner: postgres
--

CREATE UNIQUE INDEX idx_engine_cmds_idem ON public.engine_commands USING btree (idem_key) WHERE (idem_key IS NOT NULL);


--
-- Name: idx_engine_cmds_prio_due; Type: INDEX; Schema: public; Owner: postgres
--

CREATE INDEX idx_engine_cmds_prio_due ON public.engine_commands USING btree (priority, due_at);


--
-- Name: idx_engine_cmds_status_due; Type: INDEX; Schema: public; Owner: postgres
--

CREATE INDEX idx_engine_cmds_status_due ON public.engine_commands USING btree (status, due_at);


--
-- Name: idx_engine_events_actor_ts; Type: INDEX; Schema: public; Owner: postgres
--

CREATE INDEX idx_engine_events_actor_ts ON public.engine_events USING btree (actor_player_id, ts);


--
-- Name: idx_engine_events_idem; Type: INDEX; Schema: public; Owner: postgres
--

CREATE UNIQUE INDEX idx_engine_events_idem ON public.engine_events USING btree (idem_key) WHERE (idem_key IS NOT NULL);


--
-- Name: idx_engine_events_sector_ts; Type: INDEX; Schema: public; Owner: postgres
--

CREATE INDEX idx_engine_events_sector_ts ON public.engine_events USING btree (sector_id, ts);


--
-- Name: idx_engine_events_ts; Type: INDEX; Schema: public; Owner: postgres
--

CREATE INDEX idx_engine_events_ts ON public.engine_events USING btree (ts);


--
-- Name: idx_idemp_cmd; Type: INDEX; Schema: public; Owner: postgres
--

CREATE INDEX idx_idemp_cmd ON public.idempotency USING btree (cmd);


--
-- Name: idx_locks_until; Type: INDEX; Schema: public; Owner: postgres
--

CREATE INDEX idx_locks_until ON public.locks USING btree (until_ms);


--
-- Name: idx_mail_idem_recipient; Type: INDEX; Schema: public; Owner: postgres
--

CREATE UNIQUE INDEX idx_mail_idem_recipient ON public.mail USING btree (idempotency_key, recipient_id) WHERE (idempotency_key IS NOT NULL);


--
-- Name: idx_mail_inbox; Type: INDEX; Schema: public; Owner: postgres
--

CREATE INDEX idx_mail_inbox ON public.mail USING btree (recipient_id, deleted, archived, sent_at DESC);


--
-- Name: idx_mail_sender; Type: INDEX; Schema: public; Owner: postgres
--

CREATE INDEX idx_mail_sender ON public.mail USING btree (sender_id, sent_at DESC);


--
-- Name: idx_mail_unread; Type: INDEX; Schema: public; Owner: postgres
--

CREATE INDEX idx_mail_unread ON public.mail USING btree (recipient_id, read_at);


--
-- Name: idx_notes_player; Type: INDEX; Schema: public; Owner: postgres
--

CREATE INDEX idx_notes_player ON public.player_notes USING btree (player_id);


--
-- Name: idx_notice_seen_player; Type: INDEX; Schema: public; Owner: postgres
--

CREATE INDEX idx_notice_seen_player ON public.notice_seen USING btree (player_id, seen_at DESC);


--
-- Name: idx_planets_sector; Type: INDEX; Schema: public; Owner: postgres
--

CREATE INDEX idx_planets_sector ON public.planets USING btree (sector_id);


--
-- Name: idx_player_block_blocked; Type: INDEX; Schema: public; Owner: postgres
--

CREATE INDEX idx_player_block_blocked ON public.player_block USING btree (blocked_id);


--
-- Name: idx_player_prefs_player; Type: INDEX; Schema: public; Owner: postgres
--

CREATE INDEX idx_player_prefs_player ON public.player_prefs USING btree (player_prefs_id);


--
-- Name: idx_players_name; Type: INDEX; Schema: public; Owner: postgres
--

CREATE UNIQUE INDEX idx_players_name ON public.players USING btree (name);


--
-- Name: idx_players_sector; Type: INDEX; Schema: public; Owner: postgres
--

CREATE INDEX idx_players_sector ON public.players USING btree (sector_id);


--
-- Name: idx_players_ship; Type: INDEX; Schema: public; Owner: postgres
--

CREATE INDEX idx_players_ship ON public.players USING btree (ship_id);


--
-- Name: idx_policies_holder; Type: INDEX; Schema: public; Owner: postgres
--

CREATE INDEX idx_policies_holder ON public.insurance_policies USING btree (holder_type, holder_id);


--
-- Name: idx_port_busts_player; Type: INDEX; Schema: public; Owner: postgres
--

CREATE INDEX idx_port_busts_player ON public.port_busts USING btree (player_id);


--
-- Name: idx_ports_loc; Type: INDEX; Schema: public; Owner: postgres
--

CREATE INDEX idx_ports_loc ON public.ports USING btree (sector_id);


--
-- Name: idx_ports_loc_number; Type: INDEX; Schema: public; Owner: postgres
--

CREATE UNIQUE INDEX idx_ports_loc_number ON public.ports USING btree (sector_id, number);


--
-- Name: idx_sessions_expires; Type: INDEX; Schema: public; Owner: postgres
--

CREATE INDEX idx_sessions_expires ON public.sessions USING btree (expires);


--
-- Name: idx_sessions_player; Type: INDEX; Schema: public; Owner: postgres
--

CREATE INDEX idx_sessions_player ON public.sessions USING btree (player_id);


--
-- Name: idx_ship_own_player; Type: INDEX; Schema: public; Owner: postgres
--

CREATE INDEX idx_ship_own_player ON public.ship_ownership USING btree (player_id);


--
-- Name: idx_ship_own_ship; Type: INDEX; Schema: public; Owner: postgres
--

CREATE INDEX idx_ship_own_ship ON public.ship_ownership USING btree (ship_id);


--
-- Name: idx_stock_orders_stock; Type: INDEX; Schema: public; Owner: postgres
--

CREATE INDEX idx_stock_orders_stock ON public.stock_orders USING btree (stock_id, status);


--
-- Name: idx_subs_enabled; Type: INDEX; Schema: public; Owner: postgres
--

CREATE INDEX idx_subs_enabled ON public.subscriptions USING btree (enabled);


--
-- Name: idx_subs_event; Type: INDEX; Schema: public; Owner: postgres
--

CREATE INDEX idx_subs_event ON public.subscriptions USING btree (event_type);


--
-- Name: idx_subscriptions_player; Type: INDEX; Schema: public; Owner: postgres
--

CREATE INDEX idx_subscriptions_player ON public.subscriptions USING btree (player_id, enabled);


--
-- Name: idx_subspace_time; Type: INDEX; Schema: public; Owner: postgres
--

CREATE INDEX idx_subspace_time ON public.subspace USING btree (posted_at DESC);


--
-- Name: idx_sys_events_scope; Type: INDEX; Schema: public; Owner: postgres
--

CREATE INDEX idx_sys_events_scope ON public.system_events USING btree (scope);


--
-- Name: idx_sys_events_time; Type: INDEX; Schema: public; Owner: postgres
--

CREATE INDEX idx_sys_events_time ON public.system_events USING btree (created_at DESC);


--
-- Name: idx_system_notice_active; Type: INDEX; Schema: public; Owner: postgres
--

CREATE INDEX idx_system_notice_active ON public.system_notice USING btree (expires_at, created_at DESC);


--
-- Name: idx_traps_trigger; Type: INDEX; Schema: public; Owner: postgres
--

CREATE INDEX idx_traps_trigger ON public.traps USING btree (armed, trigger_at);


--
-- Name: idx_turns_player; Type: INDEX; Schema: public; Owner: postgres
--

CREATE UNIQUE INDEX idx_turns_player ON public.turns USING btree (player_id);


--
-- Name: idx_warps_from; Type: INDEX; Schema: public; Owner: postgres
--

CREATE INDEX idx_warps_from ON public.sector_warps USING btree (from_sector);


--
-- Name: idx_warps_to; Type: INDEX; Schema: public; Owner: postgres
--

CREATE INDEX idx_warps_to ON public.sector_warps USING btree (to_sector);


--
-- Name: ix_corp_members_player; Type: INDEX; Schema: public; Owner: postgres
--

CREATE INDEX ix_corp_members_player ON public.corp_members USING btree (player_id);


--
-- Name: ix_corp_members_role; Type: INDEX; Schema: public; Owner: postgres
--

CREATE INDEX ix_corp_members_role ON public.corp_members USING btree (corporation_id, role);


--
-- Name: ix_corporations_owner; Type: INDEX; Schema: public; Owner: postgres
--

CREATE INDEX ix_corporations_owner ON public.corporations USING btree (owner_id);


--
-- Name: ix_news_feed_pub_ts; Type: INDEX; Schema: public; Owner: postgres
--

CREATE INDEX ix_news_feed_pub_ts ON public.news_feed USING btree (published_ts);


--
-- Name: ix_stardock_owner; Type: INDEX; Schema: public; Owner: postgres
--

CREATE INDEX ix_stardock_owner ON public.stardock_assets USING btree (owner_id);


--
-- Name: ix_trade_log_ts; Type: INDEX; Schema: public; Owner: postgres
--

CREATE INDEX ix_trade_log_ts ON public.trade_log USING btree ("timestamp");


--
-- Name: ix_warps_from_to; Type: INDEX; Schema: public; Owner: postgres
--

CREATE INDEX ix_warps_from_to ON public.sector_warps USING btree (from_sector, to_sector);


--
-- Name: ux_corp_name_uc; Type: INDEX; Schema: public; Owner: postgres
--

CREATE UNIQUE INDEX ux_corp_name_uc ON public.corporations USING btree (upper(name));


--
-- Name: ux_corp_tag_uc; Type: INDEX; Schema: public; Owner: postgres
--

CREATE UNIQUE INDEX ux_corp_tag_uc ON public.corporations USING btree (upper(tag)) WHERE (tag IS NOT NULL);


--
-- Name: corp_members corp_one_leader_guard; Type: TRIGGER; Schema: public; Owner: postgres
--

CREATE TRIGGER corp_one_leader_guard BEFORE INSERT ON public.corp_members FOR EACH ROW EXECUTE FUNCTION public.fn_corp_one_leader_guard();


--
-- Name: corporations corp_owner_leader_sync; Type: TRIGGER; Schema: public; Owner: postgres
--

CREATE TRIGGER corp_owner_leader_sync AFTER UPDATE OF owner_id ON public.corporations FOR EACH ROW EXECUTE FUNCTION public.fn_corp_owner_leader_sync();


--
-- Name: corporations corp_owner_must_be_member_insert; Type: TRIGGER; Schema: public; Owner: postgres
--

CREATE TRIGGER corp_owner_must_be_member_insert AFTER INSERT ON public.corporations FOR EACH ROW EXECUTE FUNCTION public.fn_corp_owner_must_be_member_insert();


--
-- Name: corporations corporations_touch_updated; Type: TRIGGER; Schema: public; Owner: postgres
--

CREATE TRIGGER corporations_touch_updated BEFORE UPDATE ON public.corporations FOR EACH ROW EXECUTE FUNCTION public.fn_corporations_touch_updated();


--
-- Name: ships ships_ai_set_installed_shields; Type: TRIGGER; Schema: public; Owner: postgres
--

CREATE TRIGGER ships_ai_set_installed_shields BEFORE INSERT ON public.ships FOR EACH ROW EXECUTE FUNCTION public.fn_ships_ai_set_installed_shields();


--
-- Name: bank_transactions trg_bank_transactions_after_insert; Type: TRIGGER; Schema: public; Owner: postgres
--

CREATE TRIGGER trg_bank_transactions_after_insert AFTER INSERT ON public.bank_transactions FOR EACH ROW EXECUTE FUNCTION public.fn_bank_transactions_after_insert();


--
-- Name: bank_transactions trg_bank_transactions_before_delete; Type: TRIGGER; Schema: public; Owner: postgres
--

CREATE TRIGGER trg_bank_transactions_before_delete BEFORE DELETE ON public.bank_transactions FOR EACH ROW EXECUTE FUNCTION public.fn_bank_transactions_before_delete();


--
-- Name: bank_transactions trg_bank_transactions_before_insert; Type: TRIGGER; Schema: public; Owner: postgres
--

CREATE TRIGGER trg_bank_transactions_before_insert BEFORE INSERT ON public.bank_transactions FOR EACH ROW EXECUTE FUNCTION public.fn_bank_transactions_before_insert();


--
-- Name: corp_tx trg_corp_tx_after_insert; Type: TRIGGER; Schema: public; Owner: postgres
--

CREATE TRIGGER trg_corp_tx_after_insert AFTER INSERT ON public.corp_tx FOR EACH ROW EXECUTE FUNCTION public.fn_corp_tx_after_insert();


--
-- Name: corp_tx trg_corp_tx_before_insert; Type: TRIGGER; Schema: public; Owner: postgres
--

CREATE TRIGGER trg_corp_tx_before_insert BEFORE INSERT ON public.corp_tx FOR EACH ROW EXECUTE FUNCTION public.fn_corp_tx_before_insert();


--
-- Name: planets trg_planets_total_cap_before_insert; Type: TRIGGER; Schema: public; Owner: postgres
--

CREATE TRIGGER trg_planets_total_cap_before_insert BEFORE INSERT ON public.planets FOR EACH ROW EXECUTE FUNCTION public.fn_planets_total_cap_before_insert();


--
-- Name: bank_accounts bank_accounts_currency_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.bank_accounts
    ADD CONSTRAINT bank_accounts_currency_fkey FOREIGN KEY (currency) REFERENCES public.currencies(code);


--
-- Name: bank_flags bank_flags_player_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.bank_flags
    ADD CONSTRAINT bank_flags_player_id_fkey FOREIGN KEY (player_id) REFERENCES public.players(player_id) ON DELETE CASCADE;


--
-- Name: bank_interest_policy bank_interest_policy_currency_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.bank_interest_policy
    ADD CONSTRAINT bank_interest_policy_currency_fkey FOREIGN KEY (currency) REFERENCES public.currencies(code);


--
-- Name: bank_orders bank_orders_currency_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.bank_orders
    ADD CONSTRAINT bank_orders_currency_fkey FOREIGN KEY (currency) REFERENCES public.currencies(code);


--
-- Name: bank_orders bank_orders_player_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.bank_orders
    ADD CONSTRAINT bank_orders_player_id_fkey FOREIGN KEY (player_id) REFERENCES public.players(player_id) ON DELETE CASCADE;


--
-- Name: bank_transactions bank_transactions_account_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.bank_transactions
    ADD CONSTRAINT bank_transactions_account_id_fkey FOREIGN KEY (account_id) REFERENCES public.bank_accounts(bank_accounts_id);


--
-- Name: citadel_requirements citadel_requirements_planet_type_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.citadel_requirements
    ADD CONSTRAINT citadel_requirements_planet_type_id_fkey FOREIGN KEY (planet_type_id) REFERENCES public.planettypes(planettypes_id) ON DELETE CASCADE;


--
-- Name: citadels citadels_owner_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.citadels
    ADD CONSTRAINT citadels_owner_id_fkey FOREIGN KEY (owner_id) REFERENCES public.players(player_id);


--
-- Name: citadels citadels_planet_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.citadels
    ADD CONSTRAINT citadels_planet_id_fkey FOREIGN KEY (planet_id) REFERENCES public.planets(planet_id) ON DELETE CASCADE;


--
-- Name: cluster_commodity_index cluster_commodity_index_cluster_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.cluster_commodity_index
    ADD CONSTRAINT cluster_commodity_index_cluster_id_fkey FOREIGN KEY (cluster_id) REFERENCES public.clusters(clusters_id) ON DELETE CASCADE;


--
-- Name: cluster_commodity_index cluster_commodity_index_commodity_code_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.cluster_commodity_index
    ADD CONSTRAINT cluster_commodity_index_commodity_code_fkey FOREIGN KEY (commodity_code) REFERENCES public.commodities(code) ON DELETE CASCADE;


--
-- Name: cluster_player_status cluster_player_status_cluster_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.cluster_player_status
    ADD CONSTRAINT cluster_player_status_cluster_id_fkey FOREIGN KEY (cluster_id) REFERENCES public.clusters(clusters_id);


--
-- Name: cluster_player_status cluster_player_status_player_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.cluster_player_status
    ADD CONSTRAINT cluster_player_status_player_id_fkey FOREIGN KEY (player_id) REFERENCES public.players(player_id);


--
-- Name: cluster_sectors cluster_sectors_cluster_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.cluster_sectors
    ADD CONSTRAINT cluster_sectors_cluster_id_fkey FOREIGN KEY (cluster_id) REFERENCES public.clusters(clusters_id) ON DELETE CASCADE;


--
-- Name: cluster_sectors cluster_sectors_sector_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.cluster_sectors
    ADD CONSTRAINT cluster_sectors_sector_id_fkey FOREIGN KEY (sector_id) REFERENCES public.sectors(sector_id) ON DELETE CASCADE;


--
-- Name: collateral collateral_loan_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.collateral
    ADD CONSTRAINT collateral_loan_id_fkey FOREIGN KEY (loan_id) REFERENCES public.loans(loans_id) ON DELETE CASCADE;


--
-- Name: commodity_orders commodity_orders_commodity_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.commodity_orders
    ADD CONSTRAINT commodity_orders_commodity_id_fkey FOREIGN KEY (commodity_id) REFERENCES public.commodities(commodities_id) ON DELETE CASCADE;


--
-- Name: commodity_trades commodity_trades_commodity_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.commodity_trades
    ADD CONSTRAINT commodity_trades_commodity_id_fkey FOREIGN KEY (commodity_id) REFERENCES public.commodities(commodities_id) ON DELETE CASCADE;


--
-- Name: contracts_illicit contracts_illicit_escrow_black_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.contracts_illicit
    ADD CONSTRAINT contracts_illicit_escrow_black_id_fkey FOREIGN KEY (escrow_black_id) REFERENCES public.black_accounts(black_accounts_id) ON DELETE SET NULL;


--
-- Name: corp_accounts corp_accounts_corp_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.corp_accounts
    ADD CONSTRAINT corp_accounts_corp_id_fkey FOREIGN KEY (corp_id) REFERENCES public.corporations(corporation_id) ON DELETE CASCADE;


--
-- Name: corp_accounts corp_accounts_currency_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.corp_accounts
    ADD CONSTRAINT corp_accounts_currency_fkey FOREIGN KEY (currency) REFERENCES public.currencies(code);


--
-- Name: corp_interest_policy corp_interest_policy_currency_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.corp_interest_policy
    ADD CONSTRAINT corp_interest_policy_currency_fkey FOREIGN KEY (currency) REFERENCES public.currencies(code);


--
-- Name: corp_invites corp_invites_corp_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.corp_invites
    ADD CONSTRAINT corp_invites_corp_id_fkey FOREIGN KEY (corp_id) REFERENCES public.corporations(corporation_id) ON DELETE CASCADE;


--
-- Name: corp_invites corp_invites_player_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.corp_invites
    ADD CONSTRAINT corp_invites_player_id_fkey FOREIGN KEY (player_id) REFERENCES public.players(player_id) ON DELETE CASCADE;


--
-- Name: corp_log corp_log_actor_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.corp_log
    ADD CONSTRAINT corp_log_actor_id_fkey FOREIGN KEY (actor_id) REFERENCES public.players(player_id) ON DELETE SET NULL;


--
-- Name: corp_log corp_log_corporation_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.corp_log
    ADD CONSTRAINT corp_log_corporation_id_fkey FOREIGN KEY (corporation_id) REFERENCES public.corporations(corporation_id) ON DELETE CASCADE;


--
-- Name: corp_mail corp_mail_corporation_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.corp_mail
    ADD CONSTRAINT corp_mail_corporation_id_fkey FOREIGN KEY (corporation_id) REFERENCES public.corporations(corporation_id) ON DELETE CASCADE;


--
-- Name: corp_mail_cursors corp_mail_cursors_corporation_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.corp_mail_cursors
    ADD CONSTRAINT corp_mail_cursors_corporation_id_fkey FOREIGN KEY (corporation_id) REFERENCES public.corporations(corporation_id) ON DELETE CASCADE;


--
-- Name: corp_mail_cursors corp_mail_cursors_player_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.corp_mail_cursors
    ADD CONSTRAINT corp_mail_cursors_player_id_fkey FOREIGN KEY (player_id) REFERENCES public.players(player_id) ON DELETE CASCADE;


--
-- Name: corp_mail corp_mail_sender_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.corp_mail
    ADD CONSTRAINT corp_mail_sender_id_fkey FOREIGN KEY (sender_id) REFERENCES public.players(player_id) ON DELETE SET NULL;


--
-- Name: corp_members corp_members_corporation_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.corp_members
    ADD CONSTRAINT corp_members_corporation_id_fkey FOREIGN KEY (corporation_id) REFERENCES public.corporations(corporation_id) ON UPDATE CASCADE ON DELETE CASCADE;


--
-- Name: corp_members corp_members_player_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.corp_members
    ADD CONSTRAINT corp_members_player_id_fkey FOREIGN KEY (player_id) REFERENCES public.players(player_id);


--
-- Name: corp_recruiting corp_recruiting_corp_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.corp_recruiting
    ADD CONSTRAINT corp_recruiting_corp_id_fkey FOREIGN KEY (corp_id) REFERENCES public.corporations(corporation_id);


--
-- Name: corp_shareholders corp_shareholders_corp_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.corp_shareholders
    ADD CONSTRAINT corp_shareholders_corp_id_fkey FOREIGN KEY (corp_id) REFERENCES public.corporations(corporation_id) ON DELETE CASCADE;


--
-- Name: corp_shareholders corp_shareholders_player_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.corp_shareholders
    ADD CONSTRAINT corp_shareholders_player_id_fkey FOREIGN KEY (player_id) REFERENCES public.players(player_id) ON DELETE CASCADE;


--
-- Name: corp_tx corp_tx_corp_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.corp_tx
    ADD CONSTRAINT corp_tx_corp_id_fkey FOREIGN KEY (corp_id) REFERENCES public.corporations(corporation_id) ON DELETE CASCADE;


--
-- Name: corp_tx corp_tx_currency_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.corp_tx
    ADD CONSTRAINT corp_tx_currency_fkey FOREIGN KEY (currency) REFERENCES public.currencies(code);


--
-- Name: donations donations_charity_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.donations
    ADD CONSTRAINT donations_charity_id_fkey FOREIGN KEY (charity_id) REFERENCES public.charities(charities_id) ON DELETE CASCADE;


--
-- Name: entity_stock entity_stock_commodity_code_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.entity_stock
    ADD CONSTRAINT entity_stock_commodity_code_fkey FOREIGN KEY (commodity_code) REFERENCES public.commodities(code);


--
-- Name: expedition_backers expedition_backers_expedition_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.expedition_backers
    ADD CONSTRAINT expedition_backers_expedition_id_fkey FOREIGN KEY (expedition_id) REFERENCES public.expeditions(expeditions_id) ON DELETE CASCADE;


--
-- Name: expedition_returns expedition_returns_expedition_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.expedition_returns
    ADD CONSTRAINT expedition_returns_expedition_id_fkey FOREIGN KEY (expedition_id) REFERENCES public.expeditions(expeditions_id) ON DELETE CASCADE;


--
-- Name: expeditions expeditions_charter_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.expeditions
    ADD CONSTRAINT expeditions_charter_id_fkey FOREIGN KEY (charter_id) REFERENCES public.charters(charters_id) ON DELETE SET NULL;


--
-- Name: expeditions expeditions_leader_player_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.expeditions
    ADD CONSTRAINT expeditions_leader_player_id_fkey FOREIGN KEY (leader_player_id) REFERENCES public.players(player_id) ON DELETE CASCADE;


--
-- Name: futures_contracts futures_contracts_buyer_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.futures_contracts
    ADD CONSTRAINT futures_contracts_buyer_id_fkey FOREIGN KEY (buyer_id) REFERENCES public.players(player_id) ON DELETE CASCADE;


--
-- Name: futures_contracts futures_contracts_commodity_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.futures_contracts
    ADD CONSTRAINT futures_contracts_commodity_id_fkey FOREIGN KEY (commodity_id) REFERENCES public.commodities(commodities_id) ON DELETE CASCADE;


--
-- Name: futures_contracts futures_contracts_seller_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.futures_contracts
    ADD CONSTRAINT futures_contracts_seller_id_fkey FOREIGN KEY (seller_id) REFERENCES public.players(player_id) ON DELETE CASCADE;


--
-- Name: guild_dues guild_dues_guild_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.guild_dues
    ADD CONSTRAINT guild_dues_guild_id_fkey FOREIGN KEY (guild_id) REFERENCES public.guilds(guilds_id) ON DELETE CASCADE;


--
-- Name: guild_memberships guild_memberships_guild_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.guild_memberships
    ADD CONSTRAINT guild_memberships_guild_id_fkey FOREIGN KEY (guild_id) REFERENCES public.guilds(guilds_id) ON DELETE CASCADE;


--
-- Name: insurance_claims insurance_claims_policy_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.insurance_claims
    ADD CONSTRAINT insurance_claims_policy_id_fkey FOREIGN KEY (policy_id) REFERENCES public.insurance_policies(insurance_policies_id) ON DELETE CASCADE;


--
-- Name: insurance_policies insurance_policies_fund_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.insurance_policies
    ADD CONSTRAINT insurance_policies_fund_id_fkey FOREIGN KEY (fund_id) REFERENCES public.insurance_funds(fund_id) ON DELETE SET NULL;


--
-- Name: laundering_ops laundering_ops_from_black_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.laundering_ops
    ADD CONSTRAINT laundering_ops_from_black_id_fkey FOREIGN KEY (from_black_id) REFERENCES public.black_accounts(black_accounts_id) ON DELETE SET NULL;


--
-- Name: laundering_ops laundering_ops_to_player_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.laundering_ops
    ADD CONSTRAINT laundering_ops_to_player_id_fkey FOREIGN KEY (to_player_id) REFERENCES public.players(player_id) ON DELETE SET NULL;


--
-- Name: limpet_attached limpet_attached_owner_player_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.limpet_attached
    ADD CONSTRAINT limpet_attached_owner_player_id_fkey FOREIGN KEY (owner_player_id) REFERENCES public.players(player_id) ON DELETE CASCADE;


--
-- Name: limpet_attached limpet_attached_ship_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.limpet_attached
    ADD CONSTRAINT limpet_attached_ship_id_fkey FOREIGN KEY (ship_id) REFERENCES public.ships(ship_id) ON DELETE CASCADE;


--
-- Name: loan_payments loan_payments_loan_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.loan_payments
    ADD CONSTRAINT loan_payments_loan_id_fkey FOREIGN KEY (loan_id) REFERENCES public.loans(loans_id) ON DELETE CASCADE;


--
-- Name: mail mail_recipient_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.mail
    ADD CONSTRAINT mail_recipient_id_fkey FOREIGN KEY (recipient_id) REFERENCES public.players(player_id) ON DELETE CASCADE;


--
-- Name: mail mail_sender_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.mail
    ADD CONSTRAINT mail_sender_id_fkey FOREIGN KEY (sender_id) REFERENCES public.players(player_id) ON DELETE CASCADE;


--
-- Name: msl_sectors msl_sectors_sector_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.msl_sectors
    ADD CONSTRAINT msl_sectors_sector_id_fkey FOREIGN KEY (sector_id) REFERENCES public.sectors(sector_id);


--
-- Name: planet_goods planet_goods_planet_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.planet_goods
    ADD CONSTRAINT planet_goods_planet_id_fkey FOREIGN KEY (planet_id) REFERENCES public.planets(planet_id);


--
-- Name: planet_production planet_production_commodity_code_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.planet_production
    ADD CONSTRAINT planet_production_commodity_code_fkey FOREIGN KEY (commodity_code) REFERENCES public.commodities(code) ON DELETE CASCADE;


--
-- Name: planet_production planet_production_planet_type_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.planet_production
    ADD CONSTRAINT planet_production_planet_type_id_fkey FOREIGN KEY (planet_type_id) REFERENCES public.planettypes(planettypes_id) ON DELETE CASCADE;


--
-- Name: planets planets_sector_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.planets
    ADD CONSTRAINT planets_sector_id_fkey FOREIGN KEY (sector_id) REFERENCES public.sectors(sector_id);


--
-- Name: planets planets_type_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.planets
    ADD CONSTRAINT planets_type_fkey FOREIGN KEY (type) REFERENCES public.planettypes(planettypes_id);


--
-- Name: player_avoid player_avoid_player_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.player_avoid
    ADD CONSTRAINT player_avoid_player_id_fkey FOREIGN KEY (player_id) REFERENCES public.players(player_id) ON DELETE CASCADE;


--
-- Name: player_avoid player_avoid_sector_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.player_avoid
    ADD CONSTRAINT player_avoid_sector_id_fkey FOREIGN KEY (sector_id) REFERENCES public.sectors(sector_id) ON DELETE CASCADE;


--
-- Name: player_bookmarks player_bookmarks_player_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.player_bookmarks
    ADD CONSTRAINT player_bookmarks_player_id_fkey FOREIGN KEY (player_id) REFERENCES public.players(player_id) ON DELETE CASCADE;


--
-- Name: player_bookmarks player_bookmarks_sector_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.player_bookmarks
    ADD CONSTRAINT player_bookmarks_sector_id_fkey FOREIGN KEY (sector_id) REFERENCES public.sectors(sector_id) ON DELETE CASCADE;


--
-- Name: player_notes player_notes_player_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.player_notes
    ADD CONSTRAINT player_notes_player_id_fkey FOREIGN KEY (player_id) REFERENCES public.players(player_id) ON DELETE CASCADE;


--
-- Name: players players_commission_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.players
    ADD CONSTRAINT players_commission_id_fkey FOREIGN KEY (commission_id) REFERENCES public.commission(commission_id);


--
-- Name: players players_sector_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.players
    ADD CONSTRAINT players_sector_id_fkey FOREIGN KEY (sector_id) REFERENCES public.sectors(sector_id);


--
-- Name: players players_ship_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.players
    ADD CONSTRAINT players_ship_id_fkey FOREIGN KEY (ship_id) REFERENCES public.ships(ship_id);


--
-- Name: podded_status podded_status_player_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.podded_status
    ADD CONSTRAINT podded_status_player_id_fkey FOREIGN KEY (player_id) REFERENCES public.players(player_id);


--
-- Name: port_busts port_busts_player_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.port_busts
    ADD CONSTRAINT port_busts_player_id_fkey FOREIGN KEY (player_id) REFERENCES public.players(player_id);


--
-- Name: port_busts port_busts_port_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.port_busts
    ADD CONSTRAINT port_busts_port_id_fkey FOREIGN KEY (port_id) REFERENCES public.ports(port_id);


--
-- Name: port_trade port_trade_port_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.port_trade
    ADD CONSTRAINT port_trade_port_id_fkey FOREIGN KEY (port_id) REFERENCES public.ports(port_id);


--
-- Name: ports ports_economy_curve_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.ports
    ADD CONSTRAINT ports_economy_curve_id_fkey FOREIGN KEY (economy_curve_id) REFERENCES public.economy_curve(economy_curve_id);


--
-- Name: ports ports_sector_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.ports
    ADD CONSTRAINT ports_sector_id_fkey FOREIGN KEY (sector_id) REFERENCES public.sectors(sector_id);


--
-- Name: research_contributors research_contributors_project_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.research_contributors
    ADD CONSTRAINT research_contributors_project_id_fkey FOREIGN KEY (project_id) REFERENCES public.research_projects(research_projects_id) ON DELETE CASCADE;


--
-- Name: research_results research_results_project_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.research_results
    ADD CONSTRAINT research_results_project_id_fkey FOREIGN KEY (project_id) REFERENCES public.research_projects(research_projects_id) ON DELETE CASCADE;


--
-- Name: sector_assets sector_assets_owner_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.sector_assets
    ADD CONSTRAINT sector_assets_owner_id_fkey FOREIGN KEY (owner_id) REFERENCES public.players(player_id);


--
-- Name: sector_assets sector_assets_sector_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.sector_assets
    ADD CONSTRAINT sector_assets_sector_id_fkey FOREIGN KEY (sector_id) REFERENCES public.sectors(sector_id);


--
-- Name: sector_warps sector_warps_from_sector_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.sector_warps
    ADD CONSTRAINT sector_warps_from_sector_fkey FOREIGN KEY (from_sector) REFERENCES public.sectors(sector_id) ON DELETE CASCADE;


--
-- Name: sector_warps sector_warps_to_sector_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.sector_warps
    ADD CONSTRAINT sector_warps_to_sector_fkey FOREIGN KEY (to_sector) REFERENCES public.sectors(sector_id) ON DELETE CASCADE;


--
-- Name: ship_markers ship_markers_ship_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.ship_markers
    ADD CONSTRAINT ship_markers_ship_id_fkey FOREIGN KEY (ship_id) REFERENCES public.ships(ship_id);


--
-- Name: ship_ownership ship_ownership_player_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.ship_ownership
    ADD CONSTRAINT ship_ownership_player_id_fkey FOREIGN KEY (player_id) REFERENCES public.players(player_id);


--
-- Name: ship_ownership ship_ownership_ship_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.ship_ownership
    ADD CONSTRAINT ship_ownership_ship_id_fkey FOREIGN KEY (ship_id) REFERENCES public.ships(ship_id);


--
-- Name: ships ships_sector_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.ships
    ADD CONSTRAINT ships_sector_id_fkey FOREIGN KEY (sector_id) REFERENCES public.sectors(sector_id);


--
-- Name: ships ships_type_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.ships
    ADD CONSTRAINT ships_type_id_fkey FOREIGN KEY (type_id) REFERENCES public.shiptypes(shiptypes_id);


--
-- Name: shiptypes shiptypes_required_commission_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.shiptypes
    ADD CONSTRAINT shiptypes_required_commission_fkey FOREIGN KEY (required_commission) REFERENCES public.commission(commission_id);


--
-- Name: shipyard_inventory shipyard_inventory_port_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.shipyard_inventory
    ADD CONSTRAINT shipyard_inventory_port_id_fkey FOREIGN KEY (port_id) REFERENCES public.ports(port_id);


--
-- Name: shipyard_inventory shipyard_inventory_ship_type_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.shipyard_inventory
    ADD CONSTRAINT shipyard_inventory_ship_type_id_fkey FOREIGN KEY (ship_type_id) REFERENCES public.shiptypes(shiptypes_id);


--
-- Name: stardock_assets stardock_assets_owner_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.stardock_assets
    ADD CONSTRAINT stardock_assets_owner_id_fkey FOREIGN KEY (owner_id) REFERENCES public.players(player_id);


--
-- Name: stardock_assets stardock_assets_sector_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.stardock_assets
    ADD CONSTRAINT stardock_assets_sector_id_fkey FOREIGN KEY (sector_id) REFERENCES public.sectors(sector_id);


--
-- Name: stock_dividends stock_dividends_stock_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.stock_dividends
    ADD CONSTRAINT stock_dividends_stock_id_fkey FOREIGN KEY (stock_id) REFERENCES public.stocks(id) ON DELETE CASCADE;


--
-- Name: stock_index_members stock_index_members_index_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.stock_index_members
    ADD CONSTRAINT stock_index_members_index_id_fkey FOREIGN KEY (index_id) REFERENCES public.stock_indices(id) ON DELETE CASCADE;


--
-- Name: stock_index_members stock_index_members_stock_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.stock_index_members
    ADD CONSTRAINT stock_index_members_stock_id_fkey FOREIGN KEY (stock_id) REFERENCES public.stocks(id) ON DELETE CASCADE;


--
-- Name: stock_orders stock_orders_player_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.stock_orders
    ADD CONSTRAINT stock_orders_player_id_fkey FOREIGN KEY (player_id) REFERENCES public.players(player_id) ON DELETE CASCADE;


--
-- Name: stock_orders stock_orders_stock_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.stock_orders
    ADD CONSTRAINT stock_orders_stock_id_fkey FOREIGN KEY (stock_id) REFERENCES public.stocks(id) ON DELETE CASCADE;


--
-- Name: stock_trades stock_trades_buyer_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.stock_trades
    ADD CONSTRAINT stock_trades_buyer_id_fkey FOREIGN KEY (buyer_id) REFERENCES public.players(player_id) ON DELETE CASCADE;


--
-- Name: stock_trades stock_trades_seller_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.stock_trades
    ADD CONSTRAINT stock_trades_seller_id_fkey FOREIGN KEY (seller_id) REFERENCES public.players(player_id) ON DELETE CASCADE;


--
-- Name: stock_trades stock_trades_stock_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.stock_trades
    ADD CONSTRAINT stock_trades_stock_id_fkey FOREIGN KEY (stock_id) REFERENCES public.stocks(id) ON DELETE CASCADE;


--
-- Name: stocks stocks_corp_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.stocks
    ADD CONSTRAINT stocks_corp_id_fkey FOREIGN KEY (corp_id) REFERENCES public.corporations(corporation_id) ON DELETE CASCADE;


--
-- Name: subscriptions subscriptions_player_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.subscriptions
    ADD CONSTRAINT subscriptions_player_id_fkey FOREIGN KEY (player_id) REFERENCES public.players(player_id) ON DELETE CASCADE;


--
-- Name: subspace_cursors subspace_cursors_player_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.subspace_cursors
    ADD CONSTRAINT subspace_cursors_player_id_fkey FOREIGN KEY (player_id) REFERENCES public.players(player_id) ON DELETE CASCADE;


--
-- Name: subspace subspace_sender_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.subspace
    ADD CONSTRAINT subspace_sender_id_fkey FOREIGN KEY (sender_id) REFERENCES public.players(player_id) ON DELETE SET NULL;


--
-- Name: tavern_deadpool_bets tavern_deadpool_bets_bettor_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.tavern_deadpool_bets
    ADD CONSTRAINT tavern_deadpool_bets_bettor_id_fkey FOREIGN KEY (bettor_id) REFERENCES public.players(player_id);


--
-- Name: tavern_deadpool_bets tavern_deadpool_bets_target_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.tavern_deadpool_bets
    ADD CONSTRAINT tavern_deadpool_bets_target_id_fkey FOREIGN KEY (target_id) REFERENCES public.players(player_id);


--
-- Name: tavern_graffiti tavern_graffiti_player_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.tavern_graffiti
    ADD CONSTRAINT tavern_graffiti_player_id_fkey FOREIGN KEY (player_id) REFERENCES public.players(player_id);


--
-- Name: tavern_loans tavern_loans_player_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.tavern_loans
    ADD CONSTRAINT tavern_loans_player_id_fkey FOREIGN KEY (player_id) REFERENCES public.players(player_id);


--
-- Name: tavern_lottery_tickets tavern_lottery_tickets_player_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.tavern_lottery_tickets
    ADD CONSTRAINT tavern_lottery_tickets_player_id_fkey FOREIGN KEY (player_id) REFERENCES public.players(player_id);


--
-- Name: tavern_notices tavern_notices_author_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.tavern_notices
    ADD CONSTRAINT tavern_notices_author_id_fkey FOREIGN KEY (author_id) REFERENCES public.players(player_id);


--
-- Name: taverns taverns_name_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.taverns
    ADD CONSTRAINT taverns_name_id_fkey FOREIGN KEY (name_id) REFERENCES public.tavern_names(tavern_names_id);


--
-- Name: taverns taverns_sector_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.taverns
    ADD CONSTRAINT taverns_sector_id_fkey FOREIGN KEY (sector_id) REFERENCES public.sectors(sector_id);


--
-- Name: tax_ledgers tax_ledgers_policy_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.tax_ledgers
    ADD CONSTRAINT tax_ledgers_policy_id_fkey FOREIGN KEY (policy_id) REFERENCES public.tax_policies(tax_policies_id) ON DELETE CASCADE;


--
-- Name: trade_log trade_log_player_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.trade_log
    ADD CONSTRAINT trade_log_player_id_fkey FOREIGN KEY (player_id) REFERENCES public.players(player_id);


--
-- Name: trade_log trade_log_port_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.trade_log
    ADD CONSTRAINT trade_log_port_id_fkey FOREIGN KEY (port_id) REFERENCES public.ports(port_id);


--
-- Name: trade_log trade_log_sector_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.trade_log
    ADD CONSTRAINT trade_log_sector_id_fkey FOREIGN KEY (sector_id) REFERENCES public.sectors(sector_id);


--
-- Name: turns turns_player_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.turns
    ADD CONSTRAINT turns_player_id_fkey FOREIGN KEY (player_id) REFERENCES public.players(player_id) ON DELETE CASCADE;


--
-- Name: SCHEMA public; Type: ACL; Schema: -; Owner: postgres
--

REVOKE USAGE ON SCHEMA public FROM PUBLIC;


--
-- PostgreSQL database dump complete
--

\unrestrict i95pTXBtkPqwpHcJ6MAek6w1UTbEo2SfiJXuUYDoeQ5v26kmcN5YRobkFT0pgFl

