#!/usr/bin/env python3
"""scripts/packaging/third_party_notices.py (ADR-0012, #263) on fake license trees."""

from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts/packaging"))
import third_party_notices as notices  # noqa: E402


class Notices(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.out = self.root / "THIRD-PARTY-NOTICES"

    def licenses(self, **texts):
        directory = self.root / "licenses"
        for component, text in texts.items():
            (directory / component).mkdir(parents=True, exist_ok=True)
            (directory / component / "LICENSE").write_text(text, encoding="utf-8")
        (directory / "SOURCES.txt").write_text("pins, not a component\n", encoding="utf-8")
        return str(directory)

    def run_tool(self, *arguments):
        return notices.main(["--out", str(self.out), *arguments])

    def test_engine_code_is_always_listed(self):
        self.assertEqual(self.run_tool(), 0)
        text = self.out.read_text(encoding="utf-8")
        self.assertIn("Open Dynamics Engine (physics", text)
        self.assertIn("Russell L. Smith", text)
        self.assertIn("Jean-loup Gailly and Mark Adler", text)
        self.assertIn("Copyright (C) 1998 Gilles Vollant", text)
        self.assertIn("Quake III Arena", text)
        self.assertNotIn("Speex", text)  # the dedicated server has no voice codec

    def test_client_package_adds_speex_and_each_bundled_component(self):
        licenses = self.licenses(**{"openal-soft": "LGPL text", "dxvk": "zlib-style text"})
        self.assertEqual(self.run_tool("--client", "--licenses", licenses,
                                       "--component", f"extra={self.out.parent / 'licenses/dxvk/LICENSE'}"), 0)
        text = self.out.read_text(encoding="utf-8")
        for expected in ("Speex 1.1.9 (voice chat codec)", "Xiph.org Foundation",
                         "THE OggVorbis SOURCE CODE", "Technische Universitaet Berlin",
                         "\nopenal-soft\n", "LGPL text", "\ndxvk\n", "\nextra\n"):
            self.assertIn(expected, text)
        self.assertNotIn("SOURCES.txt", text)
        self.assertNotIn("pins, not a component", text)

    def test_empty_license_text_fails(self):
        self.assertEqual(self.run_tool("--licenses", self.licenses(ffmpeg="  \n")), 1)
        self.assertFalse(self.out.exists())

    def test_component_directory_without_text_fails(self):
        licenses = Path(self.licenses(zstd="BSD text"))
        (licenses / "mesa").mkdir()
        self.assertEqual(self.run_tool("--licenses", str(licenses)), 1)

    def test_missing_inputs_fail(self):
        self.assertEqual(self.run_tool("--licenses", str(self.root / "absent")), 1)
        self.assertEqual(self.run_tool("--component", f"x={self.root / 'absent'}"), 1)
        self.assertEqual(self.run_tool("--component", "no-path"), 1)

    def test_engine_license_files_exist_in_the_tree(self):
        for _, _, files, _ in notices.ENGINE:
            for relative in files:
                self.assertTrue(notices.read_text(notices.ROOT / relative))


if __name__ == "__main__":
    unittest.main()
