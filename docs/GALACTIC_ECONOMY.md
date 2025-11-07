# **Galactic Economy: Design & Implementation Blueprint**

## **1\. Core Principle: "All Value is a Ledger Event"**

This document outlines a holistic economic model for the game, built on a single, powerful foundation: a **secure, append-only transaction ledger**.  
The legacy model of storing a player's money as a single, mutable number in a database column (players.bank\_balance) is fragile, unauditable, and difficult to extend.  
The new architecture replaces this with an auditable "money rail" consisting of two main tables:

1. bank\_accounts: A simple, cached table of current balances.  
2. bank\_tx: An **append-only ledger** that records *every* change in value.

Database triggers ensure these two tables are always in sync. The balance is just a cache; the bank\_tx ledger is the **single source of truth**.  
This design provides a safe, auditable, and extensible foundation for every feature described below. All value transfer—from a 10c tax to a 10-billion-credit corporate merger—is reduced to one or more rows in a transaction ledger.

## **2\. Core Finance Layer (Player & Corp)**

This layer provides the foundational banking features for individual players and corporations.

### **Player Banking**

* **Core Loop (Issues \#370, \#371, \#372):** Players can **deposit**, **withdraw**, and check their **balance** at Stardock and other secure banking locations.  
* **Player-to-Player Transfers (Issue \#372):** Players can securely transfer credits to any other player, even if offline.  
* **Bank Statements (Issue \#373):** Players can view a full, auditable history of all transactions (bank\_statement view).  
* **Automated Interest (Issue \#374):** A daily cron job pays out interest based on the bank\_interest\_policy table, creating a simple savings loop.  
* **Standing Orders:** The bank\_orders table allows players to set up recurring payments (e.g., for loans, corp dues, or taxes).  
* **Fees & Alerts (Issues \#375, \#376):** The system supports configurable transfer fees and can issue system\_notice alerts for large transactions.  
* **Leaderboard (Issue \#377):** A "rich list" (v\_bank\_leaderboard) can be shown, with privacy/opt-out flags.

### **Corporation Banking (Issue \#364)**

* **Corporate Treasury:** A parallel set of tables (corp\_accounts, corp\_tx) provides a shared bank account for corporations.  
* **Automated Finances:** The system supports:  
  * **Automated Taxes:** A corp leader can set a "5% interest tax" or a flat weekly fee, which is automatically collected from members via bank.transfer.  
  * **Dividends & Salaries:** The leader can issue mass payouts to members, which are processed as corp\_tx entries with kind='dividend'.

### **Multi-Server & Forex**

* **Currencies:** The currencies table and a currency column on all ledgers provide the foundation for a future multi-server economy, enabling **Foreign Exchange (Forex)** trading between different server "currencies" (e.g., 'Federation Credits' vs. 'Orion Dollars').

## **3\. Government, Law & Crime**

This system models the financial side of law enforcement, government services, and the criminal underworld.

### **Federation & Government**

* **Automated Fines:** Federation police can issue fines (fines table) that are automatically debited from a player's bank account.  
* **Federation Bounties:** Bounties paid by the Federation for killing NPCs are automatically paid out as a bank\_tx deposit.  
* **Taxation:** The tax\_policies and tax\_ledgers tables allow the game to automatically assess and collect taxes on trade, income, or property.  
* **Asset Freezes:** The bank\_flags table (is\_frozen flag) allows law enforcement to freeze a player's assets during an investigation.

### **Player-Driven Law & Chaos**

* **Player-Posted Bounties:** A player can post a bounty on another player. This is a key escrow flow:  
  1. The poster's funds are moved via transfer\_out into a system "Bounty Escrow" account (tracked in the bounties table).  
  2. When an engine\_event confirms the kill, the system executes a transfer\_in from escrow to the hunter.  
* **Automated Tolls (Issue \#357):** This is a critical trigger-based interaction.  
  1. Player enters a sector with a fighter toll.  
  2. The game attempts a bank.transfer from the player to the fighter owner.  
  3. **Crucially:** The trg\_bank\_tx\_before\_insert trigger will RAISE(ABORT, 'BANK\_INSUFFICIENT\_FUNDS') if the player can't pay.  
  4. The game engine catches this *specific database error* as the signal to initiate combat. If the transfer succeeds, the player passes.

### **Orion Black Market & Underworld**

* **Parallel Economy:** The Orion faction (and criminal players) can use their own set of "dark" tables: black\_accounts, laundering\_ops, and contracts\_illicit.  
* **Illicit Bounties:** Orion NPCs can use the bounties table to post rewards on "Good" players.  
* **Illicit Sales:** The sale of illegal goods (like slaves) is handled via auditable bank transfers, creating a "paper trail" for Fed police to follow.  
* **Money Laundering:** Players can attempt to move funds from black\_accounts to the legal bank\_accounts, with a risk of seizure.

## **4\. Trade & Commodity Exchange**

This system evolves ports from simple static shops into active participants in a dynamic, player-driven economy.

### **AI-Driven Port Economy**

* **From Static to Dynamic:** Instead of magic cron jobs (Issue \#61), ports become AI traders.  
* **Port Buy Orders:** A port that is low on "Ore" will create a **buy order** on the central commodity\_orders table.  
* **Port Sell Orders:** A port that has a surplus of "Equipment" will create a **sell order**.

### **Emergent Player Roles**

This AI-driven market creates three distinct and viable player roles:

1. **The Hauler (Arbitrage):** A player who physically buys from Port A's sell order, flies across the galaxy, and sells to Port B's buy order, profiting from the price difference.  
2. **The Speculator (Trader):** A player who never touches cargo. They buy 1,000,000 units of "Ore Futures" on the futures\_contracts table, wait for the price to rise, and sell the contract for a profit.  
3. **The Industrialist (Producer):** A player with a planet (planet\_goods) who creates a long-term **sell order** for their daily output, ensuring a stable, automated income stream.

### **Automated Player Shops**

* **Player "Vending Machines":** A player can configure their citadel to sell goods from its planet\_goods inventory. A visiting player simply initiates a bank.transfer to the owner; the game engine sees the successful bank\_tx receipt and automatically gives the items to the buyer.

## **5\. Financial Instruments & Services**

This system introduces complex financial products that create mid- and late-game strategic loops.

### **Loans & Financing (Issue \#295)**

* **Ship Mortgages:** A player can buy a 50M-credit ship with 10M down. The bank issues a 40M deposit (tracked in the loans table).  
* **Automated Repayment:** A bank\_order (Standing Order) is created to auto-pay the weekly installments.  
* **Repossession:** If a payment transfer\_out fails with BANK\_INSUFFICIENT\_FUNDS, the game engine is alerted and can begin the repossession process.  
* **Collateral & Credit:** The collateral table links loans to specific ships/planets. The credit\_ratings table tracks player reliability, affecting future loan and insurance rates.

### **Insurance (Issue \#276)**

* **Premiums & Payouts:** Players pay a periodic premium (an automated withdraw). Upon ship destruction (an engine\_event), the insurance\_policies table is checked, and a deposit for the payout is automatically issued.

## **6\. Strategic Investment (Credit Sinks)**

These systems are designed to give veteran players and corporations large-scale goals and to serve as critical "credit sinks" to combat late-game inflation.

### **Research & Development (R\&D)**

* **Primary Credit Sink:** All major planet and citadel upgrades are funneled through the research\_projects table.  
* **Funding:** A corp must fund a 500M-credit project (e.g., "Citadel Shields Lvl 2"). This is a single, massive corp\_tx withdraw that permanently removes the money from the economy. The project is then marked "funded."

### **Stock Exchange**

* **Corporate Fundraising:** Corporations can issue shares (stocks table) to raise capital.  
* **Player Investment:** Players can buy/sell shares (stock\_orders), creating a speculative market. This also enables **hostile takeovers** by acquiring a majority of a rival's stock.

### **Expeditionary Financing**

* **Exploration Syndicates:** Players can form syndicates to fund high-risk, high-reward exploration missions (expeditions table).  
* **Backers & Dividends:** Backers (expedition\_backers) pledge funds. Any profits from the expedition are automatically distributed back to them via bank\_tx deposits.

## **7\. System Reliability & Other Features**

* **Reliable Purchases:** All Shipyard/Shop purchases draw from the bank, preventing "double spend" bugs or silent credit loss on a failed purchase.  
* **Citadel-to-Bank Transfers:** Players can securely transfer credits from their citadels.treasury directly into their bank\_accounts.  
* **Tavern & Casino (Issue \#314):** All gambling (lottery, roulette) is handled via ledger transactions, providing a perfect audit trail and preventing "cash on hand" exploits.  
* **Guilds & Charities:** The guilds table allows for automated dues collection, and the charities table allows for auditable donations.

## **8\. Complete Database Schema**

This is the master SQL blueprint that implements all 12 economic systems described above.  
\-- \=============================================================  
\-- ECONOMY UNIVERSE SCHEMA (SQLite)  
\-- \=============================================================  
\-- NOTE: Enable FK in your application: PRAGMA foreign\_keys=ON;  
\-- Timestamps: TEXT as ISO8601 Z (strftime('%Y-%m-%dT%H:%M:%fZ','now'))  
\-- Currency: INTEGER smallest units (no floats)  
\-- No ALTER TABLE statements.  
\-- \=============================================================

\-- 0\. Shared enumerations  
CREATE TABLE IF NOT EXISTS currencies (  
  code TEXT PRIMARY KEY,  
  name TEXT NOT NULL,  
  minor\_unit INTEGER NOT NULL DEFAULT 1 CHECK (minor\_unit \> 0),  
  is\_default INTEGER NOT NULL DEFAULT 1 CHECK (is\_default IN (0,1))  
);  
INSERT OR IGNORE INTO currencies(code, name, minor\_unit, is\_default)  
VALUES ('CRD','Galactic Credits',1,1);

\-- I. BANKING (PLAYER)  
CREATE TABLE IF NOT EXISTS bank\_accounts (  
  player\_id INTEGER PRIMARY KEY REFERENCES players(id) ON DELETE CASCADE,  
  currency TEXT NOT NULL DEFAULT 'CRD' REFERENCES currencies(code),  
  balance INTEGER NOT NULL DEFAULT 0 CHECK (balance \>= 0),  
  last\_interest\_at TEXT  
);

CREATE TABLE IF NOT EXISTS bank\_tx (  
  id INTEGER PRIMARY KEY,  
  player\_id INTEGER NOT NULL REFERENCES players(id) ON DELETE CASCADE,  
  ts TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')),  
  kind TEXT NOT NULL CHECK (kind IN ('deposit','withdraw','transfer\_in','transfer\_out','interest','adjustment')),  
  amount INTEGER NOT NULL CHECK (amount \> 0),  
  balance\_after INTEGER NOT NULL,  
  currency TEXT NOT NULL DEFAULT 'CRD' REFERENCES currencies(code),  
  memo TEXT,  
  idempotency\_key TEXT UNIQUE  
);  
CREATE INDEX IF NOT EXISTS idx\_bank\_tx\_player\_ts ON bank\_tx(player\_id, ts);  
CREATE INDEX IF NOT EXISTS idx\_bank\_tx\_kind\_ts   ON bank\_tx(kind, ts);

CREATE TABLE IF NOT EXISTS bank\_interest\_policy (  
  id INTEGER PRIMARY KEY CHECK (id \= 1),  
  apr\_bps INTEGER NOT NULL DEFAULT 0 CHECK (apr\_bps \>= 0),  
  compounding TEXT NOT NULL DEFAULT 'daily' CHECK (compounding IN ('none','daily','weekly','monthly')),  
  min\_balance INTEGER NOT NULL DEFAULT 0 CHECK (min\_balance \>= 0),  
  max\_balance INTEGER NOT NULL DEFAULT 9223372036854775807,  
  last\_run\_at TEXT,  
  currency TEXT NOT NULL DEFAULT 'CRD' REFERENCES currencies(code)  
);  
INSERT OR IGNORE INTO bank\_interest\_policy (id, apr\_bps, compounding, min\_balance, max\_balance, last\_run\_at, currency)  
VALUES (1, 0, 'daily', 0, 9223372036854775807, NULL, 'CRD');

CREATE TABLE IF NOT EXISTS bank\_orders (  
  id INTEGER PRIMARY KEY,  
  player\_id INTEGER NOT NULL REFERENCES players(id) ON DELETE CASCADE,  
  kind TEXT NOT NULL CHECK (kind IN ('recurring','once')),  
  schedule TEXT NOT NULL,                 \-- cron-like or JSON schedule  
  next\_run\_at TEXT,  
  enabled INTEGER NOT NULL DEFAULT 1 CHECK (enabled IN (0,1)),  
  amount INTEGER NOT NULL CHECK (amount \> 0),  
  currency TEXT NOT NULL DEFAULT 'CRD' REFERENCES currencies(code),  
  to\_entity TEXT NOT NULL CHECK (to\_entity IN ('player','corp','gov','npc')),  
  to\_id INTEGER NOT NULL,  
  memo TEXT  
);

CREATE TABLE IF NOT EXISTS bank\_flags (  
  player\_id INTEGER PRIMARY KEY REFERENCES players(id) ON DELETE CASCADE,  
  is\_frozen INTEGER NOT NULL DEFAULT 0 CHECK (is\_frozen IN (0,1)),  
  risk\_tier TEXT NOT NULL DEFAULT 'normal' CHECK (risk\_tier IN ('normal','elevated','high','blocked'))  
);

CREATE TRIGGER IF NOT EXISTS trg\_bank\_tx\_before\_insert  
BEFORE INSERT ON bank\_tx  
FOR EACH ROW  
BEGIN  
  INSERT OR IGNORE INTO bank\_accounts(player\_id, currency, balance, last\_interest\_at)  
  VALUES (NEW.player\_id, COALESCE(NEW.currency,'CRD'), 0, NULL);

  SELECT CASE  
    WHEN NEW.kind IN ('withdraw','transfer\_out')  
      AND (SELECT balance FROM bank\_accounts WHERE player\_id \= NEW.player\_id) \- NEW.amount \< 0  
    THEN RAISE(ABORT, 'BANK\_INSUFFICIENT\_FUNDS')  
    ELSE 1  
  END;

  SET NEW.balance\_after \=  
    CASE NEW.kind  
      WHEN 'withdraw'     THEN (SELECT balance FROM bank\_accounts WHERE player\_id \= NEW.player\_id) \- NEW.amount  
      WHEN 'transfer\_out' THEN (SELECT balance FROM bank\_accounts WHERE player\_id \= NEW.player\_id) \- NEW.amount  
      WHEN 'deposit'      THEN (SELECT balance FROM bank\_accounts WHERE player\_id \= NEW.player\_id) \+ NEW.amount  
      WHEN 'transfer\_in'  THEN (SELECT balance FROM bank\_accounts WHERE player\_id \= NEW.player\_id) \+ NEW.amount  
      WHEN 'interest'     THEN (SELECT balance FROM bank\_accounts WHERE player\_id \= NEW.player\_id) \+ NEW.amount  
      WHEN 'adjustment'   THEN (SELECT balance FROM bank\_accounts WHERE player\_id \= NEW.player\_id) \+ NEW.amount  
      ELSE RAISE(ABORT, 'BANK\_UNKNOWN\_KIND')  
    END;

  SELECT CASE WHEN NEW.balance\_after \< 0 THEN RAISE(ABORT,'BANK\_NEGATIVE\_BALANCE') ELSE 1 END;  
END;

CREATE TRIGGER IF NOT EXISTS trg\_bank\_tx\_after\_insert  
AFTER INSERT ON bank\_tx  
FOR EACH ROW  
BEGIN  
  UPDATE bank\_accounts SET balance \= NEW.balance\_after WHERE player\_id \= NEW.player\_id;  
END;

CREATE TRIGGER IF NOT EXISTS trg\_bank\_tx\_before\_delete  
BEFORE DELETE ON bank\_tx  
FOR EACH ROW  
BEGIN  
  SELECT RAISE(ABORT, 'BANK\_LEDGER\_APPEND\_ONLY');  
END;

CREATE TRIGGER IF NOT EXISTS trg\_bank\_tx\_before\_update  
BEFORE UPDATE ON bank\_tx  
FOR EACH ROW  
BEGIN  
  SELECT RAISE(ABORT, 'BANK\_LEDGER\_IMMUTABLE');  
END;

CREATE VIEW IF NOT EXISTS bank\_statement AS  
SELECT id, player\_id, ts, kind, amount, balance\_after, currency, memo  
FROM bank\_tx  
ORDER BY ts ASC, id ASC;

CREATE VIEW IF NOT EXISTS bank\_balances AS  
SELECT player\_id, currency, balance, last\_interest\_at  
FROM bank\_accounts;

\-- I.b. BANKING (CORPORATE)  
CREATE TABLE IF NOT EXISTS corp\_accounts (  
  corp\_id INTEGER PRIMARY KEY REFERENCES corps(id) ON DELETE CASCADE,  
  currency TEXT NOT NULL DEFAULT 'CRD' REFERENCES currencies(code),  
  balance INTEGER NOT NULL DEFAULT 0 CHECK (balance \>= 0),  
  last\_interest\_at TEXT  
);

CREATE TABLE IF NOT EXISTS corp\_tx (  
  id INTEGER PRIMARY KEY,  
  corp\_id INTEGER NOT NULL REFERENCES corps(id) ON DELETE CASCADE,  
  ts TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')),  
  kind TEXT NOT NULL CHECK (kind IN ('deposit','withdraw','transfer\_in','transfer\_out','interest','dividend','salary','adjustment')),  
  amount INTEGER NOT NULL CHECK (amount \> 0),  
  balance\_after INTEGER NOT NULL,  
  currency TEXT NOT NULL DEFAULT 'CRD' REFERENCES currencies(code),  
  memo TEXT,  
  idempotency\_key TEXT UNIQUE  
);  
CREATE INDEX IF NOT EXISTS idx\_corp\_tx\_corp\_ts ON corp\_tx(corp\_id, ts);

CREATE TABLE IF NOT EXISTS corp\_interest\_policy (  
  id INTEGER PRIMARY KEY CHECK (id \= 1),  
  apr\_bps INTEGER NOT NULL DEFAULT 0 CHECK (apr\_bps \>= 0),  
  compounding TEXT NOT NULL DEFAULT 'none' CHECK (compounding IN ('none','daily','weekly','monthly')),  
  last\_run\_at TEXT,  
  currency TEXT NOT NULL DEFAULT 'CRD' REFERENCES currencies(code)  
);  
INSERT OR IGNORE INTO corp\_interest\_policy (id, apr\_bps, compounding, last\_run\_at, currency)  
VALUES (1, 0, 'none', NULL, 'CRD');

CREATE TRIGGER IF NOT EXISTS trg\_corp\_tx\_before\_insert  
BEFORE INSERT ON corp\_tx  
FOR EACH ROW  
BEGIN  
  INSERT OR IGNORE INTO corp\_accounts(corp\_id, currency, balance, last\_interest\_at)  
  VALUES (NEW.corp\_id, COALESCE(NEW.currency,'CRD'), 0, NULL);

  SELECT CASE  
    WHEN NEW.kind IN ('withdraw','transfer\_out','dividend','salary')  
      AND (SELECT balance FROM corp\_accounts WHERE corp\_id \= NEW.corp\_id) \- NEW.amount \< 0  
    THEN RAISE(ABORT, 'CORP\_INSUFFICIENT\_FUNDS')  
    ELSE 1  
  END;

  SET NEW.balance\_after \=  
    CASE NEW.kind  
      WHEN 'withdraw'     THEN (SELECT balance FROM corp\_accounts WHERE corp\_id \= NEW.corp\_id) \- NEW.amount  
      WHEN 'transfer\_out' THEN (SELECT balance FROM corp\_accounts WHERE corp\_id \= NEW.corp\_id) \- NEW.amount  
      WHEN 'dividend'     THEN (SELECT balance FROM corp\_accounts WHERE corp\_id \= NEW.corp\_id) \- NEW.amount  
      WHEN 'salary'       THEN (SELECT balance FROM corp\_accounts WHERE corp\_id \= NEW.corp\_id) \- NEW.amount  
      WHEN 'deposit'      THEN (SELECT balance FROM corp\_accounts WHERE corp\_id \= NEW.corp\_id) \+ NEW.amount  
      WHEN 'transfer\_in'  THEN (SELECT balance FROM corp\_accounts WHERE corp\_id \= NEW.corp\_id) \+ NEW.amount  
      WHEN 'interest'     THEN (SELECT balance FROM corp\_accounts WHERE corp\_id \= NEW.corp\_id) \+ NEW.amount  
      WHEN 'adjustment'   THEN (SELECT balance FROM corp\_accounts WHERE corp\_id \= NEW.corp\_id) \+ NEW.amount  
      ELSE RAISE(ABORT, 'CORP\_UNKNOWN\_KIND')  
    END;

  SELECT CASE WHEN NEW.balance\_after \< 0 THEN RAISE(ABORT,'CORP\_NEGATIVE\_BALANCE') ELSE 1 END;  
END;

CREATE TRIGGER IF NOT EXISTS trg\_corp\_tx\_after\_insert  
AFTER INSERT ON corp\_tx  
FOR EACH ROW  
BEGIN  
  UPDATE corp\_accounts SET balance \= NEW.balance\_after WHERE corp\_id \= NEW.corp\_id;  
END;

CREATE VIEW IF NOT EXISTS corp\_statement AS  
SELECT id, corp\_id, ts, kind, amount, balance\_after, currency, memo  
FROM corp\_tx  
ORDER BY ts ASC, id ASC;

\-- II. STOCKS & EXCHANGE  
CREATE TABLE IF NOT EXISTS stocks (  
  id INTEGER PRIMARY KEY,  
  corp\_id INTEGER NOT NULL REFERENCES corps(id) ON DELETE CASCADE,  
  ticker TEXT NOT NULL UNIQUE,  
  total\_shares INTEGER NOT NULL CHECK (total\_shares \> 0),  
  par\_value INTEGER NOT NULL DEFAULT 0 CHECK (par\_value \>= 0),  
  current\_price INTEGER NOT NULL DEFAULT 0 CHECK (current\_price \>= 0),  
  last\_dividend\_ts TEXT  
);

CREATE TABLE IF NOT EXISTS stock\_orders (  
  id INTEGER PRIMARY KEY,  
  player\_id INTEGER NOT NULL REFERENCES players(id) ON DELETE CASCADE,  
  stock\_id INTEGER NOT NULL REFERENCES stocks(id) ON DELETE CASCADE,  
  type TEXT NOT NULL CHECK (type IN ('buy','sell')),  
  quantity INTEGER NOT NULL CHECK (quantity \> 0),  
  price INTEGER NOT NULL CHECK (price \>= 0),  
  status TEXT NOT NULL DEFAULT 'open' CHECK (status IN ('open','filled','cancelled','expired')),  
  ts TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now'))  
);  
CREATE INDEX IF NOT EXISTS idx\_stock\_orders\_stock ON stock\_orders(stock\_id, status);

CREATE TABLE IF NOT EXISTS stock\_trades (  
  id INTEGER PRIMARY KEY,  
  stock\_id INTEGER NOT NULL REFERENCES stocks(id) ON DELETE CASCADE,  
  buyer\_id INTEGER NOT NULL REFERENCES players(id) ON DELETE CASCADE,  
  seller\_id INTEGER NOT NULL REFERENCES players(id) ON DELETE CASCADE,  
  quantity INTEGER NOT NULL CHECK (quantity \> 0),  
  price INTEGER NOT NULL CHECK (price \>= 0),  
  ts TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')),  
  settlement\_tx\_buy INTEGER,  
  settlement\_tx\_sell INTEGER  
);

CREATE TABLE IF NOT EXISTS stock\_dividends (  
  id INTEGER PRIMARY KEY,  
  stock\_id INTEGER NOT NULL REFERENCES stocks(id) ON DELETE CASCADE,  
  amount\_per\_share INTEGER NOT NULL CHECK (amount\_per\_share \>= 0),  
  declared\_ts TEXT NOT NULL,  
  paid\_ts TEXT  
);

CREATE TABLE IF NOT EXISTS stock\_indices (  
  id INTEGER PRIMARY KEY,  
  name TEXT UNIQUE NOT NULL  
);  
CREATE TABLE IF NOT EXISTS stock\_index\_members (  
  index\_id INTEGER NOT NULL REFERENCES stock\_indices(id) ON DELETE CASCADE,  
  stock\_id INTEGER NOT NULL REFERENCES stocks(id) ON DELETE CASCADE,  
  weight REAL NOT NULL DEFAULT 1.0,  
  PRIMARY KEY (index\_id, stock\_id)  
);

\-- III. INSURANCE  
CREATE TABLE IF NOT EXISTS insurance\_funds (  
  id INTEGER PRIMARY KEY,  
  owner\_type TEXT NOT NULL CHECK (owner\_type IN ('system','corp','player')),  
  owner\_id INTEGER,  
  balance INTEGER NOT NULL DEFAULT 0 CHECK (balance \>= 0\)  
);

CREATE TABLE IF NOT EXISTS insurance\_policies (  
  id INTEGER PRIMARY KEY,  
  holder\_type TEXT NOT NULL CHECK (holder\_type IN ('player','corp')),  
  holder\_id INTEGER NOT NULL,  
  subject\_type TEXT NOT NULL CHECK (subject\_type IN ('ship','cargo','planet')),  
  subject\_id INTEGER NOT NULL,  
  premium INTEGER NOT NULL CHECK (premium \>= 0),  
  payout INTEGER NOT NULL CHECK (payout \>= 0),  
  fund\_id INTEGER REFERENCES insurance\_funds(id) ON DELETE SET NULL,  
  start\_ts TEXT NOT NULL,  
  expiry\_ts TEXT,  
  active INTEGER NOT NULL DEFAULT 1 CHECK (active IN (0,1))  
);  
CREATE INDEX IF NOT EXISTS idx\_policies\_holder ON insurance\_policies(holder\_type, holder\_id);

CREATE TABLE IF NOT EXISTS insurance\_claims (  
  id INTEGER PRIMARY KEY,  
  policy\_id INTEGER NOT NULL REFERENCES insurance\_policies(id) ON DELETE CASCADE,  
  event\_id TEXT,  
  amount INTEGER NOT NULL CHECK (amount \>= 0),  
  status TEXT NOT NULL DEFAULT 'open' CHECK (status IN ('open','paid','denied')),  
  ts TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')),  
  paid\_bank\_tx INTEGER  
);

CREATE TABLE IF NOT EXISTS risk\_profiles (  
  id INTEGER PRIMARY KEY,  
  entity\_type TEXT NOT NULL CHECK (entity\_type IN ('player','corp')),  
  entity\_id INTEGER NOT NULL,  
  risk\_score INTEGER NOT NULL DEFAULT 0  
);

\-- IV. LOANS & CREDIT  
CREATE TABLE IF NOT EXISTS loans (  
  id INTEGER PRIMARY KEY,  
  lender\_type TEXT NOT NULL CHECK (lender\_type IN ('player','corp','bank')),  
  lender\_id INTEGER,  
  borrower\_type TEXT NOT NULL CHECK (borrower\_type IN ('player','corp')),  
  borrower\_id INTEGER NOT NULL,  
  principal INTEGER NOT NULL CHECK (principal \> 0),  
  rate\_bps INTEGER NOT NULL DEFAULT 0 CHECK (rate\_bps \>= 0),  
  term\_days INTEGER NOT NULL CHECK (term\_days \> 0),  
  next\_due TEXT,  
  status TEXT NOT NULL DEFAULT 'active' CHECK (status IN ('active','paid','defaulted','written\_off')),  
  created\_ts TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now'))  
);

CREATE TABLE IF NOT EXISTS loan\_payments (  
  id INTEGER PRIMARY KEY,  
  loan\_id INTEGER NOT NULL REFERENCES loans(id) ON DELETE CASCADE,  
  ts TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')),  
  amount INTEGER NOT NULL CHECK (amount \> 0),  
  status TEXT NOT NULL DEFAULT 'posted' CHECK (status IN ('posted','reversed')),  
  bank\_tx\_id INTEGER  
);

CREATE TABLE IF NOT EXISTS collateral (  
  id INTEGER PRIMARY KEY,  
  loan\_id INTEGER NOT NULL REFERENCES loans(id) ON DELETE CASCADE,  
  asset\_type TEXT NOT NULL CHECK (asset\_type IN ('ship','planet','cargo','stock','other')),  
  asset\_id INTEGER NOT NULL,  
  appraised\_value INTEGER NOT NULL DEFAULT 0 CHECK (appraised\_value \>= 0\)  
);

CREATE TABLE IF NOT EXISTS credit\_ratings (  
  entity\_type TEXT NOT NULL CHECK (entity\_type IN ('player','corp')),  
  entity\_id INTEGER NOT NULL,  
  score INTEGER NOT NULL DEFAULT 600 CHECK (score BETWEEN 300 AND 900),  
  last\_update TEXT,  
  PRIMARY KEY (entity\_type, entity\_id)  
);

\-- V. EXPEDITIONS & CHARTERS  
CREATE TABLE IF NOT EXISTS charters (  
  id INTEGER PRIMARY KEY,  
  name TEXT NOT NULL UNIQUE,  
  granted\_by TEXT NOT NULL DEFAULT 'federation',  
  monopoly\_scope TEXT,  
  start\_ts TEXT NOT NULL,  
  expiry\_ts TEXT  
);

CREATE TABLE IF NOT EXISTS expeditions (  
  id INTEGER PRIMARY KEY,  
  leader\_player\_id INTEGER NOT NULL REFERENCES players(id) ON DELETE CASCADE,  
  charter\_id INTEGER REFERENCES charters(id) ON DELETE SET NULL,  
  goal TEXT NOT NULL,  
  target\_region TEXT,  
  pledged\_total INTEGER NOT NULL DEFAULT 0 CHECK (pledged\_total \>= 0),  
  duration\_days INTEGER NOT NULL DEFAULT 7 CHECK (duration\_days \> 0),  
  status TEXT NOT NULL DEFAULT 'planning' CHECK (status IN ('planning','launched','complete','failed','aborted')),  
  created\_ts TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now'))  
);

CREATE TABLE IF NOT EXISTS expedition\_backers (  
  expedition\_id INTEGER NOT NULL REFERENCES expeditions(id) ON DELETE CASCADE,  
  backer\_type TEXT NOT NULL CHECK (backer\_type IN ('player','corp')),  
  backer\_id INTEGER NOT NULL,  
  pledged\_amount INTEGER NOT NULL CHECK (pledged\_amount \>= 0),  
  share\_pct REAL NOT NULL CHECK (share\_pct \>= 0),  
  PRIMARY KEY (expedition\_id, backer\_type, backer\_id)  
);

CREATE TABLE IF NOT EXISTS expedition\_returns (  
  id INTEGER PRIMARY KEY,  
  expedition\_id INTEGER NOT NULL REFERENCES expeditions(id) ON DELETE CASCADE,  
  ts TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')),  
  amount INTEGER NOT NULL CHECK (amount \>= 0),  
  bank\_tx\_id INTEGER  
);

\-- VI. COMMODITIES & FUTURES  
CREATE TABLE IF NOT EXISTS commodities (  
  id INTEGER PRIMARY KEY,  
  code TEXT UNIQUE NOT NULL,  
  name TEXT NOT NULL,  
  base\_price INTEGER NOT NULL DEFAULT 0 CHECK (base\_price \>= 0),  
  volatility INTEGER NOT NULL DEFAULT 0 CHECK (volatility \>= 0\)  
);

CREATE TABLE IF NOT EXISTS commodity\_orders (  
  id INTEGER PRIMARY KEY,  
  actor\_type TEXT NOT NULL CHECK (actor\_type IN ('player','corp','npc','port')),  
  actor\_id INTEGER NOT NULL,  
  commodity\_id INTEGER NOT NULL REFERENCES commodities(id) ON DELETE CASCADE,  
  side TEXT NOT NULL CHECK (side IN ('buy','sell')),  
  quantity INTEGER NOT NULL CHECK (quantity \> 0),  
  price INTEGER NOT NULL CHECK (price \>= 0),  
  status TEXT NOT NULL DEFAULT 'open' CHECK (status IN ('open','filled','cancelled','expired')),  
  ts TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now'))  
);  
CREATE INDEX IF NOT EXISTS idx\_commodity\_orders\_comm ON commodity\_orders(commodity\_id, status);

CREATE TABLE IF NOT EXISTS commodity\_trades (  
  id INTEGER PRIMARY KEY,  
  commodity\_id INTEGER NOT NULL REFERENCES commodities(id) ON DELETE CASCADE,  
  buyer\_type TEXT NOT NULL CHECK (buyer\_type IN ('player','corp','npc','port')),  
  buyer\_id INTEGER NOT NULL,  
  seller\_type TEXT NOT NULL CHECK (seller\_type IN ('player','corp','npc','port')),  
  seller\_id INTEGER NOT NULL,  
  quantity INTEGER NOT NULL CHECK (quantity \> 0),  
  price INTEGER NOT NULL CHECK (price \>= 0),  
  ts TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')),  
  settlement\_tx\_buy INTEGER,  
  settlement\_tx\_sell INTEGER  
);

CREATE TABLE IF NOT EXISTS futures\_contracts (  
  id INTEGER PRIMARY KEY,  
  commodity\_id INTEGER NOT NULL REFERENCES commodities(id) ON DELETE CASCADE,  
  buyer\_type TEXT NOT NULL CHECK (buyer\_type IN ('player','corp')),  
  buyer\_id INTEGER NOT NULL,  
  seller\_type TEXT NOT NULL CHECK (seller\_type IN ('player','corp')),  
  seller\_id INTEGER NOT NULL,  
  strike\_price INTEGER NOT NULL CHECK (strike\_price \>= 0),  
  expiry\_ts TEXT NOT NULL,  
  status TEXT NOT NULL DEFAULT 'open' CHECK (status IN ('open','settled','defaulted','cancelled'))  
);

CREATE TABLE IF NOT EXISTS warehouses (  
  id INTEGER PRIMARY KEY,  
  location\_type TEXT NOT NULL CHECK (location\_type IN ('sector','planet','port')),  
  location\_id INTEGER NOT NULL,  
  owner\_type TEXT NOT NULL CHECK (owner\_type IN ('player','corp')),  
  owner\_id INTEGER NOT NULL  
);

\-- VII. GOVERNMENT & TAX  
CREATE TABLE IF NOT EXISTS gov\_accounts (  
  id INTEGER PRIMARY KEY,  
  name TEXT NOT NULL UNIQUE,  
  balance INTEGER NOT NULL DEFAULT 0 CHECK (balance \>= 0\)  
);

CREATE TABLE IF NOT EXISTS tax\_policies (  
  id INTEGER PRIMARY KEY,  
  name TEXT NOT NULL,  
  tax\_type TEXT NOT NULL CHECK (tax\_type IN ('trade','income','corp','wealth','transfer')),  
  rate\_bps INTEGER NOT NULL DEFAULT 0 CHECK (rate\_bps \>= 0),  
  active INTEGER NOT NULL DEFAULT 1 CHECK (active IN (0,1))  
);

CREATE TABLE IF NOT EXISTS tax\_ledgers (  
  id INTEGER PRIMARY KEY,  
  policy\_id INTEGER NOT NULL REFERENCES tax\_policies(id) ON DELETE CASCADE,  
  payer\_type TEXT NOT NULL CHECK (payer\_type IN ('player','corp')),  
  payer\_id INTEGER NOT NULL,  
  amount INTEGER NOT NULL CHECK (amount \>= 0),  
  ts TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')),  
  bank\_tx\_id INTEGER  
);

CREATE TABLE IF NOT EXISTS fines (  
  id INTEGER PRIMARY KEY,  
  issued\_by TEXT NOT NULL DEFAULT 'federation',  
  recipient\_type TEXT NOT NULL CHECK (recipient\_type IN ('player','corp')),  
  recipient\_id INTEGER NOT NULL,  
  reason TEXT,  
  amount INTEGER NOT NULL CHECK (amount \>= 0),  
  status TEXT NOT NULL DEFAULT 'unpaid' CHECK (status IN ('unpaid','paid','void')),  
  issued\_ts TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')),  
  paid\_bank\_tx INTEGER  
);

CREATE TABLE IF NOT EXISTS bounties (  
  id INTEGER PRIMARY KEY,  
  posted\_by\_type TEXT NOT NULL CHECK (posted\_by\_type IN ('player','corp','gov','npc')),  
  posted\_by\_id INTEGER,  
  target\_type TEXT NOT NULL CHECK (target\_type IN ('player','corp','npc')),  
  target\_id INTEGER NOT NULL,  
  reward INTEGER NOT NULL CHECK (reward \>= 0),  
  escrow\_bank\_tx INTEGER,  
  status TEXT NOT NULL DEFAULT 'open' CHECK (status IN ('open','claimed','cancelled','expired')),  
  posted\_ts TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')),  
  claimed\_by INTEGER,  
  paid\_bank\_tx INTEGER  
);

CREATE TABLE IF NOT EXISTS grants (  
  id INTEGER PRIMARY KEY,  
  name TEXT NOT NULL,  
  recipient\_type TEXT NOT NULL CHECK (recipient\_type IN ('player','corp')),  
  recipient\_id INTEGER NOT NULL,  
  amount INTEGER NOT NULL CHECK (amount \>= 0),  
  awarded\_ts TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')),  
  bank\_tx\_id INTEGER  
);

\-- VIII. RESEARCH & DEVELOPMENT  
CREATE TABLE IF NOT EXISTS research\_projects (  
  id INTEGER PRIMARY KEY,  
  sponsor\_type TEXT NOT NULL CHECK (sponsor\_type IN ('player','corp','gov')),  
  sponsor\_id INTEGER,  
  title TEXT NOT NULL,  
  field TEXT NOT NULL,  
  cost INTEGER NOT NULL CHECK (cost \>= 0),  
  progress INTEGER NOT NULL DEFAULT 0 CHECK (progress BETWEEN 0 AND 100),  
  status TEXT NOT NULL DEFAULT 'active' CHECK (status IN ('active','paused','complete','failed')),  
  created\_ts TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now'))  
);

CREATE TABLE IF NOT EXISTS research\_contributors (  
  project\_id INTEGER NOT NULL REFERENCES research\_projects(id) ON DELETE CASCADE,  
  actor\_type TEXT NOT NULL CHECK (actor\_type IN ('player','corp')),  
  actor\_id INTEGER NOT NULL,  
  amount INTEGER NOT NULL CHECK (amount \>= 0),  
  PRIMARY KEY (project\_id, actor\_type, actor\_id)  
);

CREATE TABLE IF NOT EXISTS research\_results (  
  id INTEGER PRIMARY KEY,  
  project\_id INTEGER NOT NULL REFERENCES research\_projects(id) ON DELETE CASCADE,  
  blueprint\_code TEXT NOT NULL,  
  unlocked\_ts TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now'))  
);

\-- IX. CRIME & UNDERWORLD  
CREATE TABLE IF NOT EXISTS black\_accounts (  
  id INTEGER PRIMARY KEY,  
  owner\_type TEXT NOT NULL CHECK (owner\_type IN ('player','corp','npc')),  
  owner\_id INTEGER NOT NULL,  
  balance INTEGER NOT NULL DEFAULT 0 CHECK (balance \>= 0\)  
);

CREATE TABLE IF NOT EXISTS laundering\_ops (  
  id INTEGER PRIMARY KEY,  
  from\_black\_id INTEGER REFERENCES black\_accounts(id) ON DELETE SET NULL,  
  to\_player\_id INTEGER REFERENCES players(id) ON DELETE SET NULL,  
  amount INTEGER NOT NULL CHECK (amount \> 0),  
  risk\_pct INTEGER NOT NULL DEFAULT 25 CHECK (risk\_pct BETWEEN 0 AND 100),  
  status TEXT NOT NULL DEFAULT 'pending' CHECK (status IN ('pending','cleaned','seized','failed')),  
  ts TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now'))  
);

CREATE TABLE IF NOT EXISTS contracts\_illicit (  
  id INTEGER PRIMARY KEY,  
  contractor\_type TEXT NOT NULL CHECK (contractor\_type IN ('player','corp','npc')),  
  contractor\_id INTEGER NOT NULL,  
  target\_type TEXT NOT NULL CHECK (target\_type IN ('player','corp','npc')),  
  target\_id INTEGER NOT NULL,  
  reward INTEGER NOT NULL CHECK (reward \>= 0),  
  escrow\_black\_id INTEGER REFERENCES black\_accounts(id) ON DELETE SET NULL,  
  status TEXT NOT NULL DEFAULT 'open' CHECK (status IN ('open','fulfilled','failed','cancelled')),  
  ts TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now'))  
);

CREATE TABLE IF NOT EXISTS fences (  
  id INTEGER PRIMARY KEY,  
  npc\_id INTEGER,  
  sector\_id INTEGER,  
  reputation INTEGER NOT NULL DEFAULT 0  
);

\-- X. MACRO-ECONOMY  
CREATE TABLE IF NOT EXISTS economic\_indicators (  
  id INTEGER PRIMARY KEY,  
  ts TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')),  
  inflation\_bps INTEGER NOT NULL DEFAULT 0,  
  liquidity INTEGER NOT NULL DEFAULT 0,  
  credit\_velocity REAL NOT NULL DEFAULT 0.0  
);

CREATE TABLE IF NOT EXISTS sector\_gdp (  
  sector\_id INTEGER PRIMARY KEY,  
  gdp INTEGER NOT NULL DEFAULT 0,  
  last\_update TEXT  
);

CREATE TABLE IF NOT EXISTS event\_triggers (  
  id INTEGER PRIMARY KEY,  
  name TEXT NOT NULL,  
  condition\_json TEXT NOT NULL,  
  action\_json TEXT NOT NULL  
);

\-- XI. CULTURE, GUILDS, CHARITIES  
CREATE TABLE IF NOT EXISTS charities (  
  id INTEGER PRIMARY KEY,  
  name TEXT NOT NULL UNIQUE,  
  description TEXT  
);

CREATE TABLE IF NOT EXISTS donations (  
  id INTEGER PRIMARY KEY,  
  charity\_id INTEGER NOT NULL REFERENCES charities(id) ON DELETE CASCADE,  
  donor\_type TEXT NOT NULL CHECK (donor\_type IN ('player','corp')),  
  donor\_id INTEGER NOT NULL,  
  amount INTEGER NOT NULL CHECK (amount \>= 0),  
  ts TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')),  
  bank\_tx\_id INTEGER  
);

CREATE TABLE IF NOT EXISTS temples (  
  id INTEGER PRIMARY KEY,  
  name TEXT NOT NULL UNIQUE,  
  sector\_id INTEGER,  
  favour INTEGER NOT NULL DEFAULT 0  
);

CREATE TABLE IF NOT EXISTS guilds (  
  id INTEGER PRIMARY KEY,  
  name TEXT NOT NULL UNIQUE,  
  description TEXT  
);

CREATE TABLE IF NOT EXISTS guild\_memberships (  
  guild\_id INTEGER NOT NULL REFERENCES guilds(id) ON DELETE CASCADE,  
  member\_type TEXT NOT NULL CHECK (member\_type IN ('player','corp')),  
  member\_id INTEGER NOT NULL,  
  role TEXT NOT NULL DEFAULT 'member',  
  PRIMARY KEY (guild\_id, member\_type, member\_id)  
);

CREATE TABLE IF NOT EXISTS guild\_dues (  
  id INTEGER PRIMARY KEY,  
  guild\_id INTEGER NOT NULL REFERENCES guilds(id) ON DELETE CASCADE,  
  amount INTEGER NOT NULL CHECK (amount \>= 0),  
  period TEXT NOT NULL DEFAULT 'monthly' CHECK (period IN ('weekly','monthly','quarterly','yearly'))  
);

\-- XII. ANALYTICS & POLICIES  
CREATE TABLE IF NOT EXISTS economy\_snapshots (  
  id INTEGER PRIMARY KEY,  
  ts TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')),  
  money\_supply INTEGER NOT NULL DEFAULT 0,  
  total\_deposits INTEGER NOT NULL DEFAULT 0,  
  total\_loans INTEGER NOT NULL DEFAULT 0,  
  total\_insured INTEGER NOT NULL DEFAULT 0,  
  notes TEXT  
);

CREATE TABLE IF NOT EXISTS ai\_economy\_agents (  
  id INTEGER PRIMARY KEY,  
  name TEXT NOT NULL,  
  role TEXT NOT NULL,  
  config\_json TEXT NOT NULL  
);

CREATE TABLE IF NOT EXISTS anomaly\_reports (  
  id INTEGER PRIMARY KEY,  
  ts TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')),  
  severity TEXT NOT NULL CHECK (severity IN ('low','medium','high','critical')),  
  subject TEXT NOT NULL,  
  details TEXT NOT NULL,  
  resolved INTEGER NOT NULL DEFAULT 0 CHECK (resolved IN (0,1))  
);

CREATE TABLE IF NOT EXISTS economy\_policies (  
  id INTEGER PRIMARY KEY,  
  name TEXT NOT NULL UNIQUE,  
  config\_json TEXT NOT NULL,  
  active INTEGER NOT NULL DEFAULT 1 CHECK (active IN (0,1))  
);

\-- Recommended Views  
CREATE VIEW IF NOT EXISTS v\_player\_networth AS  
SELECT p.id AS player\_id,  
       p.name AS player\_name,  
       COALESCE(ba.balance,0) AS bank\_balance  
FROM players p  
LEFT JOIN bank\_accounts ba ON ba.player\_id \= p.id;

CREATE VIEW IF NOT EXISTS v\_corp\_treasury AS  
SELECT c.id AS corp\_id,  
       c.name AS corp\_name,  
       COALESCE(ca.balance,0) AS bank\_balance  
FROM corps c  
LEFT JOIN corp\_accounts ca ON ca.corp\_id \= c.id;

CREATE VIEW IF NOT EXISTS v\_bounty\_board AS  
SELECT  
  b.id,  
  b.target\_type,  
  b.target\_id,  
  p\_target.name AS target\_name,  
  b.reward,  
  b.status,  
  b.posted\_by\_type,  
  b.posted\_by\_id,  
  CASE b.posted\_by\_type  
    WHEN 'player' THEN p\_poster.name  
    WHEN 'corp' THEN c\_poster.name  
    ELSE b.posted\_by\_type  
  END AS poster\_name,  
  b.posted\_ts  
FROM bounties b  
LEFT JOIN players p\_target ON b.target\_type \= 'player' AND b.target\_id \= p\_target.id  
LEFT JOIN players p\_poster ON b.posted\_by\_type \= 'player' AND b.posted\_by\_id \= p\_poster.id  
LEFT JOIN corps c\_poster ON b.posted\_by\_type \= 'corp' AND b.posted\_by\_id \= c\_poster.id  
WHERE b.status \= 'open';

CREATE VIEW IF NOT EXISTS v\_bank\_leaderboard AS  
SELECT  
  ba.player\_id,  
  p.name,  
  ba.balance  
FROM bank\_accounts ba  
JOIN players p ON ba.player\_id \= p.id  
\-- Assumes a 'privacy.show\_leaderboard' pref from player\_prefs  
LEFT JOIN player\_prefs pp ON ba.player\_id \= pp.player\_id AND pp.key \= 'privacy.show\_leaderboard'  
WHERE COALESCE(pp.value, 'true') \= 'true'  
ORDER BY ba.balance DESC;

