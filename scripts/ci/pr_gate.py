#!/usr/bin/env python3
"""Report whether a PR meets AGENTS.md rule 16, mechanically.

Usage: scripts/ci/pr_gate.py <pr-number> [--repo jm2/kisakcod]

Needs an authenticated `gh` CLI. Prints one line per check and a verdict:
READY (exit 0), NOT READY (exit 1) or ESCALATE (exit 3, rule 21: a person or
the reviewer must decide). It never merges anything.

Checks:
  - the PR is open and GitHub says it is mergeable (no conflicts);
  - no CI check failed or is still pending (Codacy and CodeRabbit are
    judged by the next two checks instead);
  - Codacy has no open Security finding. Other Codacy categories only warn:
    they need an answer on the PR, which this script can't judge;
  - every review thread is resolved or has a reply;
  - the advisory x86 identity job (#603) passed. If it reports CODE, the
    verdict is ESCALATE: the PR body must declare the x86 change and someone
    must accept it.
"""

import argparse
import json
import subprocess
import sys
import urllib.request

CODACY = ("https://app.codacy.com/api/v3/analysis/organizations/gh/{owner}/"
          "repositories/{name}/pull-requests/{number}/issues?status=new")
JUDGED_ELSEWHERE = ("Codacy", "CodeRabbit")
X86_JOB = "objects vs base"


def gh(*args):
    return subprocess.run(["gh", *args], check=True, capture_output=True,
                          text=True, encoding="utf-8").stdout


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("number", type=int)
    parser.add_argument("--repo", default="jm2/kisakcod")
    args = parser.parse_args()
    owner, name = args.repo.split("/")

    pr = json.loads(gh("pr", "view", str(args.number), "-R", args.repo, "--json",
                       "state,mergeable,baseRefName,headRefOid,statusCheckRollup,files"))
    # Only these inputs can change the Windows x86 objects.
    builds_x86 = any(f["path"].startswith(("src/", "deps/", "scripts/", "cmake/"))
                     or f["path"].endswith(("CMakeLists.txt", ".cmake", "CMakePresets.json"))
                     for f in pr["files"]
                     if not f["path"].startswith("scripts/ci/"))
    blockers, warnings, escalations = [], [], []

    if pr["state"] != "OPEN":
        blockers.append(f"state is {pr['state']}")
    if pr["mergeable"] != "MERGEABLE":
        blockers.append(f"mergeable is {pr['mergeable']} (UNKNOWN: retry; CONFLICTING: rule 17)")
    if pr["baseRefName"] != "master":
        blockers.append(f"base is {pr['baseRefName']}: merge that first, then retarget (rule 16)")

    x86_seen = False
    for check in pr["statusCheckRollup"]:
        label = check.get("name") or check.get("context") or "?"
        state = check.get("conclusion") or check.get("state") or check.get("status")
        if X86_JOB in label:
            x86_seen = True
            if state in ("FAILURE", "failure"):
                escalations.append("x86 identity job reports CODE/added/removed objects: "
                                   "the PR body must declare the change (rule 16)")
            elif state not in ("SUCCESS", "success", "SKIPPED"):
                blockers.append(f"x86 identity job is {state}")
            continue
        if any(label.startswith(prefix) for prefix in JUDGED_ELSEWHERE):
            continue
        if state in ("FAILURE", "failure", "ERROR", "error", "CANCELLED", "TIMED_OUT"):
            blockers.append(f"CI failed: {label}")
        elif state not in ("SUCCESS", "success", "SKIPPED", "NEUTRAL"):
            blockers.append(f"CI not finished: {label} ({state})")
    if not x86_seen and builds_x86:
        escalations.append("no x86 identity result on this head (job missing or the PR predates it): "
                           "run scripts/ci/compare-x86-pr-objects.sh or rerun CI before merging")

    url = CODACY.format(owner=owner, name=name, number=args.number)
    with urllib.request.urlopen(url, timeout=30) as response:
        issues = json.load(response).get("data", [])
    for issue in issues:
        info = issue["commitIssue"]
        where = f"{info['filePath']}:{info['lineNumber']}"
        category = info["patternInfo"]["category"]
        if category == "Security":
            blockers.append(f"Codacy Security {where}: fix it, or escalate if it can't be fixed")
        else:
            warnings.append(f"Codacy {category} {where}: needs a fix or an answer on the PR")

    query = ("query($owner:String!,$name:String!,$number:Int!){repository(owner:$owner,name:$name)"
             "{pullRequest(number:$number){reviewThreads(first:100){nodes{isResolved "
             "comments(first:100){totalCount nodes{author{login} path line}}}}}}}")
    threads = json.loads(gh("api", "graphql", "-f", f"query={query}", "-F", f"owner={owner}",
                            "-F", f"name={name}", "-F", f"number={args.number}"))
    for thread in threads["data"]["repository"]["pullRequest"]["reviewThreads"]["nodes"]:
        comments = thread["comments"]
        if thread["isResolved"] or comments["totalCount"] > 1:
            continue
        first = comments["nodes"][0]
        author = (first.get("author") or {}).get("login", "?")
        blockers.append(f"unanswered review thread by {author} at {first.get('path')}:{first.get('line')}")

    for line in blockers:
        print(f"BLOCK    {line}")
    for line in escalations:
        print(f"ESCALATE {line}")
    for line in warnings:
        print(f"WARN     {line}")
    if blockers:
        print(f"NOT READY: #{args.number}")
        return 1
    if escalations:
        print(f"ESCALATE: #{args.number}")
        return 3
    print(f"READY: #{args.number}" + (" (answer every WARN on the PR first)" if warnings else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
