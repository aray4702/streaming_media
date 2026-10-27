#!/usr/bin/env bash
# Posts the Reviewer agent's output as aray4702-reviewer-app[bot] by
# dispatching the reviewer-relay workflow, then waits for it and prints the
# result. Usage: agents/tools/reviewer_post.sh payload.json
# See agents/tools/reviewer_relay.py for the payload format.
set -euo pipefail
REPO=aray4702/streaming_media
payload_file=${1:?usage: reviewer_post.sh payload.json}
jq -e '.pr | numbers' "$payload_file" > /dev/null || { echo "payload needs a numeric .pr" >&2; exit 2; }

payload=$(gzip -9c "$payload_file" | base64 | tr -d '\n')
if [ "${#payload}" -gt 60000 ]; then
  echo "payload too large (${#payload} chars after compression); shorten the review" >&2
  exit 2
fi

since=$(date -u +%Y-%m-%dT%H:%M:%SZ)
gh api -X POST "repos/$REPO/actions/workflows/reviewer-relay.yml/dispatches" \
  -f ref=main -f "inputs[payload]=$payload"

# Find the run we just started and wait for it.
run_id=""
for _ in $(seq 1 30); do
  run_id=$(gh api "repos/$REPO/actions/workflows/reviewer-relay.yml/runs?event=workflow_dispatch&per_page=5" \
    --jq "[.workflow_runs[] | select(.created_at >= \"$since\")][-1].id // empty")
  [ -n "$run_id" ] && break
  sleep 5
done
[ -n "$run_id" ] || { echo "relay run did not start" >&2; exit 1; }
for _ in $(seq 1 60); do
  read -r status conclusion < <(gh api "repos/$REPO/actions/runs/$run_id" --jq '"\(.status) \(.conclusion)"')
  [ "$status" = completed ] && break
  sleep 5
done
echo "reviewer-relay run $run_id: $status $conclusion (https://github.com/$REPO/actions/runs/$run_id)"
job=$(gh api "repos/$REPO/actions/runs/$run_id/jobs" --jq '.jobs[0].id')
gh api "repos/$REPO/actions/jobs/$job/logs" 2>/dev/null | grep -E "posted|replied|commented|labelled|skipped|FAILED|Error" | sed 's/^[0-9TZ:.-]* //' || true
[ "$conclusion" = success ]
