---
I've moved this project from sourceforge to github. Nobody had done anything with it since 2002, so I decided to pick back up where I left off and try to get it going.

I was working with two other developers who did most of the other work outside of the bigbang(). I've rewritten basically everything in order to support DB storage of data rather than the old text file system. The old system was mostly GNU license except the bits I wrote which were MIT. I've replaced all the old code now, and I've changed the license for all the stuff I've written to MIT. But I still wanted to give a shout out to the guys I used to work on this with. You can get the original code from sourceforge.

The changes I've made which required an entire rewrite was to put all the data into an SQLite DB file, and to change the protocol to JSON. The benefit of this is that I don't really need to write a client since a client could be written in any language which supports JSON. I've developed a test client in python which will be extended to a full client after I've implemented all the systems on the server. Because of this the code is 100% focused on building the server. THe reason for moving to JSON was that I wanted to train an AI to play TW2002, but after a lot of messing around decided to come back to this project and roll my own server. 

Any AI player will not need a client and can just parse and send JSON, so while I need a client for testing, I'm not focused on it. 

---

# twclone

A modern, C-based recreation of classic BBS-era space-trading gameplay (in the spirit of TradeWars 2002). **twclone** provides a headless server, a terminal client, and a deterministic “Big Bang” universe generator, backed by SQLite.

> Sectors `1–10` are reserved **FedSpace**. Universe content is generated reproducibly per-game, and you can wipe the SQLite DB to start a fresh universe at any time.

---

## Table of contents

