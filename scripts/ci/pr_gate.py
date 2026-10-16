#!/usr/bin/env python3
"""Report whether a PR meets AGENTS.md rule 16, mechanically.

Usage: scripts/ci/pr_gate.py <pr-number> [--repo jm2/kisakcod]

Needs an authenticated `gh` CLI. Prints one line per finding and a verdict:
READY (exit 0), NOT READY (exit 1) or ESCALATE (exit 3, rule 21: a person or
the reviewer must decide). It never merges anything. READY still requires
every WARN line to be fixed or answered on the PR first.

Checks:
  - the PR is open, based on master and mergeable (no conflicts);
  - no CI check failed or is still pending (Codacy and CodeRabbit are
    judged by the next two checks instead);
  - Codacy has no open Security finding; other categories warn;
  - every review thread is resolved or has a reply, no review requests
    changes, and review-body findings outside the diff warn;
  - the advisory x86 identity job: CODE escalates, and so does a missing
    result when the PR touches compiled inputs.
"""

import argparse
import json
import re
import shutil
import subprocess  # nosec B404 - runs only the resolved gh binary with fixed arguments
import sys
import urllib.parse
import urllib.request

CODACY = ("https://app.codacy.com/api/v3/analysis/organizations/gh/{owner}/"
          "repositories/{name}/pull-requests/{number}/issues?status=new")
JUDGED_ELSEWHERE = ("Codacy", "CodeRabbit")
X86_JOB = "objects vs base"
PASSED = ("SUCCESS", "success", "SKIPPED", "NEUTRAL")
FAILED = ("FAILURE", "failure", "ERROR", "error", "CANCELLED", "TIMED_OUT")
REPO_PATTERN = re.compile(r"^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$")
MERGEABLE_STATES = ("CLEAN", "UNSTABLE", "HAS_HOOKS")
MISMATCH = ("FAILURE", "failure")
GH_TIMEOUT = 120
# CodeRabbit puts findings on lines outside the diff into the review body.
OUTSIDE_DIFF = "Outside diff range"


class Findings:
    """Blocking, escalating and warning lines collected by the checks."""

    def __init__(self):
        """Start with no findings."""
        self.blockers, self.escalations, self.warnings = [], [], []


def gh(*args):
    executable = shutil.which("gh")
    if executable is None:
        sys.exit("pr_gate: the gh CLI is not on PATH")
    # Argument list, no shell, executable resolved by shutil.which; the
    # arguments are validated ints and an owner/name repo.
    try:
        result = subprocess.run([executable, *args], check=True, capture_output=True, text=True, encoding="utf-8", timeout=GH_TIMEOUT)  # nosec B603  # nosemgrep
    except subprocess.TimeoutExpired:
        sys.exit(f"pr_gate: gh {args[0]} timed out after {GH_TIMEOUT}s: NOT READY, rerun")
    return result.stdout


def builds_x86(files):
    # Only these inputs can change the Windows x86 objects.
    for path in (f["path"] for f in files):
        if path.startswith("scripts/ci/"):
            continue
        if path.startswith(("src/", "deps/", "scripts/", "cmake/")) or path.endswith(
                ("CMakeLists.txt", ".cmake", "CMakePresets.json")):
            return True
    return False


def check_pr(pr, found):
    if pr["state"] != "OPEN":
        found.blockers.append(f"state is {pr['state']}")
    if pr["mergeable"] != "MERGEABLE":
        found.blockers.append(f"mergeable is {pr['mergeable']} (UNKNOWN: retry; CONFLICTING: rule 17)")
    # GitHub's own verdict: BLOCKED means required checks or reviews are missing
    # for this head; only CLEAN, UNSTABLE (non-required checks) and HAS_HOOKS merge.
    if pr["mergeStateStatus"] not in MERGEABLE_STATES:
        found.blockers.append(f"GitHub merge state is {pr['mergeStateStatus']}")
    if pr["baseRefName"] != "master":
        found.blockers.append(f"base is {pr['baseRefName']}: merge that first, then retarget (rule 16)")
    if pr["reviewDecision"] == "CHANGES_REQUESTED":
        found.blockers.append("a review requests changes")


def check_x86(state, found):
    if state in MISMATCH:
        found.escalations.append("x86 identity job reports CODE/added/removed objects: "
                                 "the PR body must declare the change (rule 16)")
    elif state in FAILED:
        found.blockers.append(f"x86 identity job is {state}, so it proved nothing: rerun it")
    elif state not in PASSED:
        found.blockers.append(f"x86 identity job is {state}")


def check_job(label, state, found):
    if state in FAILED:
        found.blockers.append(f"CI failed: {label}")
    elif state not in PASSED:
        found.blockers.append(f"CI not finished: {label} ({state})")


