#!/usr/bin/env python3
"""Runs inside the reviewer-relay workflow: validates the Reviewer agent's
payload and posts it with the App token (GH_TOKEN).

Payload (JSON, gzip + base64 in $PAYLOAD):
  {
    "pr": 12,
    "review":  {"commit_id": "<head sha>", "event": "APPROVE|REQUEST_CHANGES|COMMENT",
                "body": "<!-- sm-agent: reviewer -->...", "comments": [{"path", "line", "side", "body"}]},
    "replies": [{"comment_id": 123, "body": "<!-- sm-agent: reviewer -->..."}],
    "comments": ["<!-- sm-agent: reviewer -->..."],
    "labels": ["needs-human"]
  }
Every part is optional except "pr". A review whose commit_id is no longer the
PR head is skipped (the PR changed after the review was written).
"""
import base64
import gzip
import json
import os
import subprocess
import sys

MARKER = "<!-- sm-agent: reviewer -->"
EVENTS = {"APPROVE", "REQUEST_CHANGES", "COMMENT"}
LABELS = {"needs-human"}
REPO = os.environ["REPO"]


def gh(method, path, body=None):
    cmd = ["gh", "api", "-X", method, f"repos/{REPO}/{path}"]
    if body is not None:
        cmd += ["--input", "-"]
    r = subprocess.run(cmd, input=json.dumps(body) if body is not None else None,
                       capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError(f"{method} {path}: {r.stderr.strip() or r.stdout.strip()}")
    return json.loads(r.stdout) if r.stdout.strip() else None


def summary(line):
    print(line)
    path = os.environ.get("GITHUB_STEP_SUMMARY")
    if path:
        with open(path, "a") as f:
            f.write(line + "\n")


def with_marker(body):
    if not isinstance(body, str) or not body.strip():
        raise ValueError("empty body")
    return body if body.lstrip().startswith(MARKER) else f"{MARKER}\n{body}"


def main():
    p = json.loads(gzip.decompress(base64.b64decode(os.environ["PAYLOAD"])))
    pr_number = int(p["pr"])
    pr = gh("GET", f"pulls/{pr_number}")
    if pr["state"] != "open":
        summary(f"PR #{pr_number} is {pr['state']}; nothing posted")
        return 0
    failures = 0

    review = p.get("review")
    if review:
        event = review.get("event")
        if event not in EVENTS:
            raise ValueError(f"bad review event {event!r}")
        if review.get("commit_id") != pr["head"]["sha"]:
            summary(f"skipped {event} on #{pr_number}: written for {review.get('commit_id')}, head is now {pr['head']['sha']}")
        else:
            body = {"commit_id": review["commit_id"], "event": event, "body": with_marker(review.get("body", ""))}
            comments = []
            for c in review.get("comments") or []:
                cc = {"path": c["path"], "body": c["body"]}
                if "line" in c:
                    cc["line"] = int(c["line"])
                    cc["side"] = c.get("side", "RIGHT")
                if "start_line" in c:
                    cc["start_line"] = int(c["start_line"])
                    cc["start_side"] = c.get("start_side", "RIGHT")
                comments.append(cc)
            if comments:
                body["comments"] = comments
            r = gh("POST", f"pulls/{pr_number}/reviews", body)
            summary(f"posted {event} review on #{pr_number} ({len(comments)} inline comments) by {r['user']['login']}: {r['html_url']}")

    for rep in p.get("replies") or []:
        try:
            r = gh("POST", f"pulls/{pr_number}/comments/{int(rep['comment_id'])}/replies", {"body": with_marker(rep["body"])})
            summary(f"replied to comment {rep['comment_id']}: {r['html_url']}")
        except Exception as e:  # Keep going; report at the end.
            failures += 1
            summary(f"FAILED reply to {rep.get('comment_id')}: {e}")

    for c in p.get("comments") or []:
        r = gh("POST", f"issues/{pr_number}/comments", {"body": with_marker(c)})
        summary(f"commented on #{pr_number}: {r['html_url']}")

    labels = [l for l in p.get("labels") or [] if l in LABELS]
    if labels:
        gh("POST", f"issues/{pr_number}/labels", {"labels": labels})
        summary(f"labelled #{pr_number}: {', '.join(labels)}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
