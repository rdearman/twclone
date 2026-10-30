# Godot client server contract handoff

The Godot command menu disables the actions below because the server request schemas and handlers are incompatible, or because the handler returns placeholder data. CODEX 1 should verify these against the current server worktree before changing either contract. Re-enable a client action only after a server-backed positive and refusal test proves the agreed payload and response.

| Action | Client-observed blocker | References |
| --- | --- | --- |
| `ship.rename` | Supported with `{ "name": "New name" }`; the server renames the caller's active ship and validates ownership and length. | `CommandMenu.gd` Rename ship; `src/schemas.c` `schema_ship_rename`; `src/server_ships.c` `cmd_ship_rename` |
| `chat.send` private message | Schema rejects the recipient fields needed by the handler. | `CommandMenu.gd` Private message; `src/schemas.c` `schema_chat_send`; `src/server_communication.c` `cmd_chat_send` |
| `mail.send` | Schema requires `to_player_name`; handler reads `to` or `to_id`. | `CommandMenu.gd` Send mail; `src/schemas.c` `schema_mail_send`; `src/server_communication.c` `cmd_mail_send` |
| `mail.delete` | Schema requires `mail_id`; handler reads an `ids` array. | `CommandMenu.gd` Delete mail; `src/schemas.c` `schema_mail_delete`; `src/server_communication.c` `cmd_mail_delete` |
| `player.rankings` | Handler returns an empty array even when players exist. | `CommandMenu.gd` Player rankings; `src/server_players.c` `cmd_player_rankings` |
| `insurance.policies.list`, `insurance.policies.buy`, `insurance.claim.file` | Responses are hard-coded placeholders; no authoritative policy or claim lifecycle is exposed. | `CommandMenu.gd` Insurance actions; `src/server_loop.c` insurance handlers |

The menu messages document the contract seen by the client; they do not establish that all of these features are required by a particular TW2002 edition. Server changes may be in progress in the shared worktree, so these observations must be rechecked before implementation.
