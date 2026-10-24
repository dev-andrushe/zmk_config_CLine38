"""Fail closed if any pinned event or macro source changes."""
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
import generate_ctrl_layer8 as generator


class Ctrl8SourceGuardTests(unittest.TestCase):
    def test_changed_sources_fail_before_any_output(self):
        with tempfile.TemporaryDirectory() as temp:
            app, output = Path(temp) / "app", Path(temp) / "output"
            for name in generator.HASHES:
                path = app / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("unsupported version")
            with self.assertRaisesRegex(ValueError, "Unsupported Ctrl8"):
                generator.generate(app, output)
            self.assertFalse(output.exists())
            for name in generator.HASHES:
                self.assertEqual((app / name).read_text(), "unsupported version")


if __name__ == "__main__":
    unittest.main()
