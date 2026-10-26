#!/bin/bash
# Coordinator tick snapshot (agents/coordinator.md, "Each tick" step 2).
# REST only (the cloud sandbox blocks GraphQL); runs on Linux and macOS.
#
#   bash agents/tools/coord_status.sh
R=${SM_REPO:-aray4702/streaming_media}
ROADMAP=${SM_ROADMAP:-64}
BOT='streaming-media-reviewer[bot]'

minutes_since() {  # ISO-8601 UTC time -> whole minutes until now
  python3 -c 'import sys,datetime as d; t=d.datetime.strptime(sys.argv[1],"%Y-%m-%dT%H:%M:%SZ").replace(tzinfo=d.timezone.utc); print(int((d.datetime.now(d.timezone.utc)-t).total_seconds()//60))' "$1"
}

body=$(gh api repos/$R/issues/$ROADMAP --jq .body)
open_prs=$(gh api "repos/$R/pulls?state=open&per_page=100" --jq '[.[]|{number, body: (.body // "")}]')

echo "== roadmap (#$ROADMAP) =="
items=$(printf '%s\n' "$body" | awk '/^## Work list/{f=1;next} /^## /{f=0} f' | grep -E '^- \[[ x]\] #[0-9]+')
cand=""; inprog=0
while read -r line; do
  [[ -z $line ]] && continue
  n=$(sed -E 's/^- \[[ x]\] #([0-9]+).*/\1/' <<<"$line"); box=$(cut -c4 <<<"$line")
  [[ $box == "x" ]] && continue
  st=$(gh api repos/$R/issues/$n --jq '.state+" "+([.labels[].name]|join(","))')
  if [[ $st == closed* ]]; then echo "TICK-NEEDED #$n (closed but unchecked)"; continue; fi
  prs=$(jq -r --arg n "$n" '.[]|select(.body|test("(Fixes|Refs|Closes) #"+$n+"\\b"))|.number' <<<"$open_prs" | tr '\n' ' ')
  [[ $st == *in-progress* ]] && inprog=$((inprog+1))
  if [[ $st == *in-progress* || $st == *needs-human* || $st == *needs-builder* || $st == *blocked* || -n $prs ]]; then busy=BUSY; else busy=FREE; fi
  if [[ -z $cand ]]; then cand=$n; kind=NEXT
  elif [[ $st == *parallel-ok* ]]; then kind=PARALLEL
  else kind=LATER; fi
  echo "$kind #$n $busy [$st] prs: ${prs:-none}"
done <<<"$items"
echo "in-progress items: $inprog"

# Minutes since the last Fixer comment on each in-progress issue; STALE over 90.
for n in $(gh api "repos/$R/issues?state=open&labels=in-progress&per_page=100" --jq '.[]|select(.pull_request==null)|.number'); do
  last=$(gh api "repos/$R/issues/$n/comments?per_page=100" --jq '[.[]|select(.body|contains("sm-agent: fixer"))][-1].created_at // empty')
  if [[ -n $last ]]; then
    age=$(minutes_since "$last"); flag=""; (( age > 90 )) && flag=" STALE"
    echo "  in-progress #$n last fixer comment ${age} min ago$flag"
  else
    echo "  in-progress #$n no fixer comment STALE?"
  fi
done

echo "== open PRs =="
gh api "repos/$R/pulls?state=open&per_page=100" --jq '.[]|"\(.number) \(.head.sha) \(.head.ref) draft=\(.draft) labels=\([.labels[].name]|join(",")) automerge=\(.auto_merge != null) updated=\(.updated_at)"' |
while read -r num sha ref rest; do
  ci=$(gh api repos/$R/commits/$sha/check-runs --jq '[.check_runs[]|select(.name=="ci-ok")|.conclusion // .status][0] // "none"')
  rv=$(gh api repos/$R/pulls/$num/reviews --jq "[.[]|select(.user.login==\"$BOT\")]|last|if .==null then \"none\" else \"\\(.state)@\\(.commit_id[0:7])\" end")
  ms=$(gh api repos/$R/pulls/$num --jq .mergeable_state)
  echo "PR #$num $ref head=${sha:0:7} ci-ok=$ci review=$rv merge=$ms $rest"
done

echo "== escalations / triage =="
gh api "repos/$R/issues?state=open&labels=needs-human&per_page=50" --jq '.[]|"NEEDS-HUMAN #\(.number) \(.title[0:70])"'
gh api "repos/$R/issues?state=open&labels=sev:1&per_page=50" --jq '.[]|select(.pull_request==null)|"SEV1 #\(.number) \(.title[0:70])"'
listed=$(printf '%s\n' "$body" | grep -oE '#[0-9]+' | tr -d '#' | sort -u)
gh api "repos/$R/issues?state=open&per_page=100" --jq '.[]|select(.pull_request==null)|"\(.number)\t\(.created_at)\t\([.labels[].name]|join(","))\t\(.title[0:60])"' |
while IFS=$'\t' read -r n c l t; do
  grep -qx "$n" <<<"$listed" || [[ $l == *agent-log* || $l == *record* || $l == *goal* || $n == "$ROADMAP" ]] || echo "UNPLACED #$n $c [$l] $t"
done
