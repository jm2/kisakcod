#!/usr/bin/env python3
"""Tests for release_meta.py: its functions, and the CLI release.yml runs."""

from __future__ import annotations

import contextlib
import copy
import io
import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import release_meta as rm  # noqa: E402

SHA_A, SHA_B = "a" * 40, "b" * 40
REAL_MANIFEST = json.loads(rm.MANIFEST.read_text())


def cli(*args: str) -> SimpleNamespace:
    """Run the command line in-process, as release.yml invokes it."""
    out, err = io.StringIO(), io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
        try:
            code = rm.main(list(args))
        except SystemExit as exc:  # argparse rejects bad arguments this way
            code = exc.code
    return SimpleNamespace(returncode=code, stdout=out.getvalue(), stderr=err.getvalue())


def manifest_with(levels: dict[tuple[str, str], str]) -> dict:
    """The real manifest with every cell at `none` except the given ones."""
    m = copy.deepcopy(REAL_MANIFEST)
    for cell in m["cells"]:
        cell["level"] = levels.get((cell["target"], cell["role"]), "none")
    return m


class VersionTest(unittest.TestCase):
    def test_publish_takes_only_plain_semver(self):
        self.assertEqual(rm.parse_version("v1.20.3", False),
                         {"tag": "v1.20.3", "version": "1.20.3", "name": "KisakCOD 1.20.3"})
        for bad in ("", "1.2.3", "v1.2", "v01.2.3", "v1.2.3-rc1", "v1.2.3.4", "v1.2.3/x", "v\uff11.2.3"):
            with self.subTest(bad=bad), self.assertRaises(rm.ReleaseError):
                rm.parse_version(bad, False)

    def test_dry_run_takes_a_suffix_and_has_a_default(self):
        self.assertEqual(rm.parse_version("", True)["tag"], "v0.0.0-dryrun")
        self.assertEqual(rm.parse_version("v0.1.0-rc.1", True)["version"], "0.1.0-rc.1")
        for bad in ("v0.1.0-", "v0.1.0-../x", "v0.1.0 x", "latest"):
            with self.subTest(bad=bad), self.assertRaises(rm.ReleaseError):
                rm.parse_version(bad, True)


class MatrixTest(unittest.TestCase):
    def test_real_manifest_builds_the_linux_headless_leg(self):
        legs, _ = rm.build_matrix(REAL_MANIFEST, "1.2.3")
        leg = next(leg for leg in legs if leg["target"] == "linux-amd64")
        self.assertEqual(leg["stem"], "kisakcod-1.2.3-linux-x64-headless-preview")
        self.assertEqual((leg["role"], leg["recipe"], leg["runner"], leg["binary"]),
                         ("headless-server", "linux-headless", "ubuntu-24.04", "KisakCOD-dedi"))

    def test_levels_pick_legs_tiers_and_names(self):
        legs, missing = rm.build_matrix(manifest_with({
            ("linux-amd64", "headless-server"): "fork_peer",
            ("linux-arm64", "headless-server"): "steam18_peer",
            ("linux-amd64", "mp-client"): "compiles",       # below links: no leg
            ("windows-x86", "headless-server"): "packaged",  # not required: no leg
            ("macos-arm64", "headless-server"): "links",     # no recipe yet
        }), "2.0.0")
        self.assertEqual([(leg["stem"], leg["tier"]) for leg in legs], [
            ("kisakcod-2.0.0-linux-arm64-headless", "first-class"),
            ("kisakcod-2.0.0-linux-x64-headless-beta", "beta"),
        ])
        self.assertEqual(missing, ["macos-arm64/headless-server"])

    def test_every_level_maps_to_a_tier(self):
        self.assertEqual([rm.tier(level) for level in rm.LEVELS[2:]],
                         ["preview", "preview", "beta", "first-class", "first-class"])

    def test_malformed_manifest_fails(self):
        m = copy.deepcopy(REAL_MANIFEST)
        m["cells"][0]["level"] = "shipped"
        with self.assertRaises(rm.ReleaseError):
            rm.build_matrix(m, "1.0.0")


class GateTest(unittest.TestCase):
    def test_publishing_needs_master_green_ci_and_a_free_tag(self):
        self.assertEqual(rm.gate_problems(SHA_A, "refs/heads/master", 1, ""), [])
        self.assertEqual(rm.gate_problems(SHA_A, "refs/heads/master", 2, SHA_A), [])
        self.assertEqual(len(rm.gate_problems(SHA_A, "refs/heads/topic", 0, SHA_B)), 3)


