# 27. NPC & Ferengi AI

## 1. NPC Commands (Engine Enqueued)

These are primarily Engine-driven commands executed by the Server.

*   **`npc.spawn.v1`**: Create a new NPC.
*   **`npc.move.v1`**: Move an NPC.
*   **`npc.attack.v1`**: Initiate NPC attack.
*   **`npc.deploy_fighters.v1`**: Drop defenses.
*   **`npc.destroy.v1`**: Event when NPC is killed.

## 2. Behaviors

*   **Imperial**: Enforcement.
*   **Ferengi**: Trade and theft.
*   **Scaffold**: Basic patrol/waypoint logic.

## 3. Police & Legal

*   **`player.illegal_act.v1`**: Event when player breaks law.
*   **`police.bribe` / `surrender`**: RPCs for interacting with law enforcement.
