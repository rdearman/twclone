# Trade Wars Web Client

A modern Angular web interface for TWClone, including a Python WebSocket gateway.

## Components

1. **Gateway** (Python): Proxies WebSocket <-> TCP NDJSON for the browser.
2. **Web** (Angular): The game client UI.

## Requirements
- Python 3.10+
- Node.js 20+
- Angular CLI 17+

## Quick Start

### 1. Start the Gateway

```bash
cd gateway
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
python gateway.py
```
The gateway runs on `ws://localhost:8081/ws`.

### 2. Start the Web Client

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
- **Event Feed**: Real-time broadcast display.
- **Auto-Resolution**: Handles `<ctx:...>` and `<prompt:...>` placeholders.
