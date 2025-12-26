-- 090_other.sql
-- Converted from SQLite inline triggers to PostgreSQL Functions + Triggers

-- 1. Ship Shield Initialization
-- Optimization: Switched to BEFORE INSERT to modify NEW directly instead of running a second UPDATE
CREATE OR REPLACE FUNCTION fn_ships_ai_set_installed_shields() RETURNS TRIGGER AS $$
BEGIN
    NEW.installed_shields := LEAST(
        NEW.shields, 
        COALESCE(
            (SELECT maxshields FROM shiptypes st WHERE st.id = NEW.type_id), 
            NEW.shields
        )
    );
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER ships_ai_set_installed_shields
BEFORE INSERT ON ships
FOR EACH ROW
EXECUTE FUNCTION fn_ships_ai_set_installed_shields();


-- 2. Planet Cap Check
CREATE OR REPLACE FUNCTION fn_planets_total_cap_before_insert() RETURNS TRIGGER AS $$
DECLARE
    current_count BIGINT;
    max_allowed BIGINT;
BEGIN
    -- Allow System (0) to bypass cap during generation
    IF NEW.created_by = 0 THEN
        RETURN NEW;
    END IF;

    SELECT count(*) INTO current_count FROM planets;
    SELECT CAST(value AS BIGINT) INTO max_allowed FROM config WHERE key = 'max_total_planets';
    
    IF current_count >= max_allowed THEN
        RAISE EXCEPTION 'ERR_UNIVERSE_FULL';
    END IF;
    
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER trg_planets_total_cap_before_insert
BEFORE INSERT ON planets
FOR EACH ROW
EXECUTE FUNCTION fn_planets_total_cap_before_insert();


-- 3. Corporation Updated Timestamp
CREATE OR REPLACE FUNCTION fn_corporations_touch_updated() RETURNS TRIGGER AS $$
BEGIN
    NEW.updated_at := CURRENT_TIMESTAMP;
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER corporations_touch_updated
BEFORE UPDATE ON corporations
FOR EACH ROW
EXECUTE FUNCTION fn_corporations_touch_updated();


-- 4. Corp Owner Must Be Member (Auto-Insert)
CREATE OR REPLACE FUNCTION fn_corp_owner_must_be_member_insert() RETURNS TRIGGER AS $$
BEGIN
    IF NEW.owner_id IS NOT NULL AND NOT EXISTS (SELECT 1 FROM corp_members WHERE corp_id=NEW.id AND player_id=NEW.owner_id) THEN
        INSERT INTO corp_members(corp_id, player_id, role) VALUES(NEW.id, NEW.owner_id, 'Leader');
    END IF;
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER corp_owner_must_be_member_insert
AFTER INSERT ON corporations
FOR EACH ROW
EXECUTE FUNCTION fn_corp_owner_must_be_member_insert();


-- 5. Corp One Leader Guard
CREATE OR REPLACE FUNCTION fn_corp_one_leader_guard() RETURNS TRIGGER AS $$
BEGIN
    IF NEW.role = 'Leader' AND EXISTS (SELECT 1 FROM corp_members WHERE corp_id=NEW.corp_id AND role='Leader') THEN
        RAISE EXCEPTION 'corp may have only one Leader';
    END IF;
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER corp_one_leader_guard
BEFORE INSERT ON corp_members
FOR EACH ROW
EXECUTE FUNCTION fn_corp_one_leader_guard();


-- 6. Corp Owner/Leader Sync
CREATE OR REPLACE FUNCTION fn_corp_owner_leader_sync() RETURNS TRIGGER AS $$
BEGIN
    -- Downgrade old leader
    UPDATE corp_members SET role='Officer' 
    WHERE corp_id=NEW.id AND role='Leader' AND player_id <> NEW.owner_id;

    -- Ensure new owner is leader
    INSERT INTO corp_members(corp_id, player_id, role) 
    VALUES(NEW.id, NEW.owner_id, 'Leader') 
    ON CONFLICT(corp_id, player_id) DO UPDATE SET role='Leader';
    
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER corp_owner_leader_sync
AFTER UPDATE OF owner_id ON corporations
FOR EACH ROW
EXECUTE FUNCTION fn_corp_owner_leader_sync();


