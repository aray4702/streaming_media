#!/bin/bash
# Every page of a GitHub REST list, joined into one JSON array.
#
#   agents/tools/gh_pages.sh 'repos/aray4702/streaming_media/issues/111/comments?per_page=100' | jq '.[-5:]'
#
# Use this instead of `gh api --paginate` in the cloud sandbox: GitHub's
# next-page links use repositories/{id}/ paths, which the session proxy
# refuses (HTTP 403), so --paginate returns the first page and fails.
set -euo pipefail
path=$1
per_page=$(sed -n 's/.*[?&]per_page=\([0-9]*\).*/\1/p' <<<"$path")
per_page=${per_page:-30}
sep='?'; [[ $path == *\?* ]] && sep='&'
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
page=1
while :; do
  gh api "${path}${sep}page=${page}" > "$tmp/$page.json"
  [ "$(jq length "$tmp/$page.json")" -lt "$per_page" ] && break
  page=$((page + 1))
done
for i in $(seq 1 "$page"); do cat "$tmp/$i.json"; done | jq -s 'add'
