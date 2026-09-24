import asyncio
import websockets
import json

async def test():
    uri = "ws://localhost:8081/ws"
    async with websockets.connect(uri) as websocket:
        # Test Connect
        await websocket.send(json.dumps({"type": "connect", "host": "127.0.0.1", "port": 7777}))
        resp = await websocket.recv()
        print(f"Connect Resp: {resp}")
        
        # Test RPC
        await websocket.send(json.dumps({
            "type": "rpc",
            "id": "12345",
            "command": "test.echo",
            "data": {}
        }))
        resp = await websocket.recv()
        print(f"RPC Resp: {resp}")

if __name__ == "__main__":
    asyncio.run(test())
