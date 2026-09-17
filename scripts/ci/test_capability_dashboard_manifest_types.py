#!/usr/bin/env python3
"""Manifest identity and type-boundary tests for the capability dashboard."""

# Run with:  python3 scripts/ci/test_capability_dashboard_manifest_types.py
#
# These tests are the PR #150 schema-boundary regressions: reference and
# capability ids are checked before anything indexes or builds a set from
# them, and a wrong field/container *type* produces a schema error instead of
# an AttributeError/TypeError escaping the CLI.  They are split out of
# test_capability_dashboard.py so each module stays a cohesive, readable size.

from __future__ import annotations

import copy
import importlib.util
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
MODULE_PATH = HERE / "capability-dashboard.py"
REPO_ROOT = HERE.parents[1]

spec = importlib.util.spec_from_file_location(
    "capability_dashboard_launcher", MODULE_PATH
)
if spec is None or spec.loader is None:
    raise ImportError(f"cannot load {MODULE_PATH}")
cd = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cd)


class ReferenceIdentityTests(unittest.TestCase):
    """Reference ids are checked before anything indexes by id (PR #150)."""

    @classmethod
    def setUpClass(cls):
        cls.manifest = cd.load_manifest(
            REPO_ROOT / "docs" / "capability" / "manifest.json"
        )

    def _append_reference(self, reference):
        manifest = copy.deepcopy(self.manifest)
        manifest["commercial_references"].append(reference)
        return manifest

    def test_missing_reference_id_is_rejected(self):
        # A bare {"status": "pending"} row used to validate cleanly and then
        # raise KeyError("id") while computing the aggregate.
        broken = self._append_reference({"status": "pending"})
        errors = cd.validate_manifest(broken)
        self.assertTrue(
            any("id must be a non-empty string" in error for error in errors),
            msg=f"id-less reference was accepted: {errors}",
        )
        result = cd.compute_aggregate(broken)
        self.assertFalse(result["references_ok"])
        self.assertEqual(result["delivered"], 0)

    def test_empty_and_whitespace_reference_ids_are_rejected(self):
        for bad in ("", "   ", "\t\n"):
            with self.subTest(rid=bad):
                errors = cd.validate_manifest(
                    self._append_reference({"id": bad, "status": "pending"})
                )
                self.assertTrue(
                    any(
                        "id must be a non-empty string" in error
                        for error in errors
                    ),
                    msg=f"empty reference id {bad!r} was accepted: {errors}",
                )

    def test_non_string_reference_ids_are_rejected(self):
        for bad in (None, 7, ["commercial-2.0"], {"id": "x"}):
            with self.subTest(rid=bad):
                errors = cd.validate_manifest(
                    self._append_reference({"id": bad, "status": "pending"})
                )
                self.assertTrue(
                    any(
                        "id must be a non-empty string" in error
                        for error in errors
                    ),
                    msg=f"non-string reference id {bad!r} was accepted: {errors}",
                )

    def test_duplicate_reference_ids_are_still_rejected(self):
        broken = copy.deepcopy(self.manifest)
        broken["commercial_references"].append(
            {"id": "commercial-1.7", "status": "pending"}
        )
        errors = cd.validate_manifest(broken)
        self.assertTrue(any("duplicate ids" in error for error in errors))

    def test_valid_extra_reference_is_accepted(self):
        # A syntactically valid additional reference is not an error; the
        # mandatory declared references are what the membership check enforces.
        broken = self._append_reference({"id": "mac-app-2.0", "status": "pending"})
        self.assertEqual(cd.validate_manifest(broken), [])


