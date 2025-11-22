### Revised Implementation Plan for Corporation Features

**Phase 1: Core Data Access & Read Operations**

1.  **Integrate Corporation Info into `player.my_info` Response:**
    *   **Objective:** Modify the existing `player.my_info` command handler to include the player's corporation details (ID, name, and new `credit_rating` if applicable) if they are a member.
    *   **Details:** Locate the `player.my_info` handler. Add a SQL query (joining `players`, `corp_members`, and `corporations`) to retrieve and populate the `corporation` object in the JSON response, including the `credit_rating`.

2.  **Implement `corp.balance` Command:**
    *   **Objective:** Create a new command handler to return the corporate bank balance.
    *   **Details:** Verify player membership. Query `corp_accounts` using the corporation ID. Convert and format the integer balance (minor units) to a string-encoded decimal for the response.

3.  **Implement `corp.statement` Command:**
    *   **Objective:** Create a new command handler to return a paginated history of corporate transactions.
    *   **Details:** Verify player membership and viewing permissions. Query `corp_tx` for the corporation, applying pagination logic. Convert `amount` and `balance_after` to string-encoded decimals.

4.  **Implement `corp.status` Command (New):**
    *   **Objective:** Provide a comprehensive overview of the corporation's financial health and stock market status.
    *   **Details:** Query `corporations` table for `tax_arrears`, `credit_rating`, and `is_publicly_traded`. If publicly traded, also fetch data from `corp_shares` (total, free float, IPO price) and potentially calculate current market cap.

**Phase 2: Write Operations & Business Logic**

1.  **Implement `corp.deposit` Command:**
    *   **Objective:** Enable players to deposit credits from their personal funds into the corporate bank account.
    *   **Details:** Verify player membership. Retrieve and validate player's funds. Perform an atomic transaction: debit player's funds (from `bank_accounts` or `players.credits`), credit `corp_accounts`, and insert entries into `corp_tx` and `bank_transactions`.

2.  **Implement `corp.withdraw` Command:**
    *   **Objective:** Enable authorized players to withdraw credits from the corporate bank account to their personal funds.
    *   **Details:** Verify player's role (e.g., 'Leader', 'Officer') and corporate funds. Perform an atomic transaction: debit `corp_accounts`, credit player's funds, and insert entries into `corp_tx` and `bank_transactions`.

3.  **Implement `stock.ipo.register` Command (New - for stock exchange registration):**
    *   **Objective:** Allow a corporation's CEO to register the corporation on the stock exchange and define its initial share structure.
    *   **Details:**
        *   **Client Command:** `stock.ipo.register` with `total_shares` and `initial_price`.
        *   **Server Logic:**
            *   Verify the player is the CEO (`owner_id` in `corporations` table), the corporation is not already publicly traded, and its `credit_rating` is not 'Default'.
            *   Create an entry in the `stocks` table (which has `corp_id`, `ticker`, `total_shares`, `par_value`, `current_price`).
            *   Create an entry in the new `corp_shares` table with `corp_id`, `total_shares`, `free_float_shares` (initially `total_shares`), `ipo_price`.
            *   Update `corporations` table to set `is_publicly_traded = 1`.
            *   Log an `engine_event` for the new IPO (for news feed).
            *   Return a confirmation response.

4.  **Implement `stock.buy_shares` Command (Primary Market only):**
    *   **Objective:** Allow players to buy shares directly from a publicly traded corporation.
    *   **Details:**
        *   **Client Command:** `stock.buy_shares` with `corp_id`, `quantity`.
        *   **Server Logic:**
            *   Verify `corp_id` refers to a publicly traded corporation.
            *   Calculate total cost (`quantity * current_price`).
            *   Verify player funds and corporation's available `free_float_shares`.
            *   Perform an atomic transaction:
                *   Debit player's `bank_accounts`.
                *   Credit corporation's `corp_accounts` (investment money).
                *   Update `corp_shareholders`: increase `shares_held` for the player (create entry if new shareholder).
                *   Update `corp_shares`: decrease `free_float_shares`.
                *   Insert entries into `bank_transactions` (player debit) and `corp_tx` (credit to corp).
                *   Log an `engine_event` for the trade (for news feed).

5.  **Implement `corp.issue_dividend` Command (Revised for Shareholders):**
    *   **Objective:** Distribute a specified `dividend_per_share` from the corporate treasury to all shareholders.
    *   **Details:**
        *   **Client Command:** `corp.issue_dividend` with `dividend_per_share`.
        *   **Server Logic:**
            *   Verify the player's role (e.g., 'Leader', 'Officer').
            *   Verify the corporation is publicly traded.
            *   Calculate total payout (`dividend_per_share * corp_shares.total_shares`).
            *   Verify corporate funds.
            *   Perform an atomic transaction:
                *   Debit the corporation's `corp_accounts` for the total payout.
                *   Insert a `corp_tx` entry of `kind='dividend'`.
                *   For each entry in `corp_shareholders`, calculate individual payout (`shares_held * dividend_per_share`).
                *   Credit each shareholder's `bank_accounts`.
                *   For each shareholder, insert a `bank_transactions` entry.
                *   Record the dividend in the `stock_dividends` table.
                *   Log an `engine_event` for the dividend declaration.
            *   (Optional: Corp members might also receive a "profit share" from corp treasury directly, distinct from dividends, to be added later if desired).

6.  **Implement `stock.bond.issue` Command (New):**
    *   **Objective:** Allow corporations to issue bonds to raise capital.
    *   **Details:**
        *   **Client Command:** `stock.bond.issue` with `face_value`, `coupon_rate`, `maturity_ts`, `quantity`.
        *   **Server Logic:**
            *   Verify player's role and corporation's `credit_rating` (e.g., not 'Default').
            *   Create entries in `corp_bonds` table.
            *   Credit `corp_accounts` with the proceeds from bond sales.
            *   (Initially, investors buy from the corp directly, similar to `stock.buy_shares`).