* [Project layout](#project-layout)
* [Quick start](#quick-start)
* [Build from source](#build-from-source)
* [Running the server](#running-the-server)
* [Running the client](#running-the-client)
* [Universe generation (“Big Bang”)](#universe-generation-big-bang)
* [Gameplay snapshot](#gameplay-snapshot)
* [Configuration & defaults](#configuration--defaults)
* [Menus JSON](#menus-json)
* [Database](#database)
* [Protocol](#protocol)
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
├─ src/                 # C sources (server_loop.c, globals.c, …)
├─ data/                # menus.json and other runtime data
├─ docs/                # PROTOCOL.md, SYSOP.md, design notes
├─ Makefile.am / etc.   # Autotools build files
└─ README.md            # This file
```

---

## Quick start

```bash
# 1) Build
make clean && make -j

# 2) Create a fresh universe (SQLite DB will be created/seeded)
delete the twconfig.db file and the server will create a new universe on startup. 

# 3) Start the server
./bin/server --host 0.0.0.0 --port 1234

# 4) Connect with the client (uses menus.json)
./bin/client --host localhost --port 1234 --menus ./data/menus.json
```

> Tip: Delete the DB file (typically `twconfig.db` in the working directory) to reset the universe.

---

## Build from source

### Prerequisites

* GCC or Clang
* GNU make & autotools (if regenerating build files)
* SQLite3 (dev headers and CLI recommended)
* POSIX environment (Linux / WSL / macOS)

### Build

```bash
make clean && make -j
# Artefacts land in ./bin
```

If you prefer explicit compile logs:

```bash
make V=1
```

---

## Running the server

```bash
./bin/server --host 0.0.0.0 --port 1234
```

**Flags**

* `--host <addr>`: Bind address (default `0.0.0.0`)
* `--port <port>`: Listen port (default `1234`)

Log output example:

```
Server loop starting...
Listening on 0.0.0.0:1234
```

---

## Running the client
Look for a python test client in the directory. Or write your own. 

```bash
./bin/client \
  --host localhost \
  --port 1234 \
  --menus ./data/menus.json
```

**Flags**

* `--host <addr>`: Server host (default `localhost`)
* `--port <port>`: Server port (default `1234`)
* `--menus <file>`: Menus definition JSON (default `./data/menus.json`)

To make those the *project defaults*, you can simply run:

```bash
./bin/client
```

…and ensure `menus.json` is present at `./data/menus.json`.
(If you want hard defaults compiled in, set the fallback values in the client’s argument-parsing code to `localhost`, `1234`, and `./data/menus.json` respectively.)

---

## Universe generation (“Big Bang”)

The **Big Bang** utility creates a new universe and seeds gameplay entities.

```bash
./bin/test_bang
```

Typical log:

```
BIGBANG: Creating universe...
BIGBANG: Creating sectors...
BIGBANG: Creating 500 sectors (1–10 reserved for Fedspace).
BIGBANG: Creating random warps...
BIGBANG: Ensuring FedSpace Exits...
BIGBANG: Creating complex warps...
BIGBANG: Creating 250 one-way warps...
BIGBANG: Creating 125 dead-end warps...
BIGBANG: Ensuring sector exits...
```

### Key generation rules

* **FedSpace**: sectors `1–10` are protected; content generators skip/avoid overwriting them.
* **Warps graph**: mixture of bidirectional links, one-ways, and purposeful dead-ends.
* **“Tunnels”**: short chains of unique sectors (length 4–8 by default) providing defensible corridors; typically 15 attempts with back-off to avoid duplicates.
* **Derelicts & NPCs** (WIP): scattered across sectors `11–500`. Ferrengi homeworld and traders are seeded together.

---

## Gameplay snapshot

Example sector view from the client:

```
You are in sector 238 — Alpha Lyr.
Adjacent sectors: 263
Ports here: none
Ships here: Bit Banger
Planets: none
Beacon: none

--- Main Menu (Sector 238) ---
 (M) Move to a Sector        | (D) Re-display Sector
 (P) Port & Trade            | (L) Land on a Planet
 (C) Ship's Computer         | (V) View Game Status
 (R) Release Beacon          |
 (Y) Testing Menu            | (H) Help
 (Q) Quit                    |
```

Beacons, ports, trading, and ship/sector queries flow over the JSON protocol described in `docs/PROTOCOL.md`.

---

## Configuration & defaults

* **Server defaults**: `--host 0.0.0.0`, `--port 1234`
* **Client defaults**: `--host localhost`, `--port 1234`, `--menus ./data/menus.json`
* **Resetting the game**: remove the SQLite DB file and run `./bin/test_bang` again.

To make the client flags *effectively default* without typing them:

* Keep `./data/menus.json` in place, and
* Launch `./bin/client` with no flags.

If you want *compiled-in* defaults, set them in the client’s arg-parser (e.g., fallback strings in `argv` handling).

---

## Menus JSON

The client renders menus from a JSON definition. A minimal example:

```json
{
  "main": {
    "title": "Main Menu",
    "items": [
      { "key": "M", "label": "Move to a Sector", "action": "move.prompt" },
      { "key": "D", "label": "Re-display Sector", "action": "sector.info" },
      { "key": "P", "label": "Port & Trade", "action": "port.menu" },
      { "key": "C", "label": "Ship's Computer", "action": "ship.status" },
      { "key": "V", "label": "View Game Status", "action": "game.status" },
      { "key": "R", "label": "Release Beacon", "action": "beacon.set" },
      { "key": "H", "label": "Help", "action": "help.show" },
      { "key": "Q", "label": "Quit", "action": "client.quit" }
    ]
  }
}
```

> Note: `ship.status` is the unified command name; `ship.info` is supported as an alias for backwards compatibility.

---

## Database

* **Engine**: SQLite (single file in the working directory).
* **Schema**: created on first run; `./bin/test_bang` ensures all tables and defaults exist.
* **Reset**: delete the DB file; rerun `./bin/test_bang`.

Handy CLI:

```bash
sqlite3 twclone.db ".schema"
sqlite3 twclone.db "SELECT * FROM sectors LIMIT 10;"
```

---

## Protocol

All client–server interactions use a simple JSON protocol. See:

* `docs/PROTOCOL.md` — message envelope, IDs, types, common errors
* `docs/SYSOP.md` — operational notes and server admin guidelines

Examples (request/response pairs), error codes (e.g., `1401 Player does not have a beacon on their ship.`), and deprecations (e.g., `ship.info → ship.status`) are documented there.

---

## Security

* **Current**: usernames/passwords may be transmitted in plaintext on LAN for dev convenience.
* **Target**: move to **hashed passwords** in DB (e.g., Argon2id) and **TLS** transport (e.g., `stunnel` or built-in TLS).
* **Action items** (server):

  * Store `password_hash`, not plaintext.
  * Add rate limiting and lockout on failed logins.
  * Add transport encryption or proxy behind a TLS terminator.

---

## Roadmap

* [ ] Finish parity with Version 2 client features (see project thread & v2 audit).
* [ ] Ports/trade loops and economy balancing.
* [ ] NPCs: Ferrengi homeworld + traders seeded together; Imperial warship behaviour.
* [ ] Derelict ships for each shiptype (seeded in 11–500).
* [ ] Robust beacon inventory checks and messages.
* [ ] Save-safe command aliases (`ship.status` everywhere; `ship.info` deprecated).
* [ ] Proper auth: Argon2id password hashing, salt & pepper, TLS.
* [ ] Admin/sysop commands for resets, seeds, and diagnostics.
* [ ] Unit tests for generators (warps, tunnels, one-ways, dead-ends).
* [ ] CI: build + lint on pushes.

---

## Contributing

1. **Discuss** the change in an issue or the project thread (especially protocol changes).
2. **Keep functionality intact** unless the change is explicitly scoped (Rick’s rule).
3. Code style: readable C, clear error handling, comments in `/* ... */` style.
4. Run:

   ```bash
   make clean && make -j
   ./bin/test_bang
   ./bin/server --host 0.0.0.0 --port 1234
   ./bin/client --host localhost --port 1234 --menus ./data/menus.json
   ```
5. Include tests or a reproducible scenario where sensible.

---

## Licence

MIT 

---

### Credits

Inspired by BBS classics. Built with love for terminal games and systems thinking.

Website: http://twclone.sourceforge.net
Copyright (C) 2000 Jason C. Garcowski(jcg5@po.cwru.edu),
Copyright (C) 2002 Ryan Glasnapp(rglasnap@nmt.edu)


