# Trade Wars Web Client

A modern Angular web interface prototype for TWClone.

## Components

1. **Web** (Angular): The browser UI and frontend assets in `web/`.
2. **Gateway** (separate backend): The planned HTTPS account/login and API
   service lives at the repository root in `gateway/`, outside this frontend.

The Python service in `client/angular_client/gateway/` is a deprecated
WebSocket-to-TCP experiment, not the HTTPS gateway. Do not deploy it or add
new gateway features there. See
[the v3 gateway architecture decision](../../docs/V3_WEB_GATEWAY_ARCHITECTURE.md).

## Requirements
- Python 3.10+
- Node.js 20+
- Angular CLI 17+

## Quick Start

### Start the Web Client

```bash
cd web
npm install
npm start
```
Open [http://localhost:4200](http://localhost:4200) in your browser.

## Features
- **Data-Driven Menus**: Renders `menus.json` with hotkey support.
- **Dynamic HUD**: Automatically updates sector view when moving or scanning.
- **RPC Inspector**: Manually test any server command.
- **Event Feed**: Real-time broadcast display (prototype transport).
- **Auto-Resolution**: Handles `<ctx:...>` and `<prompt:...>` placeholders.
