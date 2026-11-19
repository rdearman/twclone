# twclone

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
