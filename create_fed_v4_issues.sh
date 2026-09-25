#!/usr/bin/env bash
set -euo pipefail

REPO="${REPO:-}"
DRY_RUN="${DRY_RUN:-0}"

if ! command -v gh >/dev/null 2>&1; then
  echo "ERROR: gh (GitHub CLI) not found." >&2
  exit 1
fi

if [[ -z "$REPO" ]]; then
  if gh repo view --json nameWithOwner >/dev/null 2>&1; then
    REPO="$(gh repo view --json nameWithOwner -q .nameWithOwner)"
  else
    echo "ERROR: Could not infer repo. Set REPO=\"owner/name\"." >&2
    exit 1
  fi
fi

ISSUE_DATA=$(cat <<'EOF'
FEDv4: Federation peer table + lifecycle states|v4.0,federation,db|Add peer statuses (DISCOVERED/TRUSTED/EXIT_PENDING/DEAD/REVOKED), last_seen, key pinning fields, and revocation handling.
FEDv4: Trust establishment + signature verification|v4.0,federation,security|Implement signed federation messages, trust gating, and REVOKED hard-block of all travel.
FEDv4: Travel state machine + persistence|v4.0,federation,engine|Persist travel_id and states INITIATED/COMMITTED/ARRIVED/FAILED; implement reconciliation rules with source universe authoritative.
FEDv4: Combat-state preconditions for travel|v4.0,federation,combat|Enforce no-travel while in combat/attack-lock/interdiction/forced-hold; cannot use federation travel as combat escape.
FEDv4: Entry sector agreement + guaranteed-safe arrival|v4.0,federation,world|Implement agreed entrance sector(s), default FedSpace 1–10; guarantee arrival sector does not trigger mines/fighters/grids/limpets/planet defence.
FEDv4: Local news mirroring for departure/arrival|v4.0,federation,news|Emit local news on departure (“disappeared from this universe”) and arrival (“appeared in sector X”); no cross-universe news feed.
FEDv4: Federation event emissions integration|v4.0,federation,events|Emit federation.* events into the existing event system: peer lifecycle + travel initiated/committed/arrival/failed.
FEDv4: Destination tech policy engine|v4.0,federation,config|Implement destination config policies: ALLOW_ALL / ALLOW_BASIC_ONLY / CONFISCATE_ALL; apply confiscation + replacement rules on arrival.
FEDv4: Economy conversion via FED Credits with loss|v4.0,federation,economy|Per-universe negative exchange penalty vs FED Credits; apply source and destination penalties on universe change; prevent arbitrage.
FEDv4: Crime non-portability, alignment portability|v4.0,federation,gameplay|Criminal record does NOT follow; alignment points DO follow; implement arrival initialisation rules.
FEDv4: Universe death (UVID invalidation) + orphan/naturalise|v4.0,federation,identity|When a universe resets/dies, its UVID namespace is dead; implement ORPHANED travellers and destination naturalisation option.
FEDv4: Failure handling + dual-presence prevention tests|v4.0,federation,tests|Add fault injection tests: lost ACKs, partial commits, destination rollback/quarantine; enforce never dual-presence.
EOF
)

ensure_label() {
  return 0
}

create_issue() {
  local title="$1"
  local labels_csv="$2"
  local body="$3"

  if gh issue list -R "$REPO" --search "\"$title\" in:title" --json title -q '.[].title' | grep -Fxq "$title"; then
    echo "SKIP (exists): $title"
    return 0
  fi

  if [[ "$DRY_RUN" == "1" ]]; then
    echo "[DRY_RUN] gh issue create -R \"$REPO\" --title \"$title\" --label \"$labels_csv\" --body \"$body\""
  else
    gh issue create -R "$REPO" --title "$title" --label "$labels_csv" --body "$body"
    echo "CREATED: $title"
  fi
}

echo "Repo: $REPO"
echo "Dry run: $DRY_RUN"
echo

while IFS='|' read -r title labels body; do
  [[ -z "${title// }" ]] && continue
  create_issue "$title" "$labels" "$body"
done <<< "$ISSUE_DATA"

echo
echo "Done."