def check_ci(pr, found):
    x86_seen = False
    for check in pr["statusCheckRollup"]:
        label = check.get("name") or check.get("context") or "?"
        state = check.get("conclusion") or check.get("state") or check.get("status")
        if X86_JOB in label:
            x86_seen = True
            check_x86(state, found)
        elif not label.startswith(JUDGED_ELSEWHERE):
            check_job(label, state, found)
    if not x86_seen and builds_x86(pr["files"]):
        found.escalations.append("no x86 identity result on this head (job missing or the PR predates "
                                 "it): run scripts/ci/compare-x86-pr-objects.sh or rerun CI")


def check_codacy(owner, name, number, found):
    base = CODACY.format(owner=owner, name=name, number=number)
    if not base.startswith("https://app.codacy.com/"):
        sys.exit("pr_gate: unexpected Codacy URL")
    issues, cursor = [], ""
    while True:
        url = base + (f"&cursor={urllib.parse.quote(cursor)}" if cursor else "")
        # Fixed https Codacy endpoint built from validated parts.
        with urllib.request.urlopen(url, timeout=30) as response:  # nosec B310  # nosemgrep
            page = json.load(response)
        if not page.get("analyzed"):
            found.blockers.append("Codacy hasn't analyzed this head yet: rerun later")
            return
        issues += page.get("data", [])
        cursor = (page.get("pagination") or {}).get("cursor")
        if not cursor:
            break
    for issue in issues:
        info = issue["commitIssue"]
        where = f"{info['filePath']}:{info['lineNumber']}"
        category = info["patternInfo"]["category"]
        if category == "Security":
            found.blockers.append(f"Codacy Security {where}: fix it, or escalate if it can't be fixed")
        else:
            found.warnings.append(f"Codacy {category} {where}: fix it or answer it on the PR")


def review_threads(owner, name, number):
    query = ("query($owner:String!,$name:String!,$number:Int!,$after:String){repository(owner:$owner,"
             "name:$name){pullRequest(number:$number){reviewThreads(first:100,after:$after){pageInfo"
             "{hasNextPage endCursor} nodes{isResolved comments(first:1){totalCount "
             "nodes{author{login} path line}}}}}}}")
    after = []
    while True:
        result = json.loads(gh("api", "graphql", "-f", f"query={query}", "-F", f"owner={owner}",
                               "-F", f"name={name}", "-F", f"number={number}", *after))
        threads = result["data"]["repository"]["pullRequest"]["reviewThreads"]
        yield from threads["nodes"]
        if not threads["pageInfo"]["hasNextPage"]:
            return
        after = ["-f", f"after={threads['pageInfo']['endCursor']}"]


def check_threads(pr, owner, name, number, found):
    for review in pr["reviews"]:
        if OUTSIDE_DIFF in (review.get("body") or ""):
            author = (review.get("author") or {}).get("login", "?")
            found.warnings.append(f"review by {author} has findings outside the diff in its body: "
                                  "answer each on the PR")
    for thread in review_threads(owner, name, number):
        comments = thread["comments"]
        if thread["isResolved"] or comments["totalCount"] > 1:
            continue
        first = comments["nodes"][0]
        author = (first.get("author") or {}).get("login", "?")
        found.blockers.append(f"unanswered review thread by {author} at {first.get('path')}:{first.get('line')}")


def verdict(number, found):
    for line in found.blockers:
        print(f"BLOCK    {line}")
    for line in found.escalations:
        print(f"ESCALATE {line}")
    for line in found.warnings:
        print(f"WARN     {line}")
    if found.blockers:
        print(f"NOT READY: #{number}")
        return 1
    if found.escalations:
        print(f"ESCALATE: #{number}")
        return 3
    suffix = f" once each of the {len(found.warnings)} WARN lines is fixed or answered" if found.warnings else ""
    print(f"READY: #{number}{suffix}")
    return 0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("number", type=int)
    parser.add_argument("--repo", default="jm2/kisakcod")
    args = parser.parse_args()
    if not REPO_PATTERN.match(args.repo):
        sys.exit("pr_gate: --repo must be owner/name")
    owner, name = args.repo.split("/")

    pr = json.loads(gh("pr", "view", str(args.number), "-R", args.repo, "--json",
                       "state,mergeable,mergeStateStatus,baseRefName,headRefOid,reviewDecision,reviews,"
                       "statusCheckRollup,files"))
    found = Findings()
    check_pr(pr, found)
    check_ci(pr, found)
    check_codacy(owner, name, args.number, found)
    check_threads(pr, owner, name, args.number, found)
    # Every check above read one head; a push since then voids the verdict.
    head_now = gh("pr", "view", str(args.number), "-R", args.repo, "--json", "headRefOid",
                  "--jq", ".headRefOid").strip()
    if head_now != pr["headRefOid"]:
        found.blockers.append(f"head moved from {pr['headRefOid'][:8]} to {head_now[:8]} during the gate: rerun")
    print(f"head   {pr['headRefOid'][:8]}")
    return verdict(args.number, found)


if __name__ == "__main__":
    sys.exit(main())