-- 7. Bank Transactions: Insufficient Funds Check
CREATE OR REPLACE FUNCTION fn_bank_transactions_before_insert() RETURNS TRIGGER AS $$
DECLARE
    current_bal BIGINT;
BEGIN
    IF NEW.direction = 'DEBIT' THEN
        SELECT balance INTO current_bal FROM bank_accounts WHERE id = NEW.account_id;
        IF (current_bal - NEW.amount) < 0 THEN
            RAISE EXCEPTION 'BANK_INSUFFICIENT_FUNDS';
        END IF;
    END IF;
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER trg_bank_transactions_before_insert
BEFORE INSERT ON bank_transactions
FOR EACH ROW
EXECUTE FUNCTION fn_bank_transactions_before_insert();


-- 8. Bank Transactions: Update Balance
CREATE OR REPLACE FUNCTION fn_bank_transactions_after_insert() RETURNS TRIGGER AS $$
BEGIN
    UPDATE bank_accounts 
    SET balance = CASE NEW.direction 
        WHEN 'DEBIT' THEN balance - NEW.amount 
        WHEN 'CREDIT' THEN balance + NEW.amount 
        ELSE balance 
    END 
    WHERE id = NEW.account_id;

    -- Update the transaction record with the resulting balance (requires another update in PG unless logic is moved to app layer, keeping as is to match legacy logic)
    UPDATE bank_transactions 
    SET balance_after = (SELECT balance FROM bank_accounts WHERE id = NEW.account_id) 
    WHERE id = NEW.id;
    
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER trg_bank_transactions_after_insert
AFTER INSERT ON bank_transactions
FOR EACH ROW
EXECUTE FUNCTION fn_bank_transactions_after_insert();


-- 9. Bank Transactions: Prevent Deletion
CREATE OR REPLACE FUNCTION fn_bank_transactions_before_delete() RETURNS TRIGGER AS $$
BEGIN
    RAISE EXCEPTION 'BANK_LEDGER_APPEND_ONLY';
    RETURN OLD;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER trg_bank_transactions_before_delete
BEFORE DELETE ON bank_transactions
FOR EACH ROW
EXECUTE FUNCTION fn_bank_transactions_before_delete();


-- 10. Corp Transactions: Before Insert (Create account, check funds)
CREATE OR REPLACE FUNCTION fn_corp_tx_before_insert() RETURNS TRIGGER AS $$
DECLARE
    current_bal BIGINT;
BEGIN
    -- Ensure account exists
    INSERT INTO corp_accounts(corp_id, currency, balance, last_interest_at) 
    VALUES (NEW.corp_id, COALESCE(NEW.currency,'CRD'), 0, NULL)
    ON CONFLICT DO NOTHING;

    -- Check funds for outgoing types
    IF NEW.kind IN ('withdraw','transfer_out','dividend','salary') THEN
        SELECT balance INTO current_bal FROM corp_accounts WHERE corp_id = NEW.corp_id;
        IF (current_bal - NEW.amount) < 0 THEN
            RAISE EXCEPTION 'CORP_INSUFFICIENT_FUNDS';
        END IF;
    END IF;
    
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER trg_corp_tx_before_insert
BEFORE INSERT ON corp_tx
FOR EACH ROW
EXECUTE FUNCTION fn_corp_tx_before_insert();


-- 11. Corp Transactions: After Insert (Update balance)
CREATE OR REPLACE FUNCTION fn_corp_tx_after_insert() RETURNS TRIGGER AS $$
BEGIN
    UPDATE corp_accounts 
    SET balance = CASE 
        WHEN NEW.kind IN ('withdraw', 'transfer_out', 'dividend', 'salary') THEN balance - NEW.amount 
        ELSE balance + NEW.amount 
    END 
    WHERE corp_id = NEW.corp_id;

    UPDATE corp_tx 
    SET balance_after = (SELECT balance FROM corp_accounts WHERE corp_id = NEW.corp_id) 
    WHERE id = NEW.id;
    
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER trg_corp_tx_after_insert
AFTER INSERT ON corp_tx
FOR EACH ROW
EXECUTE FUNCTION fn_corp_tx_after_insert();
-- 11. Ship Destruction

