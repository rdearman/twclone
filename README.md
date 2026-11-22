# twclone

## TWClone V3 Public Beta is Live — Download a Client and Play

**[Public Beta — Play Now](https://twclone.impara.co.uk)**

Visit the beta website to download the Godot graphical client or Python terminal client, register an account, and start playing on the public server.

TWClone is a multiplayer TradeWars-style space game. Explore a shared 2,500-sector universe, trade at ports, develop planets, join corporations, and fight other players. The beta provides 200 turns per player each day. This is a public beta, not a final production release; expect bugs and gameplay changes.

### Quick Start

1. Visit [twclone.impara.co.uk](https://twclone.impara.co.uk).
2. Download a client for a supported platform:
   - **Godot graphical client:** Linux x86_64, using Godot 4.5.2.
   - **Python terminal client:** Linux x86_64, using Python 3.9 or newer.
3. Unpack the download and launch the client as described on the beta website.
4. Connect to `game.twclone.impara.co.uk` on port `12483` with TLS certificate verification enabled.
5. Register a player account in the client, log in, and begin playing.

For bugs or feedback, use the in-game reporting option or [open an issue](https://github.com/rdearman/twclone/issues).

Current beta release: `2c5ca177aedf9468d275980390f31ee76e8a9313`.

twclone is a C implementation of a TradeWars-style multiplayer space game. The
V3 server uses PostgreSQL and supports Python and Godot clients.

## Project documentation

Start at the [documentation index](docs/README.md). The main references are:

- [Architecture](docs/ARCHITECTURE.md)
- [V3 gameplay specification](docs/V3_GAMEPLAY_SPEC.md)
- [V3 parity and acceptance ledger](docs/V3_PARITY.md)
- [Development and testing](docs/DEVELOPMENT.md)
- [Operations](docs/OPERATIONS.md)
- [V3 protocol specifications](docs/PROTOCOL.v3/00_index.md)

## Development

Use the repository Makefile, Compose files, and client-specific READMEs for
local development. Use only disposable databases for test and acceptance
work. Local development instructions do not authorize deployment; see the
[deployment hardening plan](docs/V3_DEPLOYMENT_HARDENING_PLAN.md).

## License

See [`LICENSE`](LICENSE).
