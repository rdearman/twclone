# Bounded AI QA cohorts

`fleet.py` runs the existing `main.py` controller once per already-provisioned
ordinary player. Each source file is a normal `ai_player/config.json`-shaped
configuration. The runner does not register accounts, equip ships, connect to a
database, or grant privileges. Prepare one config per authorised player with a
distinct username and credential; the runner rejects duplicates.

Example for a disposable isolated test server:

```sh
python3 ai_player/fleet.py \
  --config-dir /secure/qa-player-configs \
  --count 20 \
  --max-concurrent 5 \
  --duration-seconds 1800 \
  --run-root /tmp/twclone-ai-qa
```

The selected configs are copied into a new private run directory. Each player
gets its own `config.json`, state file, log, QA report directory and process
group. The runner caps a cohort at 50 players and defaults to five concurrent
sessions and a 30 minute activity window. `summary.json` contains identity,
exit status and existing numeric AI metrics; it never includes credentials or
session tokens.

Press Ctrl-C for graceful shutdown. The runner sends SIGINT only to process
groups it created, allowing `main.py` to disconnect and persist state. To resume
the same cohort and its saved state:

```sh
python3 ai_player/fleet.py --resume-run /tmp/twclone-ai-qa/fleet-YYYYMMDD-HHMMSS-XXXXXXXX
```

The usual controller performs login again on startup and loads that player's
persisted planner, exploration and bandit state. Use this only against a
disposable QA universe; this tool does not establish that the target endpoint
is isolated.

`spawn.py` remains a legacy provisioning utility. It performs direct database
equipment changes after public registration and is not used by this runner.