CREATE OR REPLACE FUNCTION public.handle_ship_destruction(
    p_victim_player_id      bigint,
    p_victim_ship_id        bigint,
    p_killer_player_id      bigint,
    p_cause                 text,      -- 'combat'|'mines'|'quasar'|'navhaz'|'self_destruct'|'other'
    p_sector_id             bigint,

    -- config-driven inputs from the server (or you can fetch from config inside DB too)
    p_xp_loss_flat          bigint,
    p_xp_loss_percent       numeric,   -- e.g. 5.0
    p_max_pods_per_day      bigint,

    -- Big sleep duration; set 0 if you just want status without timer
    p_big_sleep_seconds     bigint DEFAULT 0,

    -- allow deterministic tests
    p_now_ts                bigint DEFAULT (extract(epoch from now())::bigint),

    -- ship_ownership role id for "owner" (set to your canonical value)
    p_owner_role_id         bigint DEFAULT 1
)
RETURNS TABLE (
    result_code       integer,
    decision          text,      -- 'podded' | 'big_sleep'
    new_experience    bigint,
    xp_lost           bigint,
    escape_pod_ship_id bigint,   -- NULL unless podded and spawned
    engine_event_id   bigint
)
LANGUAGE plpgsql
AS $$
DECLARE
    v_current_xp            bigint;
    v_new_xp                bigint;
    v_xp_loss               bigint;

    v_shiptype_id           bigint;
    v_has_escape_pod         boolean;

    v_podded_count_today    bigint;
    v_podded_last_reset     bigint;

    v_escape_pod_shiptype_id bigint;
    v_cfg_val               text;

    v_payload               text;