class CapabilityIdentityTests(unittest.TestCase):
    """Capability ids are checked before the seen_ids set (PR #150)."""

    @classmethod
    def setUpClass(cls):
        cls.manifest = cd.load_manifest(
            REPO_ROOT / "docs" / "capability" / "manifest.json"
        )

    def _manifest_with_first_id(self, value=None, *, delete=False):
        manifest = copy.deepcopy(self.manifest)
        if delete:
            del manifest["capabilities"][0]["id"]
        else:
            manifest["capabilities"][0]["id"] = value
        return manifest

    def test_missing_capability_id_is_rejected(self):
        # An id-less row used to fall back to a placeholder and scatter
        # follow-on field errors; it now records one schema error and is
        # skipped, so it cannot contribute an id or a pair.
        errors = cd.validate_manifest(self._manifest_with_first_id(delete=True))
        self.assertTrue(
            any("id must be a non-empty string" in error for error in errors),
            msg=f"id-less capability was accepted: {errors}",
        )

    def test_empty_and_whitespace_capability_ids_are_rejected(self):
        for bad in ("", "   ", "\t\n"):
            with self.subTest(cid=bad):
                errors = cd.validate_manifest(self._manifest_with_first_id(bad))
                self.assertTrue(
                    any(
                        "id must be a non-empty string" in error
                        for error in errors
                    ),
                    msg=f"empty capability id {bad!r} was accepted: {errors}",
                )

    def test_non_string_capability_ids_never_reach_set_membership(self):
        # The unhashable list/dict ids used to raise TypeError from the
        # seen_ids membership test before validate_manifest could return its
        # schema errors, and the CLI catches only SystemExit, so the run died
        # in an uncaught traceback instead of reporting the invalid manifest.
        for bad in (None, 7, ["delivery"], {"id": "x"}):
            with self.subTest(cid=bad):
                errors = cd.validate_manifest(self._manifest_with_first_id(bad))
                self.assertTrue(
                    any(
                        "id must be a non-empty string" in error
                        for error in errors
                    ),
                    msg=f"non-string capability id {bad!r} was accepted: {errors}",
                )

    def test_duplicate_capability_ids_are_still_rejected(self):
        broken = copy.deepcopy(self.manifest)
        broken["capabilities"][1]["id"] = broken["capabilities"][0]["id"]
        errors = cd.validate_manifest(broken)
        self.assertTrue(any("duplicate id" in error for error in errors))

    def test_valid_renamed_capability_id_is_accepted(self):
        # A distinct, well-formed id changes nothing: validation stays clean
        # and the row keeps contributing its target/mode pair to delivery.
        broken = self._manifest_with_first_id("renamed-capability")
        self.assertEqual(cd.validate_manifest(broken), [])


