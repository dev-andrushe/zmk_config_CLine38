"""Unsupported upstream versions must stop before any generated file is written."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location(
    "generator", Path(__file__).resolve().parents[1] / "scripts/generate_retro_pointer.py")
generator = importlib.util.module_from_spec(spec)
spec.loader.exec_module(generator)


class SourceGuardTests(unittest.TestCase):
    def test_changed_upstream_is_rejected_without_modification(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            app, output = root / "app", root / "generated"
            for relative in generator.HASHES:
                path = app / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("unexpected upstream revision\n")
            with self.assertRaisesRegex(ValueError, "Unsupported ZMK source"):
                generator.generate(app, output)
            self.assertFalse(output.exists())
            for relative in generator.HASHES:
                self.assertEqual((app / relative).read_text(), "unexpected upstream revision\n")

    def test_missing_or_duplicate_patch_anchor_is_rejected(self):
        for text in ("other", "anchor anchor"):
            with self.assertRaisesRegex(ValueError, "Expected one upstream anchor"):
                generator.replace_once(text, "anchor", "hook")


if __name__ == "__main__":
    unittest.main()
