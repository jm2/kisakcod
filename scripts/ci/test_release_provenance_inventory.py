#!/usr/bin/env python3
"""Typed-inventory rejection contract tests for the release provenance gate."""

# Split out of test_release_provenance.py to keep each file's non-comment line
# count under the analyzer's per-file limit. The entry point in
# test_release_provenance.py loads this module through build_suite().

from __future__ import annotations

import json
import unittest

from release_provenance_testlib import (
    COMMIT,
    TAG,
    ReleaseProvenanceTestBase,
    run_tool,
)


class InventoryTypingTests(ReleaseProvenanceTestBase):
    """A claimed required-inventory value must be present and well typed."""

    def test_boolean_inventory_value_fails(self) -> None:
        # A manifest that claims required inventory with `false` is not evidence.
        root = self.fresh("inventory-false")
        manifest = root / "dist" / "KisakCOD-windows-x86-provenance.json"
        data = json.loads(manifest.read_text(encoding="utf-8"))
        data["notices"] = False
        manifest.write_text(json.dumps(data), encoding="utf-8")
        result = self.verify(root)
        self.assertEqual(result.returncode, 1)
        self.assertIn("required inventory", result.stderr)

    def test_zero_inventory_value_fails(self) -> None:
        root = self.fresh("inventory-zero")
        manifest = root / "dist" / "KisakCOD-windows-x86-sp-provenance.json"
        data = json.loads(manifest.read_text(encoding="utf-8"))
        data["symbols"] = 0
        manifest.write_text(json.dumps(data), encoding="utf-8")
        result = self.verify(root)
        self.assertEqual(result.returncode, 1)
        self.assertIn("required inventory", result.stderr)

    def test_empty_object_inventory_value_fails(self) -> None:
        root = self.fresh("inventory-empty-object")
        manifest = root / "dist" / "KisakCOD-windows-x86-nosteam-provenance.json"
        data = json.loads(manifest.read_text(encoding="utf-8"))
        data["runtime_lookup"] = {}
        manifest.write_text(json.dumps(data), encoding="utf-8")
        result = self.verify(root)
        self.assertEqual(result.returncode, 1)
        self.assertIn("required inventory", result.stderr)

    def test_incomplete_dependency_record_fails(self) -> None:
        root = self.fresh("inventory-incomplete-dependency")
        manifest = root / "dist" / "KisakCOD-windows-x86-headless-provenance.json"
        data = json.loads(manifest.read_text(encoding="utf-8"))
        del data["dependencies"][0]["features"]
        manifest.write_text(json.dumps(data), encoding="utf-8")
        result = self.verify(root)
        self.assertEqual(result.returncode, 1)
        self.assertIn("features", result.stderr)

    def test_wrong_typed_inventory_record_fails(self) -> None:
        root = self.fresh("inventory-wrong-type")
        manifest = root / "dist" / "KisakCOD-windows-x86-provenance.json"
        data = json.loads(manifest.read_text(encoding="utf-8"))
        data["dependencies"] = ["SDL2"]
        manifest.write_text(json.dumps(data), encoding="utf-8")
        result = self.verify(root)
        self.assertEqual(result.returncode, 1)
        self.assertIn("required inventory", result.stderr)

    def test_record_rejects_invalid_inventory(self) -> None:
        # The producer fails closed too: a boolean inventory field never reaches
        # a manifest in the first place.
        root = self.tmp / "record-invalid-inventory"
        dist = root / "dist"
        dist.mkdir(parents=True)
        (dist / "KisakCOD-windows-x86.zip").write_bytes(b"artifact")
        inventory = root / "inventory.json"
        inventory.write_text(json.dumps({"toolchain": False}), encoding="utf-8")
        result = run_tool(
            "record",
            "--dist",
            str(dist),
            "--tag",
            TAG,
            "--commit",
            COMMIT,
            "--target",
            "windows-x86",
            "--config",
            "production-mp-dedi",
            "--inventory",
            str(inventory),
            "--out",
            str(root / "manifest.json"),
            "--artifact",
            "KisakCOD-windows-x86.zip",
        )
        self.assertEqual(result.returncode, 1)
        self.assertIn("invalid", result.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)