class MalformedFieldTypeTests(unittest.TestCase):
    """Malformed field/container types must error, never raise (PR #150)."""

    # validate_manifest dereferences field values and iterates containers, so
    # a wrong JSON type used to escape as AttributeError (``int.strip``,
    # ``list.get``) or TypeError (iterating an int); the CLI catches only
    # SystemExit, so the run died in a traceback instead of reporting the
    # invalid manifest.

    @classmethod
    def setUpClass(cls):
        cls.manifest = cd.load_manifest(
            REPO_ROOT / "docs" / "capability" / "manifest.json"
        )

    def _broken(self, mutate):
        manifest = copy.deepcopy(self.manifest)
        mutate(manifest)
        return manifest

    def test_scalar_owner_and_blocker_are_rejected_not_raised(self):
        cases = [
            ("owner", "owner must be a non-empty string"),
            ("blocker", "blocker must be a non-empty string"),
        ]
        for field, message in cases:
            for bad in (7, [], {}, None):
                with self.subTest(field=field, value=bad):
                    errors = cd.validate_manifest(
                        self._broken(
                            lambda m, f=field, v=bad: (
                                m["capabilities"][0].__setitem__(f, v)
                            )
                        )
                    )
                    self.assertTrue(
                        any(message in error for error in errors),
                        msg=f"{field}={bad!r} did not produce a schema "
                        f"error: {errors}",
                    )

    def test_non_object_evidence_is_rejected_not_raised(self):
        # A list ``evidence`` used to raise AttributeError from
        # ``evidence.get(...)``; a scalar used to raise TypeError from the
        # ``field not in evidence`` membership test.
        for bad in (["bad"], "bad", 7):
            with self.subTest(evidence=bad):
                errors = cd.validate_manifest(
                    self._broken(
                        lambda m, v=bad: (
                            m["capabilities"][0].__setitem__("evidence", v)
                        )
                    )
                )
                self.assertTrue(
                    any(
                        "evidence must be an object" in error
                        for error in errors
                    ),
                    msg=f"evidence={bad!r} did not produce a schema "
                    f"error: {errors}",
                )

    def test_non_list_evidence_kinds_are_rejected_not_raised(self):
        errors = cd.validate_manifest(
            self._broken(
                lambda m: m["capabilities"][0].__setitem__("evidence_kinds", 7)
            )
        )
        self.assertTrue(
            any("evidence_kinds must be a list" in error for error in errors),
            msg=f"scalar evidence_kinds was not rejected: {errors}",
        )

    def test_supporting_evidence_record_must_be_an_object(self):
        # The renderer reads ``evidence.note``, so a truthy non-object
        # evidence record used to pass validation and raise AttributeError
        # while rendering -- after the CLI's SystemExit-only catch.
        errors = cd.validate_manifest(
            self._broken(
                lambda m: m["supporting_evidence"][0].__setitem__(
                    "evidence", ["bad"]
                )
            )
        )
        self.assertTrue(
            any(
                "supporting evidence" in error
                and "evidence must be an object" in error
                for error in errors
            ),
            msg=f"non-object supporting evidence record was accepted: "
            f"{errors}",
        )

    def test_non_object_enums_are_rejected_not_raised(self):
        # ``enums.get(...)`` used to raise AttributeError for a scalar
        # ``enums`` block.
        errors = cd.validate_manifest(self._broken(lambda m: m.__setitem__("enums", 7)))
        self.assertTrue(
            any("enums must be an object" in error for error in errors),
            msg=f"scalar enums was not rejected: {errors}",
        )

    def test_non_list_validation_levels_are_rejected_not_raised(self):
        # A scalar ``validation_levels`` used to raise TypeError from
        # ``len()``/``set()``, and an unhashable entry (a list) from
        # ``set(levels)`` itself.
        for bad in (7, "packaged_clean_machine", [["a"], "configured"]):
            with self.subTest(levels=bad):
                errors = cd.validate_manifest(
                    self._broken(
                        lambda m, v=bad: m["enums"].__setitem__(
                            "validation_levels", v
                        )
                    )
                )
                self.assertTrue(
                    any(
                        "enums.validation_levels" in error
                        for error in errors
                    ),
                    msg=f"validation_levels={bad!r} was not rejected: "
                    f"{errors}",
                )

    def test_non_object_manifest_root_is_rejected_not_raised(self):
        # ``json.load`` can return any JSON value; every helper dereferences
        # the manifest mapping, so a non-object root used to raise
        # AttributeError from ``manifest.get(...)``.
        for bad in ([1, 2], "manifest", 7, None):
            with self.subTest(root=bad):
                errors = cd.validate_manifest(bad)
                self.assertEqual(errors, ["manifest must be a JSON object"])

    def test_scalar_top_level_containers_are_rejected_not_raised(self):
        # Iterating a scalar container used to raise TypeError; iterating a
        # dict silently iterated its keys.
        cases = [
            ("targets", "targets must be a list"),
            ("capabilities", "capabilities must be a list"),
            ("commercial_references", "commercial_references must be a list"),
            ("supporting_evidence", "supporting_evidence must be a list"),
        ]
        for field, message in cases:
            for bad in (7, "x", {"a": 1}):
                with self.subTest(field=field, value=bad):
                    errors = cd.validate_manifest(
                        self._broken(lambda m, f=field, v=bad: m.__setitem__(f, v))
                    )
                    self.assertTrue(
                        any(message in error for error in errors),
                        msg=f"{field}={bad!r} did not produce a schema "
                        f"error: {errors}",
                    )

    def test_cli_reports_malformed_field_as_validation_failure(self):
        # The documented CLI failure path: a malformed field value must reach
        # the user as "manifest validation failed" with exit status 2, not as
        # an uncaught traceback from a non-SystemExit exception.
        import contextlib
        import io
        import json

        broken = copy.deepcopy(self.manifest)
        broken["capabilities"][0]["owner"] = 7
        with tempfile.TemporaryDirectory() as tmp:
            manifest_path = Path(tmp) / "broken-manifest.json"
            manifest_path.write_text(
                json.dumps(broken), encoding="utf-8"
            )
            buffer = io.StringIO()
            with contextlib.redirect_stderr(buffer):
                code = cd.main(["--stdout", "--manifest", str(manifest_path)])
        self.assertEqual(code, 2)
        self.assertIn("manifest validation failed", buffer.getvalue())
        self.assertIn(
            "owner must be a non-empty string", buffer.getvalue()
        )


if __name__ == "__main__":
    unittest.main(verbosity=2)