class CliTest(unittest.TestCase):
    def test_metadata_prints_step_outputs(self):
        run = cli("metadata", "--version", "v1.2.3", "--dry-run", "false")
        self.assertEqual(run.returncode, 0, run.stderr)
        out = dict(line.split("=", 1) for line in run.stdout.splitlines())
        self.assertEqual((out["tag"], out["version"], out["prerelease"]), ("v1.2.3", "1.2.3", "true"))
        stems = [leg["stem"] for leg in json.loads(out["matrix"])["include"]]
        self.assertIn("kisakcod-1.2.3-linux-x64-headless-preview", stems)

    def test_metadata_fails_closed(self):
        run = cli("metadata", "--version", "1.2.3", "--dry-run", "false")
        self.assertEqual(run.returncode, 1)
        self.assertIn("::error::", run.stderr)
        # No cell at links, or one with no recipe: no release.
        for levels in ({}, {("macos-arm64", "mp-client"): "links"}):
            with tempfile.TemporaryDirectory() as tmp:
                path = Path(tmp) / "manifest.json"
                path.write_text(json.dumps(manifest_with(levels)))
                run = cli("metadata", "--manifest", str(path))
            self.assertEqual(run.returncode, 1, levels)
            self.assertIn("::error::", run.stderr)

    def test_gate_errors_when_publishing_and_warns_on_dry_run(self):
        args = ("gate", "--commit", SHA_A, "--ref", "refs/heads/topic", "--ci-runs", "0")
        run = cli(*args, "--dry-run", "false")
        self.assertEqual((run.returncode, run.stderr.count("::error::")), (1, 2))
        run = cli(*args, "--dry-run", "true")
        self.assertEqual((run.returncode, run.stdout, run.stderr.count("::warning::")), (0, "publish=false\n", 2))
        run = cli("gate", "--commit", SHA_A, "--ref", "refs/heads/master", "--ci-runs", "1", "--dry-run", "false")
        self.assertEqual((run.returncode, run.stdout), (0, "publish=true\n"))
        self.assertEqual(cli("gate", "--commit", "HEAD", "--ref", "refs/heads/master", "--ci-runs", "1").returncode, 1)

    def test_readme_names_the_package_and_its_limits(self):
        legs, _ = rm.build_matrix(REAL_MANIFEST, "1.2.3")
        leg = next(leg for leg in legs if leg["target"] == "linux-amd64")
        run = cli("readme", "--leg", json.dumps(leg), "--version", "1.2.3", "--commit", SHA_A,
                  "--requires", "glibc 2.39")
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertLessEqual(max(map(len, run.stdout.splitlines())), 76)
        words = " ".join(run.stdout.split())
        for text in ("KisakCOD 1.2.3: linux-x64 headless dedicated server (preview)", SHA_A,
                     "no Call of Duty 4 content", "kisakcod-1.2.3-linux-x64-headless-preview-debugsymbols.tar.xz",
                     "Requires: glibc 2.39.", "GNU GPL v3"):
            self.assertIn(text, words)


class NotesTest(unittest.TestCase):
    """Runs the notes subcommand over a real git history in a temporary repository."""

    def git(self, *args: str) -> str:
        env = {"GIT_AUTHOR_NAME": "t", "GIT_AUTHOR_EMAIL": "t@example.com", "GIT_COMMITTER_NAME": "t",
               "GIT_COMMITTER_EMAIL": "t@example.com", "HOME": self.repo, "PATH": os.environ["PATH"]}
        return subprocess.run(["git", "-C", self.repo, *args], check=True, capture_output=True, text=True,
                              env=env).stdout.strip()

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.repo = self.tmp.name
        self.git("init", "-q", "-b", "master")
        self.git("commit", "-q", "--allow-empty", "-m", "Initial import")
        self.git("tag", "v0.1.0")
        self.git("commit", "-q", "--allow-empty", "-m", "Fix a typo")
        self.git("tag", "-a", "v0.2.0-rc1", "-m", "not a release")  # only vX.Y.Z tags count
        self.git("checkout", "-q", "-b", "topic")
        self.git("commit", "-q", "--allow-empty", "-m", "Topic work that the merge covers")
        self.git("checkout", "-q", "master")
        self.git("merge", "-q", "--no-ff", "topic", "-m", "Merge pull request #7 from jm2/topic\n\nAdd a thing")
        self.commit = self.git("rev-parse", "HEAD")
        self.legs, _ = rm.build_matrix(REAL_MANIFEST, "0.2.0")

    def tearDown(self):
        self.tmp.cleanup()

    def notes(self, tag: str) -> SimpleNamespace:
        return cli("notes", "--tag", tag, "--commit", self.commit, "--matrix", json.dumps({"include": self.legs}),
                   "--run-url", "https://example.com/run/1", "--repo-dir", self.repo)

    def test_changes_since_the_previous_release(self):
        run = self.notes("v0.2.0")
        self.assertEqual(run.returncode, 0, run.stderr)
        body = run.stdout.split("## Changes since v0.1.0\n\n", 1)[1]
        fix = self.git("rev-parse", "--short=8", "HEAD~1")
        self.assertEqual(body, f"- Add a thing (#7)\n- Fix a typo ({fix})\n")
        for text in ("# KisakCOD 0.2.0", "`kisakcod-0.2.0-linux-x64-headless-preview.tar.xz` | linux-amd64",
                     "kisakcod-0.2.0-source.tar.gz", "no Call of Duty 4 content", self.commit):
            self.assertIn(text, run.stdout)

    def test_first_release_lists_the_history(self):
        self.git("tag", "-d", "v0.1.0")
        run = self.notes("v0.1.0")
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertIn("## Changes (the latest 100 on master)", run.stdout)
        self.assertTrue(run.stdout.endswith("- Initial import (%s)\n" % self.git("rev-parse", "--short=8", "HEAD~2")))

    def test_bad_tag_fails(self):
        self.assertEqual(self.notes("latest").returncode, 1)


if __name__ == "__main__":
    unittest.main()
