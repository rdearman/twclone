# Deprecated WebSocket gateway prototype

This directory is a prototype from the original Angular client experiment.
It is **not** the HTTPS gateway and must not be used as the web login or
session service.

The prototype accepts a browser-selected TCP host and port, listens on
plaintext WebSocket, and forwards protocol requests (including session
tokens). It lacks the HTTPS gateway's required authentication boundary and
must only be used in a trusted local development environment, if at all.

The canonical backend location for the new web gateway is the repository-root
`gateway/` directory. The Angular application belongs in
`client/angular_client/web/`. The prototype source and its manual echo script
remain here temporarily for historical reference; do not add new gateway
features to this directory. See [the architecture decision](../../../docs/V3_WEB_GATEWAY_ARCHITECTURE.md).
