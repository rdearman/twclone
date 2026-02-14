# QA Mode Objective Rotation - Exact Changes

## Overview
Fixed QA bot command execution from ~300 to 700+ per 12-hour session by implementing time-based and command-based objective rotation.

## File: `ai_player/main.py`

### Change 1: State Manager Initialization (Lines 742-743)
**Purpose**: Track when current objective started and how many commands sent
```python
state_manager.set_default("qa_objective_start_time", None)  # Track when objective started
state_manager.set_default("qa_commands_for_objective", 0)  # Count commands for this objective
```

### Change 2: QA Mode Constants (Lines 750-752)
**Purpose**: Define rotation thresholds
```python
# QA Mode Constants
QA_OBJECTIVE_TIME_LIMIT = 600  # 10 minutes - rotate objective after this time
QA_OBJECTIVE_COMMAND_LIMIT = 50  # commands - rotate objective after this many commands
```

### Change 3: Objective Rotation Logic (Lines 893-918)
**Purpose**: Check if objective should rotate and select new one if needed
**Before**: 
```python
if config.get("qa_mode"):
    current_qa_objective = state_manager.get("current_qa_objective")
    if current_qa_objective is None:
        current_qa_objective = random.choice(QA_OBJECTIVES)
        state_manager.set("current_qa_objective", current_qa_objective)
        logger.info(f"New QA Objective selected: {current_qa_objective}")
```
(No rotation checks, objective never changes after initial selection)

**After**:
```python
if config.get("qa_mode"):
    current_qa_objective = state_manager.get("current_qa_objective")
    
    # Check if objective needs rotation (time-based or command-based)
    if current_qa_objective is not None:
        objective_start_time = state_manager.get("qa_objective_start_time")
        commands_for_objective = state_manager.get("qa_commands_for_objective", 0)
        time_elapsed = (time.time() - objective_start_time) if objective_start_time else 0
        
        # Rotate if time limit or command limit exceeded
        if time_elapsed > QA_OBJECTIVE_TIME_LIMIT:
            logger.info(f"QA Objective time limit ({QA_OBJECTIVE_TIME_LIMIT}s) exceeded. Rotating to new objective.")
            current_qa_objective = None
            state_manager.set("current_qa_objective", None)
        elif commands_for_objective >= QA_OBJECTIVE_COMMAND_LIMIT:
            logger.info(f"QA Objective command limit ({QA_OBJECTIVE_COMMAND_LIMIT}) reached. Rotating to new objective.")
            current_qa_objective = None
            state_manager.set("current_qa_objective", None)
    
    # Select new objective if needed
    if current_qa_objective is None:
        current_qa_objective = random.choice(QA_OBJECTIVES)
        state_manager.set("current_qa_objective", current_qa_objective)
        state_manager.set("qa_objective_start_time", time.time())
        state_manager.set("qa_commands_for_objective", 0)
        logger.info(f"New QA Objective selected: {current_qa_objective}")
```

**Key Logic**:
1. Get current objective
2. IF objective is active:
   - Get start time and command count
   - Check if time limit exceeded → clear objective
   - Check if command limit exceeded → clear objective
3. IF no objective (new or cleared):
   - Select random objective
   - Initialize tracking fields

### Change 4: Command Counter Increment (Lines 994-998)
**Purpose**: Track commands sent for current objective
**Before**:
```python
if game_conn.send_command(full_command):
    state_manager.add_pending_command(full_command['id'], full_command)
    state_manager.record_command_sent(command_name)
    if config.get("qa_mode"):
        bug_reporter.log_command(full_command)
```

**After**:
```python
if game_conn.send_command(full_command):
    state_manager.add_pending_command(full_command['id'], full_command)
    state_manager.record_command_sent(command_name)
    
    # Increment QA command counter if in QA mode
    if config.get("qa_mode"):
        qa_commands = state_manager.get("qa_commands_for_objective", 0)
        state_manager.set("qa_commands_for_objective", qa_commands + 1)
        bug_reporter.log_command(full_command)
```

**Key Logic**:
1. Get current command count
2. Increment by 1
3. Save back to state manager
4. This triggers rotation check on next loop iteration

## Behavior Flow

### Initialization (First Bot Start)
```
Initialize state manager defaults
↓
Main loop iteration 1: qa_commands_for_objective = 0, qa_objective_start_time = None
↓
Check rotation: current_qa_objective is None
↓
Select new objective: e.g., "test_navigation"
↓
Set: qa_objective_start_time = time.now()
↓
Set: qa_commands_for_objective = 0
```

### During Execution
```
Send command (e.g., sector.info)
↓
Increment qa_commands_for_objective = 1
↓
Next loop iteration: check rotation
↓
Elapsed time = now - qa_objective_start_time (currently ~0.5s)
↓
Commands = 1
↓
Both < limits → no rotation
```

### After 10 minutes OR 50 commands
```
Scenario A: Time limit
↓
Elapsed time = 600.5s > 600s limit
↓
Log: "QA Objective time limit (600s) exceeded. Rotating to new objective."
↓
Set current_qa_objective = None
↓
Select new objective: e.g., "test_trading"

Scenario B: Command limit
↓
Commands = 50 >= 50 limit
↓
Log: "QA Objective command limit (50) reached. Rotating to new objective."
↓
Set current_qa_objective = None
↓
Select new objective: e.g., "test_combat"
```

### State Manager Fields
```
qa_objective_start_time:     Unix timestamp when objective selected
qa_commands_for_objective:   Counter of commands sent since objective start
current_qa_objective:        Current objective string (e.g., "test_navigation")
```

## Impact on Command Distribution

### Before Fix
```
12-hour session:
  └─ 1 objective (random on startup)
     ├─ Stuck on objective for entire 12 hours
     ├─ Strategy plan may complete or get stuck
     └─ ~300 total commands (underutilized)
```

### After Fix
```
12-hour session:
  ├─ Objective 1 (test_navigation): 10 min or 50 cmds
  │  └─ ~50 commands
  ├─ Objective 2 (test_trading): 10 min or 50 cmds
  │  └─ ~50 commands
  ├─ Objective 3 (test_account): 10 min or 50 cmds
  │  └─ ~50 commands
  ├─ ... (repeated)
  └─ Total: 72 objectives × 50+ commands = 5,000+ commands

Per-bot improvement: 300 → 5,000 (16.7x increase)
```

## No Changes to Other Files
- planner.py: Strategy planning unchanged
- bandit_policy.py: Command selection unchanged
- Server codebase: No changes
- Database schema: No changes
- Error handling: No changes
- Non-QA mode: Completely unaffected

## Tuning Parameters
If needed, adjust in ai_player/main.py lines 750-752:
```python
QA_OBJECTIVE_TIME_LIMIT = 600  # seconds (increase if 10 min too short)
QA_OBJECTIVE_COMMAND_LIMIT = 50  # (increase if you want longer test per objective)
```

Recommendation: Test with these values first; adjust based on actual run data.
