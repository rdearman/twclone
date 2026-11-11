
# twclone

A modern, C-based recreation of classic BBS-era space-trading gameplay (in the spirit of TradeWars 2002). **twclone** provides a headless server, active Godot and Python clients, and a deterministic “Big Bang” universe generator—now backed by **PostgreSQL** (with support for MySQL and other database engines) with a **JSON** protocol that makes writing clients (or AI bots) straightforward.

> **What’s new (2025):**
>
> * Full **PostgreSQL** data model with multi-database support (MySQL, etc.)
> * **JSON** protocol for client & bot compatibility
> * Two active clients: the native **Godot client** for the illustrated game UI and the **Python client** for terminal/reference workflows and protocol testing
> * A separate **Game Engine** process (forked) that runs clocks, economy, maintenance, NPC stubs, and enforcement via **durable DB rails** and a **TCP S2S** control channel
> * **DB-backed configuration** & secrets with live reload
> * Cleaner broadcast pipeline to players
> * **Massive concurrent player support:** PostgreSQL supports 100+ connections out-of-box; see **[`docs/PRODUCTION_DEPLOYMENT_GUIDE.md`](./docs/PRODUCTION_DEPLOYMENT_GUIDE.md)** for connection-pooling guidance

**Migration from SQLite (2025):** Early builds used SQLite for simplicity, but single-writer limitations proved problematic for scaling concurrent players. PostgreSQL removes this blocker and supports 100+ connections natively; see **[`docs/PRODUCTION_DEPLOYMENT_GUIDE.md`](./docs/PRODUCTION_DEPLOYMENT_GUIDE.md)** for connection-pooling guidance. Stress tests confirmed this change enables massive concurrent player counts.

If you’re here from SourceForge: welcome back! The original code (largely GPL-era) is still available there; this repo is a ground-up rewrite focused on DB storage and JSON I/O. Portions I authored are now under **MIT**. Big thanks to the original collaborators; see **Credits** at the end.

---

## Table of contents

