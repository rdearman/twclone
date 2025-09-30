
# twclone

A modern, C-based recreation of classic BBS-era space-trading gameplay (in the spirit of TradeWars 2002). **twclone** provides a headless server, a terminal client, and a deterministic “Big Bang” universe generator—now backed by **SQLite** with a **JSON** protocol that makes writing clients (or AI bots) straightforward.

> **What’s new (2025):**
>
> * Full **SQLite** data model (no flat files)
> * **JSON** protocol for client & bot compatibility
> * A separate **Game Engine** process (forked) that runs clocks, economy, maintenance, NPC stubs, and enforcement via **durable DB rails** and a **TCP S2S** control channel
> * **DB-backed configuration** & secrets with live reload
> * Cleaner broadcast pipeline to players

If you’re here from SourceForge: welcome back! The original code (largely GPL-era) is still available there; this repo is a ground-up rewrite focused on DB storage and JSON I/O. Portions I authored are now under **MIT**. Big thanks to the original collaborators; see **Credits** at the end.

---

## Table of contents

* [Project layout](#project-layout)
* [Quick start](#quick-start)
* [Build from source](#build-from-source)
* [Running the server](#running-the-server)
* [Running the client](#running-the-client)
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

# 2) Create a fresh universe (SQLite DB will be created/seeded on first run)
rm -f twclone.db

# 3) Start the server
./bin/server --host 0.0.0.0 --port 1234

# 4) (Optional) Start the engine if not auto-forked by the server in your build
# See docs/ENGINE.md for lifecycle; some builds fork the engine automatically.

# 5) Connect with the client (renders from menus.json)
./bin/client --host localhost --port 1234 --menus ./data/menus.json
```

> Tip: Deleting `twclone.db` resets the universe. Some older builds used `twconfig.db`; remove whichever DB file your build created.

---

## Build from source

**Prereqs:** GCC/Clang, GNU make, SQLite3 (lib & CLI), POSIX (Linux/WSL/macOS).
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
server: starting…
server: listening on 0.0.0.0:1234
```

---

## Running the client

A simple terminal client is included for testing. You can also write your own in any language that speaks JSON.

```bash
./bin/client --host localhost --port 1234 --menus ./data/menus.json
```

If you place `menus.json` at `./data/menus.json`, you can usually just run `./bin/client`.

---

## Universe generation (“Big Bang”)

Create/seed a fresh universe:

```bash
./bin/test_bang
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

All configuration lives in the database:

* `config` (typed key/values by scope), `config_version` (live reload), `config_audit` (history), and `s2s_keys` (HMAC secrets).
* The server/engine load config at startup, validate types/ranges, and can **live-reload** after a version bump.

Initial seeds are created on first run; see **ENGINE.md** for the exact table definitions and reload messages.

---

## Protocol

All client↔server interactions use JSON. The engine↔server (S2S) control channel also uses JSON over TCP with a small envelope.

* **Client protocol:** request/response envelopes, errors, deprecations.
* **Engine↔Server (S2S):** `s2s.health.check`, `s2s.broadcast.sweep`, `s2s.command.push`, `s2s.config.bump`, `s2s.engine.shutdown`.

🔗 See **[`docs/PROTOCOL.md`](./docs/PROTOCOL.md)** for structures and examples.
🔗 Engine S2S specifics are also summarized in **[`docs/ENGINE.md`](./docs/ENGINE.md)**.

---

## Database

* **Engine:** SQLite single-file DB (`./twclone.db` by default).
* **Schema:** created/verified at first run (or by `test_bang`).
* **Reset:** delete `twclone.db` and re-run `test_bang` (or start the server to re-seed essentials).

Handy CLI:

```bash
sqlite3 twclone.db ".schema"
sqlite3 twclone.db "SELECT * FROM sectors LIMIT 10;"
```

---

## Security

**Now:** LAN-friendly for development.
**Target hardening:**

* Store **password hashes** (e.g., Argon2id) instead of plaintext.
* Gate brute force with rate limits/lockouts.
* Add transport encryption (TLS terminator or built-in TLS).
* Keep **HMAC keys** for S2S in `s2s_keys` (DB), never in logs.

---

## Roadmap

* [ ] Finish parity with legacy client features.
* [ ] Economy loops (ports, stock/price updates), Terra/planet growth.
* [ ] NPC scaffold (Ferrengi/Imperials) + encounter hooks.
* [ ] Imperial enforcement (warn/dispatch/destroy) golden path end-to-end.
* [ ] Broadcast pump & ephemeral TTLs.
* [ ] Robust auth (Argon2id, TLS), admin/sysop ops.
* [ ] Tests (idempotency, crash-resume, load/priority).
* [ ] CI build & lint.

For deep technical detail and task breakdowns, see **ENGINE.md** and GitHub Issues/Epics (Engine Process & IPC, Durable Rails, Scheduler, Broadcasts, NPC/Enforcement, Reliability & Ops).

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

* Historic SourceForge material contained GPL’d portions; this rewrite replaces those systems with a DB-backed, JSON-speaking implementation. Newly authored code in this repo is under **MIT**.

---

## Credits

Huge thanks to the original contributors and community that kept the TW flame alive.

* Website (historical): `http://twclone.sourceforge.net`
* © 2000 Jason C. Garcowski ([jcg5@po.cwru.edu](mailto:jcg5@po.cwru.edu))
* © 2002 Ryan Glasnapp ([rglasnap@nmt.edu](mailto:rglasnap@nmt.edu))

This GitHub edition is an independent rewrite with modern plumbing (SQLite + JSON + engine/server split). Shout-out to the original team—your work inspired this revival.