BEGIN
    /*
      Concurrency / correctness:
      - lock victim player row (FOR UPDATE)
      - lock victim ship row (FOR UPDATE)
      - lock podded_status row (FOR UPDATE)
    */

    -- 0) Lock and read player XP
    SELECT experience
      INTO v_current_xp
      FROM public.players
     WHERE id = p_victim_player_id
     FOR UPDATE;

    IF NOT FOUND THEN
        RAISE EXCEPTION 'handle_ship_destruction: victim player % not found', p_victim_player_id
            USING ERRCODE = 'NO_DATA_FOUND';
    END IF;

    -- 1) Lock ship, read its type_id (for eligibility), then mark destroyed
    SELECT type_id
      INTO v_shiptype_id
      FROM public.ships
     WHERE id = p_victim_ship_id
     FOR UPDATE;

    IF NOT FOUND THEN
        RAISE EXCEPTION 'handle_ship_destruction: victim ship % not found', p_victim_ship_id
            USING ERRCODE = 'NO_DATA_FOUND';
    END IF;

    -- eligibility from shiptypes.has_escape_pod (NULL shiptype => treat as false)
    SELECT COALESCE(st.has_escape_pod, FALSE)
      INTO v_has_escape_pod
      FROM public.shiptypes st
     WHERE st.id = v_shiptype_id;

    -- mark destroyed
    UPDATE public.ships
       SET destroyed = 1
     WHERE id = p_victim_ship_id;

    -- detach ship from player active ship slot (only if currently set to this ship)
    UPDATE public.players
       SET ship = NULL
     WHERE id = p_victim_player_id
       AND ship = p_victim_ship_id;

    -- 2) increment times_blown_up (now a real column)
    UPDATE public.players
       SET times_blown_up = COALESCE(times_blown_up, 0) + 1
     WHERE id = p_victim_player_id;

    -- 3) apply XP penalty
    v_xp_loss :=
        p_xp_loss_flat
        + floor(v_current_xp * (p_xp_loss_percent / 100.0))::bigint;

    v_new_xp := v_current_xp - v_xp_loss;
    IF v_new_xp < 0 THEN
        v_new_xp := 0;
    END IF;

    UPDATE public.players
       SET experience = v_new_xp
     WHERE id = p_victim_player_id;

    -- 4) podded_status row ensure + lock
    INSERT INTO public.podded_status (player_id, status, podded_count_today, podded_last_reset)
    VALUES (p_victim_player_id, 'active', 0, p_now_ts)
    ON CONFLICT (player_id) DO NOTHING;

    SELECT podded_count_today, podded_last_reset
      INTO v_podded_count_today, v_podded_last_reset
      FROM public.podded_status
     WHERE player_id = p_victim_player_id
     FOR UPDATE;

    -- reset daily pod counter if older than 24h or NULL
    IF v_podded_last_reset IS NULL OR (p_now_ts - v_podded_last_reset) >= 86400 THEN
        v_podded_count_today := 0;
        v_podded_last_reset := p_now_ts;

        UPDATE public.podded_status
           SET podded_count_today = 0,
               podded_last_reset  = p_now_ts
         WHERE player_id = p_victim_player_id;
    END IF;

    -- 5) decide pod vs big sleep
    escape_pod_ship_id := NULL;

    IF v_has_escape_pod AND v_podded_count_today < p_max_pods_per_day THEN
        decision := 'podded';

        -- load escape pod shiptype id from config
        SELECT value
          INTO v_cfg_val
          FROM public.config
         WHERE key = 'death.escape_pod_shiptype_id'
           AND type = 'int';

        IF v_cfg_val IS NULL THEN
            -- hard fail: you asked for pod spawn, but config missing => safer to big sleep
            decision := 'big_sleep';
        ELSE
            v_escape_pod_shiptype_id := v_cfg_val::bigint;

            -- spawn escape pod ship (keep this INSERT minimal; relies on defaults / nullable cols)
	    INSERT INTO public.ships (name, type_id, sector, destroyed)
	    VALUES ('Escape Pod', v_escape_pod_shiptype_id, 1, 0)
	    RETURNING id INTO escape_pod_ship_id;

            -- assign as active ship
	    UPDATE public.players
	       SET ship = escape_pod_ship_id
	        WHERE id = p_victim_player_id;

            -- ownership (role_id is your canonical "owner" role)
            INSERT INTO public.ship_ownership (ship_id, player_id, role_id, is_primary)
            VALUES (escape_pod_ship_id, p_victim_player_id, p_owner_role_id, TRUE)
            ON CONFLICT (ship_id, player_id, role_id) DO UPDATE
              SET is_primary = EXCLUDED.is_primary;

            -- update podded status + increment daily pod count
            UPDATE public.podded_status
               SET status             = 'podded',
                   big_sleep_until    = NULL,
                   reason             = NULL,
                   podded_count_today = v_podded_count_today + 1,
                   podded_last_reset  = v_podded_last_reset
             WHERE player_id = p_victim_player_id;
        END IF;
    END IF;

    IF decision IS DISTINCT FROM 'podded' THEN
        decision := 'big_sleep';

        UPDATE public.podded_status
           SET status          = 'big_sleep',
               big_sleep_until = CASE
                                   WHEN p_big_sleep_seconds > 0 THEN p_now_ts + p_big_sleep_seconds
                                   ELSE NULL
                                 END,
               reason          = COALESCE(p_cause, 'other')
         WHERE player_id = p_victim_player_id;
    END IF;

    -- 6) log engine event (ship.destroyed)
    v_payload := json_build_object(
    'victim_ship_id',     p_victim_ship_id,
    'victim_player_id',   p_victim_player_id,
    'killer_player_id',   p_killer_player_id,
    'cause',              COALESCE(p_cause, 'other'),
    'sector_id',          p_sector_id,
    'decision',           decision,
    'pod_sector_id',      CASE WHEN decision = 'podded' THEN 1 ELSE NULL END,
    'escape_pod_ship_id', escape_pod_ship_id,
    'xp_lost',            v_xp_loss,
    'new_xp',             v_new_xp
    )::text;

    INSERT INTO public.engine_events (ts, type, actor_player_id, sector_id, payload, idem_key)
    VALUES (p_now_ts, 'ship.destroyed', NULL, p_sector_id, v_payload, NULL)
    RETURNING id INTO engine_event_id;

    -- outputs
    result_code    := 0;
    new_experience := v_new_xp;
    xp_lost        := v_xp_loss;
    RETURN NEXT;
END;
$$;
