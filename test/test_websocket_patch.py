import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("patch_websockets", ROOT / "scripts/patch_websockets.py")
patcher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patcher)


class WebSocketPatchTests(unittest.TestCase):
    def fixture(self, directory):
        project = Path(directory)
        (project / "patches").mkdir()
        (project / "test/fixtures").mkdir(parents=True)
        library = project / "WebSockets"
        (library / "src").mkdir(parents=True)
        actual = json.loads((ROOT / "patches/websockets-2.7.3-pin.json").read_text())
        callback = (ROOT / "test/fixtures/websockets_connected_pin.inc").read_bytes()
        (project / "test/fixtures/websockets_connected_pin.inc").write_bytes(callback)
        # Exercise exact tracked substitutions on a bounded synthetic source;
        # firmware prebuild independently checks the entire upstream hash.
        original = b"\r\n".join(change["old"].encode() for change in actual["replacements"]) + b"\r\n" + callback
        patched = original
        for change in actual["replacements"]:
            patched = patched.replace(change["old"].encode(), change["new"].encode())
        actual["original_sha256"] = hashlib.sha256(original).hexdigest()
        actual["patched_sha256"] = hashlib.sha256(patched).hexdigest()
        (project / "patches/websockets-2.7.3-pin.json").write_text(json.dumps(actual))
        (library / "library.json").write_text('{"version":"2.7.3"}')
        source = library / "src/WebSocketsClient.cpp"
        source.write_bytes(original)
        return project, library, source, patched

    def test_exact_patch_is_idempotent(self):
        with tempfile.TemporaryDirectory() as directory:
            project, library, source, expected = self.fixture(directory)
            patcher.apply_patch(library, project)
            self.assertEqual(expected, source.read_bytes())
            patcher.apply_patch(library, project)
            self.assertEqual(expected, source.read_bytes())

    def test_source_drift_fails_without_writing(self):
        with tempfile.TemporaryDirectory() as directory:
            project, library, source, _ = self.fixture(directory)
            unexpected = source.read_bytes() + b"unexpected change"
            source.write_bytes(unexpected)
            with self.assertRaisesRegex(RuntimeError, "source drift"):
                patcher.apply_patch(library, project)
            self.assertEqual(unexpected, source.read_bytes())

    def test_version_drift_fails(self):
        with tempfile.TemporaryDirectory() as directory:
            project, library, source, _ = self.fixture(directory)
            (library / "library.json").write_text('{"version":"2.7.4"}')
            with self.assertRaisesRegex(RuntimeError, "Unsupported"):
                patcher.apply_patch(library, project)

    def test_real_callback_fixture_is_required(self):
        with tempfile.TemporaryDirectory() as directory:
            project, library, source, _ = self.fixture(directory)
            (project / "test/fixtures/websockets_connected_pin.inc").write_text("unrelated callback")
            with self.assertRaisesRegex(RuntimeError, "fixture drift"):
                patcher.apply_patch(library, project)


if __name__ == "__main__":
    unittest.main()
