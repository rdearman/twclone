import asyncio
import json
import logging
import sys
import websockets
import os

logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s %(levelname)s %(name)s %(message)s'
)
logger = logging.getLogger("gateway")

# Defaults
DEFAULT_HOST = os.environ.get("TW_HOST", "127.0.0.1")
DEFAULT_PORT = int(os.environ.get("TW_PORT", "1234"))
LISTEN_PORT = int(os.environ.get("GATEWAY_PORT", "8081"))

async def tcp_reader(reader, websocket):
    """Reads NDJSON from TCP and forwards to WebSocket."""
    try:
        while True:
            line = await reader.readline()
            if not line:
                logger.info("TCP connection closed by server")
                break
            
            try:
                obj = json.loads(line)
                if "reply_to" in obj:
                    msg = {
                        "type": "rpc_result",
                        "id": obj["reply_to"],
                        "result": obj
                    }
                    if obj.get("status") in ("ok", "srv-ok", "success", None):
                         msg["ok"] = True
                    else:
                         msg["ok"] = False
                         msg["error"] = obj.get("error") or obj
                else:
                    msg = {
                        "type": "event",
                        "event": obj
                    }
                await websocket.send(json.dumps(msg))
            except json.JSONDecodeError:
                logger.warning(f"Invalid JSON from server: {line}")
            except Exception as e:
                logger.error(f"Error forwarding TCP->WS: {e}")
                
    except asyncio.CancelledError:
        pass
    except Exception as e:
        logger.error(f"TCP reader error: {e}")
    finally:
        await websocket.close()

async def ws_handler(websocket):
    logger.info(f"WS Client connected from {websocket.remote_address}")
    reader = None
    writer = None
    tcp_task = None
    
    try:
        async for message in websocket:
            try:
                data = json.loads(message)
                mtype = data.get("type")
                
                if mtype == "connect":
                    host = data.get("host") or DEFAULT_HOST
                    port = int(data.get("port") or DEFAULT_PORT)
                    
                    if writer:
                        logger.warning("Reconnecting TCP...")
                        writer.close()
                        if tcp_task: tcp_task.cancel()
                    
                    logger.info(f"Connecting to Game Server at {host}:{port}...")
                    try:
                        reader, writer = await asyncio.open_connection(host, port)
                        logger.info("Connected to Game Server")
                        tcp_task = asyncio.create_task(tcp_reader(reader, websocket))
                        await websocket.send(json.dumps({"type": "connected"}))
                    except Exception as e:
                        logger.error(f"Failed to connect to {host}:{port} - {e}")
                        await websocket.send(json.dumps({
                            "type": "rpc_result", 
                            "id": "init", 
                            "ok": False, 
                            "error": str(e)
                        }))
                
                elif mtype == "rpc":
                    if not writer:
                        await websocket.send(json.dumps({
                            "type": "rpc_result",
                            "id": data.get("id"),
                            "ok": False,
                            "error": "Not connected to server"
                        }))
                        continue
                    
                    server_req = {
                        "id": data.get("id"),
                        "command": data.get("command"),
                        "data": data.get("data", {})
                    }
                    if "auth" in data: server_req["auth"] = data["auth"]
                        
                    line = json.dumps(server_req, separators=( ",", ":")) + "\n"
                    writer.write(line.encode("utf-8"))
                    await writer.drain()
                    
                else:
                    logger.warning(f"Unknown message type: {mtype}")
                    
            except json.JSONDecodeError:
                logger.error("Failed to decode WS message")
                
    except websockets.exceptions.ConnectionClosed:
        logger.info("WS Connection closed")
    except Exception as e:
        logger.error(f"WS Handler error: {e}")
    finally:
        if tcp_task: tcp_task.cancel()
        if writer:
            writer.close()
            try:
                await asyncio.wait_for(writer.wait_closed(), timeout=2.0)
            except:
                pass
        logger.info("Cleanup complete")

async def main():
    logger.info(f"Gateway listening on ws://localhost:{LISTEN_PORT}/ws")
    async with websockets.serve(ws_handler, "0.0.0.0", LISTEN_PORT):
        await asyncio.Future()

if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        sys.exit(0)