**Phase 3: Automated Systems & System Integrations**

1.  **Automated Corporate Tax Collection (`h_daily_corp_tax` Cron Job):**
    *   **Objective:** Implement a daily cron job to calculate and collect a fixed 5% corporate tax.
    *   **Details:**
        *   Add `CORP_TAX_RATE_BP = 500` to the `config` table.
        *   Create or modify a cron task (`cron_tasks` table) to run daily.
        *   For each active corporation:
            *   Calculate the tax base: `corp_accounts.balance` + sum of `citadels.treasury` for corp-owned citadels.
            *   Calculate tax (`tax = tax_base * CORP_TAX_RATE_BP / 10000`).
            *   Attempt to debit `corp_accounts` for the tax.
            *   **If corp can pay:** Debit `corp_accounts`, insert `corp_tx` (kind='TAX'). Log `engine_event`.
            *   **If corp cannot pay:**
                *   Debit partially if possible.
                *   Increase `corporations.tax_arrears`.
                *   Downgrade `corporations.credit_rating` based on `tax_arrears` level (e.g., thresholds for 'Good' (0), 'Fair' (1), 'Poor' (2), 'Default' (3)).
                *   Log `engine_event` for tax default/rating change.
                *   If `credit_rating` becomes 'Default', potentially trigger a system bounty on CEO (using `bounties` table with `issuer_id` as a system entity).

2.  **Automated Bond Coupon Payments (`h_daily_bond_payout` Cron Job):**
    *   **Objective:** Implement a cron job to handle periodic coupon payments for corporate bonds.
    *   **Details:**
        *   Create a cron task to run daily (or as per coupon frequency).
        *   For each active bond with a due coupon payment:
            *   Calculate coupon payment for each bondholder.
            *   Debit `corp_accounts`, credit bondholder's `bank_accounts`.
            *   Handle insufficient funds (may lead to `credit_rating` downgrade, bond default state recorded in `corp_bonds`).

3.  **ITC Integration: Corporate Listings and Indices (Read-only views):**
    *   **Objective:** Display corporate financial and stock market data in the Interstellar Trade Commission (ITC).
    *   **Details:**
        *   Create SQL views (e.g., `itc_corp_rankings`) to aggregate data:
            *   Top corps by Market Cap (`corp_shares.total_shares * current_price`).
            *   Top corps by Dividend Yield.
            *   Top corps by Total Tax Paid to Federation (sum of `corp_tx` where `kind='TAX'`).
        *   Integrate these views into the ITC data fetching logic.
        *   Add `engine_events` for these updates to trigger news.

4.  **Corporate News Hooks:**
    *   **Objective:** Generate news events for significant corporate activities.
    *   **Details:** Ensure `engine_events` are created for:
        *   New IPO announcements (`stock.ipo.register`).
        *   Dividend declarations (`corp.issue_dividend`).
        *   Credit rating changes (`h_daily_corp_tax` cron).
        *   Tax defaults (`h_daily_corp_tax` cron).
        *   Corp going into/coming out of default.
        *   (Optional later: large insider trades).

5.  **Minor Unit Conversion Utility:**
    *   **Objective:** Create a consistent method for converting between internal integer minor units and external string-encoded decimal currency values.
    *   **Details:** Implement `minor_to_decimal_string` and `decimal_string_to_minor` functions and apply them to all financial command handlers.

6.  **Standardized Error Handling:**
    *   **Objective:** Define and consistently use specific error codes for corporation-related failures.

7.  **Comprehensive Logging:**
    *   **Objective:** Ensure all significant corporate actions are recorded in the `corp_log` table.

**Schema Impact Overview (New/Modified Tables/Columns):**

*   **`config`**: Add `CORP_TAX_RATE_BP INTEGER NOT NULL DEFAULT 500`.
*   **`corporations`**:
    *   Add `is_publicly_traded INTEGER NOT NULL DEFAULT 0`.
    *   Add `tax_arrears INTEGER NOT NULL DEFAULT 0`.
    *   Add `credit_rating INTEGER NOT NULL DEFAULT 0` (or TEXT enum).
*   **`corp_shares` (New Table)**: `corp_id INTEGER PRIMARY KEY REFERENCES corporations(id)`, `total_shares INTEGER`, `free_float_shares INTEGER`, `ipo_price INTEGER`.
*   **`corp_shareholders` (New Table)**: `player_id INTEGER REFERENCES players(id)`, `corp_id INTEGER REFERENCES corporations(id)`, `shares_held INTEGER`, `PRIMARY KEY (player_id, corp_id)`.
*   **`corp_bonds` (New Table)**: `corp_id INTEGER REFERENCES corporations(id)`, `bond_id INTEGER PRIMARY KEY`, `face_value INTEGER`, `coupon_rate INTEGER`, `maturity_ts INTEGER`, `issuer_id INTEGER REFERENCES players(id)`.

**Minimal but Deep Implementation Order:**

1.  **Corporate tax cron:** Implement `h_daily_corp_tax` cron with `CORP_TAX_RATE_BP` and tax base (corp bank + corp planet/citadel treasuries).
2.  **Basic corp shares + IPO + primary market buying:** Implement `stock.ipo.register` and `stock.buy_shares`. Define `corp_shares` and `corp_shareholders` tables.
3.  **Dividends:** Implement `corp.issue_dividend` for per-share payout from corp bank to shareholders.
4.  **Credit rating + tax arrears flag:** Implement `corporations.tax_arrears` and `corporations.credit_rating` with simple effects (block IPO if in default).
5.  **ITC corp listings:** Create read-only views for top corps by market cap and tax paid.