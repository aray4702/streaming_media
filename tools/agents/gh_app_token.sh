#!/usr/bin/env bash
# Prints a GitHub App installation token (valid 1 hour) for the Reviewer.
# Needs: SM_APP_ID, SM_APP_INSTALLATION_ID, SM_APP_PRIVATE_KEY_B64 (the App's
# PEM private key, base64-encoded on one line). Uses only openssl, curl, jq.
set -euo pipefail
: "${SM_APP_ID:?}" "${SM_APP_INSTALLATION_ID:?}" "${SM_APP_PRIVATE_KEY_B64:?}"

b64url() { openssl base64 -A | tr '+/' '-_' | tr -d '='; }
key=$(mktemp)
trap 'rm -f "$key"' EXIT
printf '%s' "$SM_APP_PRIVATE_KEY_B64" | openssl base64 -d -A > "$key"

now=$(date +%s)
header=$(printf '{"alg":"RS256","typ":"JWT"}' | b64url)
payload=$(printf '{"iat":%d,"exp":%d,"iss":"%s"}' $((now - 60)) $((now + 540)) "$SM_APP_ID" | b64url)
signature=$(printf '%s.%s' "$header" "$payload" | openssl dgst -sha256 -sign "$key" | b64url)

curl -fsS -X POST \
  -H "Authorization: Bearer $header.$payload.$signature" \
  -H "Accept: application/vnd.github+json" \
  "https://api.github.com/app/installations/$SM_APP_INSTALLATION_ID/access_tokens" | jq -er .token
