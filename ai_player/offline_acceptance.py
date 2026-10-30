"""Offline deterministic acceptance fixture for issue #490.

This module drives the ordinary response-processing path with an in-memory
protocol peer. It never opens a socket, reads credentials, or calls Ollama.
Its results are fixture evidence only and must not be reported as live
acceptance.
"""

from __future__ import annotations

import json
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

from main import process_responses
from state_manager import StateManager


@dataclass
class OfflinePeer:
    """Minimal public-protocol peer that records outbound envelopes."""

    sent: list[dict[str, Any]] = field(default_factory=list)

    def send_command(self, command: dict[str, Any]) -> bool:
        self.sent.append(json.loads(json.dumps(command)))
        return True


class _Reporter:
    def report_state_inconsistency(self, *_args: Any) -> None:
        raise AssertionError("offline acceptance produced a state inconsistency")


class _Bandit:
    def give_feedback(self, *_args: Any) -> None:
        return None


@dataclass
class OfflinePlayerResult:
    identity: str
    completed: bool
    credits: str | None
    cargo: list[dict[str, Any]]
    sector: int | None
    receipts: list[str]
    sent_commands: list[str]
    failure: str | None = None


def _process(peer: OfflinePeer, manager: StateManager, request: dict[str, Any],
             response_type: str, data: dict[str, Any], *, status: str = "ok") -> None:
    peer.sent.append(json.loads(json.dumps(request)))
    manager.add_pending_command(request["id"], request)
    process_responses([{
        "reply_to": request["id"], "status": status,
        "type": response_type, "data": data,
    }], peer, manager, _Reporter(), _Bandit(), {}, None)


def _readbacks(peer: OfflinePeer, manager: StateManager, identity: str,
               fallback_player: dict[str, Any], fallback_ship: dict[str, Any]) -> None:
    # A refusal may trigger automatic authoritative refresh commands. Drain
    # those first so the fixture models safe recovery rather than restoring
    # local state by hand.
    for request_id, pending in list((manager.get("pending_commands") or {}).items()):
        command = pending.get("command")
        if command == "player.my_info":
            _process(peer, manager, pending, "player.my_info", {"player": fallback_player})
        elif command == "ship.info":
            _process(peer, manager, pending, "ship.info", fallback_ship)
    current_player = manager.get("player_info") or {"player": fallback_player}
    current_ship = fallback_ship
    reads = [
        ({"id": f"{identity}-player-read", "command": "player.my_info", "data": {}, "meta": {}},
         "player.my_info", current_player),
        ({"id": f"{identity}-ship-read", "command": "ship.info", "data": {}, "meta": {}},
         "ship.info", current_ship),
    ]
    for request, response_type, data in reads:
        _process(peer, manager, request, response_type, data)


def run_offline_acceptance(root: Path, player_count: int = 5) -> list[OfflinePlayerResult]:
    """Run an isolated five-player buy → warp → sell protocol fixture."""
    if player_count != 5:
        raise ValueError("offline acceptance requires exactly five players")
    root.mkdir(parents=True, exist_ok=False)
    results: list[OfflinePlayerResult] = []
    for number in range(1, player_count + 1):
        identity = f"offline_ai_{number:02d}"
        player_root = root / identity
        player_root.mkdir()
        manager = StateManager(str(player_root / "state.json"), {
            "strategy_mode": "deterministic",
        })
        peer = OfflinePeer()
        manager.set("player_info", {"player": {"credits": "1000.00", "turns_remaining": 20}})
        manager.set("ship_info", {"id": number, "holds": 10, "cargo": [],
                                   "location": {"sector_id": 1}})
        manager.set("player_location_sector", 1)
        initial_player = {"credits": "1000.00", "turns_remaining": 20}
        initial_ship = {"id": number, "holds": 10, "cargo": [],
                        "location": {"sector_id": 1}}
        receipts: list[str] = []
        failure: str | None = None

        buy = {"id": f"{identity}-buy", "command": "trade.buy",
               "data": {"port_id": 101, "items": [{"commodity": "ORE", "quantity": 1}]},
               "meta": {"idempotency_key": f"{identity}-buy-key"}}
        _process(peer, manager, buy, "trade.buy_receipt_v1", {
            "port_id": 101, "lines": [{"commodity": "ORE", "quantity": 1,
                                          "unit_price": 100, "value": "100.00"}],
            "fees": "0.00", "credits_remaining": "900.00",
        })
        receipts.append("trade.buy_receipt_v1")
        fallback_player = dict(manager.get("player_info")["player"])
        fallback_ship = json.loads(json.dumps(manager.get("ship_info")))

        warp = {"id": f"{identity}-warp", "command": "move.warp",
                "data": {"to_sector_id": 2}, "meta": {}}
        if number == 4:
            failure = "simulated warp refusal"
            _process(peer, manager, warp, "error", {"code": 1453, "message": "No link"}, status="refused")
        else:
            _process(peer, manager, warp, "move.result", {"to_sector_id": 2})
            # process_responses requests a fresh sector read after movement.
            for generated in list(manager.get("pending_commands") or {}):
                pending = manager.get_pending_command(generated)
                if pending and pending.get("command") == "sector.info":
                    _process(peer, manager, pending, "sector.info", {"sector_id": 2})

        if failure is None:
            sell = {"id": f"{identity}-sell", "command": "trade.sell",
                    "data": {"port_id": 202, "items": [{"commodity": "ORE", "quantity": 1}]},
                    "meta": {"idempotency_key": f"{identity}-sell-key"}}
            if number == 3:
                # Simulate an interrupted request: persist the idempotency
                # marker, remove the pending entry, then reconcile its late
                # receipt without issuing a second sell.
                manager.set("uncertain_trade", {"request_id": sell["id"], "command": "trade.sell"})
                peer.sent.append(dict(sell))
                process_responses([{
                    "reply_to": sell["id"], "status": "ok", "type": "trade.sell_receipt_v1",
                    "data": {
                        "port_id": 202, "lines": [{"commodity": "ORE", "quantity": 1,
                                                      "unit_price": 113, "value": "113.00"}],
                        "fees": "0.00", "credits_remaining": "1013.00",
                    },
                }], peer, manager, _Reporter(), _Bandit(), {}, None)
            else:
                _process(peer, manager, sell, "trade.sell_receipt_v1", {
                "port_id": 202, "lines": [{"commodity": "ORE", "quantity": 1,
                                              "unit_price": 113, "value": "113.00"}],
                "fees": "0.00", "credits_remaining": "1013.00",
                })
            receipts.append("trade.sell_receipt_v1")

        fallback_ship["location"] = {"sector_id": 2 if failure is None else 1}
        if failure is None:
            fallback_ship["cargo"] = []

        _readbacks(peer, manager, identity, fallback_player, fallback_ship)
        player = manager.get("player_info") or {}
        player_data = player.get("player") if isinstance(player, dict) else {}
        ship = manager.get("ship_info") or {}
        cargo = ship.get("cargo") if isinstance(ship, dict) else []
        sent_names = [command.get("command", "") for command in peer.sent]
        results.append(OfflinePlayerResult(
            identity=identity,
            completed=(failure is None and not cargo and player_data.get("credits") == "1013.00"
                       and manager.get("player_location_sector") == 2),
            credits=player_data.get("credits"), cargo=cargo,
            sector=manager.get("player_location_sector"), receipts=receipts,
            sent_commands=sent_names, failure=failure,
        ))
    return results
