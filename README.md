# twclone

twclone is a C implementation of a TradeWars-style multiplayer space game. The
current V3 server uses PostgreSQL. Python and Godot clients are included.

## Project status

The latest release-candidate audit records strong gameplay and client
acceptance for the tested disposable PostgreSQL environment, but does **not**
clear an external beta deployment. Deployment ownership, credential rotation,
network exposure and designated QA authentication remain release blockers.
See [the current status](docs/V3_STATUS.md) and
[release-candidate audit](docs/testing/V3_RELEASE_CANDIDATE_AUDIT_20261115.md).

## Documentation

Start at the [documentation index](docs/README.md). The authoritative player
and engine protocol is in [`docs/PROTOCOL.v3`](docs/PROTOCOL.v3/00_index.md);
approved gameplay rules are in
[`docs/V3_GAMEPLAY_DECISIONS.md`](docs/V3_GAMEPLAY_DECISIONS.md).

## Development

Use the repository's Makefile, Compose files and client-specific READMEs for
build and local development commands. Use only disposable databases for test
and acceptance work. Do not use local development instructions as deployment
authorization; deployment requirements are in
[`docs/V3_DEPLOYMENT_HARDENING_PLAN.md`](docs/V3_DEPLOYMENT_HARDENING_PLAN.md).

## License

See [`LICENSE`](LICENSE).