* [Project layout](#project-layout)
* [Quick start](#quick-start)
* [Build from source](#build-from-source)
* [Running the server](#running-the-server)
* [Running the clients](#running-the-clients)
* [Universe generation (“Big Bang”)](#universe-generation-big-bang)
* [Game Engine (overview)](#game-engine-overview)
* [Configuration (DB-backed)](#configuration-db-backed)
* [Protocol](#protocol)
* [Database](#database)
* [Security](#security)
* [Roadmap](#roadmap)
* [Contributing](#contributing)
* [Licence](#licence)
* [Credits](#credits)

---

## Project layout

```
twclone/
├─ bin/                 # Built artefacts: server, client, test_bang
├─ src/                 # C sources (server_loop.c, engine/*.c, …)
├─ client/
│  ├─ godot_client/     # Active native Godot gameplay client
│  ├─ python_client/    # Active terminal/reference client and test client
│  ├─ angular_client/   # Reserve web client implementation
│  ├─ web_client/       # Reserve web client area
│  └─ C_client/         # Reserve/legacy client area
├─ data/                # menus.json and other runtime data
├─ docs/                # ENGINE.md, PROTOCOL.md, SYSOP.md, design notes
├─ Makefile.am …        # Autotools build files
└─ README.md            # This file
```

---

## Quick start

```bash
# 1) Build
make clean && make -j

# 2) Configure PostgreSQL (or MySQL/other DB)
# See docs/PRODUCTION_DEPLOYMENT_GUIDE.md for production pooling setup

# 3) Build the Universe
    ./bin/bigbang.json 
    a) (copy the sample.bigbang.json to bigbang.json and edit the file to show your db connection string, your desired universe size, etc.)
    ./bigbang
    b) run executable which will read the bigbang.json file and create your universe

# 4) Start the server
./server

# 5) Connect with an active client
# Python reference client:
python3 client/python_client/client.py --help
# Or open client/godot_client/godot/project.godot in Godot 4.

```

> **DB Setup:** See **[`docs/PRODUCTION_DEPLOYMENT_GUIDE.md`](./docs/PRODUCTION_DEPLOYMENT_GUIDE.md)** for PostgreSQL connection-pooling guidance.

---

## Build from source

**Prereqs:** GCC/Clang, GNU make, PostgreSQL dev libraries (or MySQL), POSIX (Linux/WSL/macOS).
**Build:**

```bash
make clean && make -j
# Artefacts land in ./bin
```

Verbose:

```bash
make V=1
```

---

## Running the server

```bash
./bin/server --host 0.0.0.0 --port 1234
```

* `--host <addr>` (default `0.0.0.0`)
* `--port <port>` (default `1234`)

Typical logs:

```
tail -f bin/twclone.log
server: starting…
server: listening on 0.0.0.0:1234
```

---

## Running the clients

The actively maintained clients are:

* **Godot — recommended.** This is the most advanced and full-featured gameplay client, with an illustrated sector view, object inspection and contextual actions, navigation, port and planet workflows, cargo/status indicators, and a responsive desktop interface. See [`client/godot_client/godot/`](./client/godot_client/godot/).
* **Python.** Use this terminal client for lightweight protocol workflows, automation, and regression coverage. See [`client/python_client/README.md`](./client/python_client/README.md).

The repository also contains reserve or alternate client implementations. The Angular/web clients and the C client area are kept for reference and future work; they are not the current primary gameplay clients.

You can also write another client in any language that speaks the JSON protocol.

### Python client

```bash
python3 client/python_client/client.py --help
```

### Godot client

Open `client/godot_client/godot/project.godot` in Godot 4 and run the project. Configure the server connection through the client’s login screen.

### Reserve clients

* [`client/angular_client/`](./client/angular_client/) — Angular/web implementation reserved for future browser-client work.
* [`client/web_client/`](./client/web_client/) — reserved web-client area.
* [`client/C_client/`](./client/C_client/) — reserved/legacy native client area.

---

## Universe generation (“Big Bang”)

Create/seed a fresh universe:

```bash
./bin/bigbang
```

Typical log:

```
BIGBANG: Creating universe…
BIGBANG: Creating 500 sectors (1–10 reserved for FedSpace)…
BIGBANG: Warps, tunnels, one-ways, dead-ends…
```

* **FedSpace:** sectors `1–10` are protected and reserved.
* **Graph:** mix of bidirectional links, one-ways, dead-ends, and short “tunnels”.

---

## Game Engine (overview)

The **engine** is a separate process responsible for clocks, economy, maintenance, NPC scaffolding, and Imperial enforcement.

* Communicates with the server over **TCP** using a small, versioned **S2S** protocol (length-prefixed JSON + HMAC).
* Uses the database as **source of truth**, with two durable rails:

  * **events** (server→engine): facts that happened.
  * **commands** (engine→server): requested mutations.
* Drives a **short-tick loop** and **cron** jobs (daily/periodic).
* Publishes **system notices**; the server’s broadcast pump delivers them to online players.

🔗 See **[`docs/ENGINE.md`](./docs/ENGINE.md)** for the full design, schemas, idempotency, poison handling, retention, and protocol message catalog.

---

## Configuration (DB-backed)

All configuration lives in the database, and the inital configuration is copied from the bigbang.json file at creation. You can modify turnsperday and other config uration items by logging into the db and doing update on the config table. 

* `config` (typed key/values by scope), `config_version` (live reload), `config_audit` (history), and `s2s_keys` (HMAC secrets).
* The server/engine load config at startup, validate types/ranges, and can **live-reload** after a version bump.

Initial seeds are created on first run; see **ENGINE.md** for the exact table definitions and reload messages.

---

## Protocol

All client↔server interactions use JSON. The engine↔server (S2S) control channel also uses JSON over TCP with a small envelope.

* **Client protocol:** request/response envelopes, errors, deprecations.
* **Engine↔Server (S2S):** `s2s.health.check`, `s2s.broadcast.sweep`, `s2s.command.push`, `s2s.config.bump`, `s2s.engine.shutdown`.

🔗 See **[`docs/PROTOCOL.v3/00_index.md`](./docs/PROTOCOL.v3/00_index.md)** for structures and examples.
🔗 Engine S2S specifics are also summarized in **[`docs/ENGINE.md`](./docs/ENGINE.md)**.

---

## Database

* **Engine:** PostgreSQL (primary), with MySQL and other database support.
* **Scaling:** 100+ concurrent connections out-of-box; scalable via **[connection pooling](./docs/PRODUCTION_DEPLOYMENT_GUIDE.md)**.
* **Schema:** created/verified at first run (or by `test_bang`).
* **Reset:** drop and recreate the database, then re-run `test_bang` (or start the server to re-seed essentials).

Handy CLI (PostgreSQL):

```bash
psql -U twclone_user -d twclone_db -c "\d"
psql -U twclone_user -d twclone_db -c "SELECT * FROM sectors LIMIT 10;"
```

---

## Security

**Now:** LAN-friendly for development. Game is playable and functional.
**Target hardening:**

* Store **password hashes** (e.g., Argon2id) instead of plaintext.
* Gate brute force with rate limits/lockouts.
* Add **TLS encryption** (currently uses plaintext telnet-style connections; Docker deployment can wrap with TLS).
* Keep **HMAC keys** for S2S in `s2s_keys` (DB), never in logs.

---

## Roadmap

This roadmap defines release goals, not implementation or verification claims.
Current implementation evidence and known gaps are tracked separately in
[`docs/V3_STATUS.md`](docs/V3_STATUS.md) and
[`docs/V3_GAMEPLAY_PARITY.md`](docs/V3_GAMEPLAY_PARITY.md). A feature is not
considered release-complete solely because code or a protocol command exists.

### V2 — Core Platform

V2 is the established platform baseline: a modern C server, PostgreSQL and
other database backends, a JSON protocol, a separate game-engine process,
universe generation, Python and Godot clients, and foundational gameplay.
This describes the platform foundation; it does not assert that every
gameplay subsystem has been fully verified.

### V3 — Classic TradeWars 2002 Parity

**Release goal:** complete the agreed original TradeWars 2002 gameplay scope,
alongside approved V3-specific rules. The release contract is
[GitHub issue #491](https://github.com/rdearman/twclone/issues/491).

Outstanding parity work includes StarPort construction, upgrades and
destruction; photon firing; toll fighters; transwarp fuel rules; and corporate
asset transfers. These are goals, not claims of completion. V3 release
acceptance requires end-to-end verification in both the Python and Godot
clients, database migration testing, and regression certification.

### V4 — Enhanced Play

**Release goal:** add economic and strategic gameplay beyond the original
TradeWars 2002 baseline, including StarPort ownership, freight contracts,
planetary industry, shipyards, corporate equity, mercenaries, territory and
salvage. The programme is tracked by
[epic #526](https://github.com/rdearman/twclone/issues/526) and
[issues #525](https://github.com/rdearman/twclone/issues/525),
[#526](https://github.com/rdearman/twclone/issues/526),
[#527](https://github.com/rdearman/twclone/issues/527),
[#528](https://github.com/rdearman/twclone/issues/528),
[#529](https://github.com/rdearman/twclone/issues/529),
[#530](https://github.com/rdearman/twclone/issues/530), and
[#531](https://github.com/rdearman/twclone/issues/531). These are planned
features, not a statement that they are implemented.

### V5 — Federation

**Release goal:** connect independently operated twclone servers through
federation, covering peer trust, identity, inter-universe travel,
communications and economic consistency. Existing federation roadmap issues
[#472](https://github.com/rdearman/twclone/issues/472),
[#473](https://github.com/rdearman/twclone/issues/473),
[#474](https://github.com/rdearman/twclone/issues/474),
[#475](https://github.com/rdearman/twclone/issues/475),
[#476](https://github.com/rdearman/twclone/issues/476),
[#477](https://github.com/rdearman/twclone/issues/477),
[#478](https://github.com/rdearman/twclone/issues/478),
[#479](https://github.com/rdearman/twclone/issues/479),
[#480](https://github.com/rdearman/twclone/issues/480),
[#481](https://github.com/rdearman/twclone/issues/481),
[#482](https://github.com/rdearman/twclone/issues/482), and
[#483](https://github.com/rdearman/twclone/issues/483) retain their numbers
and are assigned to this V5 programme. This server-to-server
federation is distinct from the in-game Federation faction and its V3
jurisdiction mechanics. V5 functionality is planned, not represented as
complete.

For technical architecture and implementation detail, see
[`docs/ENGINE.md`](docs/ENGINE.md) and the linked issue programmes above.

---

## Contributing

1. Open or reference a **GitHub issue** (especially for protocol changes).
2. Keep functionality intact unless the change is explicitly scoped.
3. Write clear, readable C; prefer short transactions; log errors.
4. Before a PR:

   ```bash
   make clean && make -j
   ./bin/test_bang
   ./bin/server --host 0.0.0.0 --port 1234
   ./bin/client --host localhost --port 1234 --menus ./data/menus.json
   ```
5. Include tests or a reproducible scenario where it makes sense.

---

## Licence

**MIT**

* Historic SourceForge material contained GPL’d portions; this rewrite replaces those systems with a DB-backed, JSON-speaking implementation. Newly authored code in this repo is under **MIT**. ALL the code in the system has now been redone and therefore all if MIT license. 

---

## Credits

Huge thanks to the original contributors and community that kept the TW flame alive.

* Website (historical): `http://twclone.sourceforge.net`
* © 2000 Jason C. Garcowski ([jcg5@po.cwru.edu](mailto:jcg5@po.cwru.edu))
* © 2002 Ryan Glasnapp ([rglasnap@nmt.edu](mailto:rglasnap@nmt.edu))

This GitHub edition is an independent rewrite with modern plumbing (SQLite + JSON + engine/server split). Shout-out to the original team—your work inspired this revival.
