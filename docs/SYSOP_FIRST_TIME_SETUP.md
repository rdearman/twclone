# Local development setup

This guide points to the current build and client instructions; it is not a
production deployment runbook.

## Before starting

- Use an isolated development database and test accounts.
- Keep secrets out of source files, command history and reports.
- The supported full-game database runtime is PostgreSQL.
- Do not connect acceptance tests to beta, production or an unverified host DB.

## Build and run

Use the repository Makefile and `compose.beta.yaml` as the current source for
build targets, container services, ports and environment settings. Do not
copy old commands from historical setup reports: server startup and database
configuration have changed over the life of the project.

For player workflows and client options, see
[`../client/python_client/README.md`](../client/python_client/README.md) and
the Godot project README. For repeatable integration acceptance, see
[`testing/V3_INTEGRATION_QA.md`](testing/V3_INTEGRATION_QA.md).

## Deployment boundary

Local setup does not authorize external exposure. The release-candidate audit
currently blocks external beta deployment. Follow
[`V3_DEPLOYMENT_HARDENING_PLAN.md`](V3_DEPLOYMENT_HARDENING_PLAN.md) with the
environment owner before changing credentials, firewall rules, services,
accounts or deployment state.
