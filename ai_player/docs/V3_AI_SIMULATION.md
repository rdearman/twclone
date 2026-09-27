# AI v3 scenario simulation

Run the repeatable offline planner harness from the repository root:

```sh
python3 ai_player/simulation.py --runs 10 --seed 42 \
  --output /tmp/twclone-ai-v3-simulation.json
```

It runs every JSON fixture in `ai_player/scenarios/` for each requested
behavior profile and emits both per-episode rows and a profile summary. The
harness calls the production planner's market and navigation selectors. Its
small world model applies quoted prices, graph paths, ship holds, credits, and
seeded shield hazards. It is an offline policy regression tool, not a complete
server or engine simulation.

Metrics include realized profit, purchase/sale counts, the share of sales that
made a profit, survival, shield damage, planned cargo-route hops completed,
failed local actions, and decision quality (profitable sales divided by all
sales). Combat success is `null`: the bot intentionally does not initiate
combat autonomously, and random special encounters still lack a server-provided
encounter/action interface.

Supported navigation profiles are `balanced`, `trader`/`merchant`,
`explorer`/`scout`, `cautious`/`survivor`, and `adaptive`. Adaptive behavior
switches to cautious navigation below `adaptive_caution_threshold` shield
fraction (default 0.4), prioritizes known-port travel while carrying cargo,
and explores when neither condition applies. Set `behavior_profile` in the AI
configuration to select a profile; the default remains `balanced`.

## Current deterministic baseline

Command: `--runs 10 --seed 42` across `market_loop` and `risky_frontier`, or
100 episodes in total. This is a smoke baseline for scenario variety rather
than a balance claim.

| Profile | Mean profit | Survival | Profitable sales | Cargo route completion | Failed actions |
| --- | ---: | ---: | ---: | ---: | ---: |
| balanced | 875 CR | 50% | 100% | 100% | 0 |
| trader | 965 CR | 60% | 100% | 100% | 0 |
| explorer | 875 CR | 50% | 100% | 100% | 0 |
| cautious | 1,410 CR | 100% | 100% | 100% | 0 |
| adaptive | 983 CR | 70% | 100% | 100% | 0 |

The fixture intentionally makes exploration expose ships to variable shield
damage. Cautious navigation survived more often in this small sample; the
result should not be generalized to the live game until the harness models
server turns, economy updates, and combat encounters.

## Adding scenarios

Add a JSON file to `ai_player/scenarios/` with a unique `name`, `start_sector`,
`adjacency`, `ports`, and optional starting resources, explored sectors,
hazards, and turn limit. Hazard values may be fixed numbers or `[min, max]`
integer ranges. Keep scenarios compact and deterministic so a failing episode
can be reproduced with its seed.

## V3 work remaining

- Colony/planet planning can consume existing planet commands only after the
  server command support and response state are confirmed as available in a
  deployed server. The current offline harness does not model production ticks
  or entity stock.
- Combat success measurement needs combat/encounter scenarios and a safe,
  explicit decision interface. Do not infer success from ordinary movement or
  shield loss.
- Issue #439 still depends on engine/server encounter generation and a
  player-facing encounter action interface. Event subscriptions still need the
  server-owned schema/handler alignment documented in `V2_AI_AUDIT.md`.
- GitHub issue pages were unavailable during this pass, so this document does
  not claim a refreshed v3 AI issue inventory.
