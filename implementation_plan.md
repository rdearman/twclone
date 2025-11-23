### Unified Implementation Plan: Corporation System & Economy

#### **Phase 1: Core Corporate Entity & Membership (Brief #2)**
*   **Goal:** Establish the fundamental ability for players to form, join, leave, and manage corporations.
*   **Schema (Use Existing):**
    *   `corporations`: Will be used to store the core corp entity. The `owner_id` column will represent the CEO.
    *   `corp_members`: Will be used to manage membership. The `role` column ('Leader', 'Officer', 'Member') will be used for the hierarchy, with 'Leader' being the CEO.
*   **Schema (To Be Created/Modified):**
    *   `corp_invites`: This table needs to be created to manage direct invitations.
*   **Files to be Modified:**
    *   `src/database.c`: Add `CREATE TABLE corp_invites` to the schema array.
    *   `src/server_corporation.h`: New file for all corporation-related function declarations.
    *   `src/server_corporation.c`: New file to implement the logic for all `corp.*` commands.
    *   `src/server_loop.c`: Add includes and routing for the new `corp.*` commands.
*   **RPCs / Commands (Logic to be Implemented):**
    *   `corp.create`: Implement the command for a player to found a new corporation. This will create the entry in `corporations` and add the founder to `corp_members` as the CEO. A bank account will also be created for the corporation.
    *   `corp.dissolve`: Implement the CEO-only command to dissolve a corporation, handling asset transfer and cascading deletes.
    *   `corp.invite`: Allow CEO/Officers to invite players, creating a timed entry in `corp_invites`.
    *   `corp.join`: Allow players to join via invite.
    *   `corp.leave`: Allow members to leave, with special handling for Leaders.
    *   `corp.kick`: Allow CEO/Officers to kick members based on hierarchy.
    *   `corp.list`: Provide a public list of corporations.
    *   `corp.roster`: Provide a list of members for a specific corporation.

#### **Phase 2: Asset Integration & CEO Powers (Brief #2)**
*   **Goal:** Implement the core gameplay loops for shared assets, trust, and CEO-specific abilities.
*   **Files to be Modified:**
    *   `src/server_planets.c`: Modify access logic for planet-related actions.
    *   `src/server_citadel.c`: Modify access logic for citadel-related actions.
    *   `src/server_combat.c`: Adjust targeting AI for fighters and mines.
    *   `src/database.c`: Add the new Flagship ship type to `shiptypes` table.
    *   `src/server_corporation.c`: Implement `corp.transfer_ceo` and logic for asset conversion.
    *   `src/server_stardock.c`: Add purchase restriction logic for the Flagship.
*   **Logic Modifications (To Be Implemented):**
    *   **Planet/Citadel Access:** Modify existing planet landing and citadel banking logic to grant access to members of the owning corporation.
    *   **Defense Systems:** Adjust fighter/mine targeting AI to be friendly to corp members.
    *   **Asset Conversion:** On `corp.create`, implement the server-side setting to automatically convert a player's personal planets to corporate ownership.
*   **CEO Features:**
    *   **Corporate Flagship:** Add the Flagship hull as a new ship type. Restrict its purchase to active CEOs.
    *   **Leadership Transfer:** Implement a `corp.transfer_ceo` command that includes the critical check to prevent transfer if the current CEO is flying the Flagship.

#### **Phase 3: Corporate Finance, Taxation & Credit (Brief #1)**
*   **Goal:** Introduce the corporate tax system and the concept of creditworthiness.
*   **Files to be Modified:**
    *   `src/database.c`: Add `tax_arrears` and `credit_rating` columns to `corporations` table.
    *   `src/server_cron.c`: Add the new `h_daily_corp_tax` cron job.
*   **Schema (Use Existing):**
    *   `bank_accounts`: Will be used for all corporate finances (a bank account will be created for each corp).
*   **Schema (Modifications Needed):**
    *   The `corporations` table needs `tax_arrears` (INTEGER) and `credit_rating` (INTEGER/ENUM) columns.
*   **Configuration:**
    *   Define `CORP_TAX_RATE_BP` (e.g., 500 for 5.00%) as a server constant.
*   **Cron Job (`h_daily_corp_tax`) (Logic to be Implemented):**
    *   This new daily job will calculate the 5% tax based on the corporation's assets (bank balance, planet treasuries).
    *   It will debit the corporation's bank account.
    *   On failure to pay, it updates `tax_arrears` and lowers the `credit_rating`.

#### **Phase 4: Stock Market Implementation (Brief #1)**
*   **Goal:** Build the framework for corporations to go public and for players to invest.
*   **Files to be Modified:**
    *   `src/database.c`: Create the `corp_shareholders` table.
    *   `src/server_corporation.c`: Implement the new `stock.*` commands (`ipo.register`, `buy`).
    *   `src/server_cron.c`: Add a new cron job for recalculating stock prices.
    *   `src/server_loop.c`: Add routing for new `stock.*` commands.
*   **Schema (Use Existing):**
    *   `stocks`: This table exists and will be used for corporate stock listings.
*   **Schema (To Be Created):**
    *   `corp_shareholders`: This table is needed to efficiently track total shares held by each player for dividend and governance purposes.
*   **RPCs / Commands (Logic to be Implemented):**
    *   `stock.ipo.register`: Allows a CEO to take their company public. This command will fail if the corp's `credit_rating` from Phase 3 is in "Default".
    *   `stock.buy`: Allows players to buy shares directly from the corporation.
*   **Cron Job:** A new daily job to recalculate `current_price` based on corporate Net Asset Value (NAV).

#### **Phase 5: Dividends & Public Information (Brief #1)**
*   **Goal:** Complete the investment loop with dividends and integrate all new economic data into the game's UI and news systems.
*   **Files to be Modified:**
    *   `src/server_corporation.c`: Implement the `stock.dividend.set` command.
    *   `src/server_cron.c`: Add the `h_dividend_payout` cron job.
    *   `src/server_news.c`: Add logic to publish economic events.
    *   `client/client.py`: (Or other client files) to create new UI screens for stock data.
*   **Schema (Use Existing):**
    *   `stock_dividends`: This table exists and will be used to manage dividend declarations.
    *   `news_feed`: This will be used for publishing economic events.
*   **RPCs & Cron (Logic to be Implemented):**
    *   Implement a `stock.dividend.set` command for CEOs.
    *   Create a `h_dividend_payout` cron job to transfer dividend payments from the corporation's bank account to shareholders.
*   **ITC & News Integration:**
    *   Create new ITC screens to display stock market data (e.g., top corps by market cap, dividend yield).
    *   Hook into the existing news engine to announce IPOs, credit defaults, and dividend declarations.