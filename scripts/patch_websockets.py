"""Narrow, hash-checked WebSockets 2.7.3 ESP32 pinned-handshake correction."""
from pathlib import Path
import hashlib
import json


def apply_patch(root, project=None):
    root = Path(root)
    project = Path(project) if project else Path(__file__).resolve().parents[1]
    spec = json.loads((project / "patches/websockets-2.7.3-pin.json").read_text())
    metadata = json.loads((root / "library.json").read_text())
    if metadata["version"] != spec["version"]:
        raise RuntimeError("Unsupported WebSockets version; review pin patch")
    source = root / "src/WebSocketsClient.cpp"
    original = source.read_bytes()
    fixture = (project / "test/fixtures/websockets_connected_pin.inc").read_text().strip()
    if fixture not in original.decode().replace("\r\n", "\n"):
        raise RuntimeError("Pinned WebSockets callback fixture drift")
    digest = hashlib.sha256(original).hexdigest()
    if digest == spec["patched_sha256"]:
        return
    if digest != spec["original_sha256"]:
        raise RuntimeError("WebSockets source drift; review pin patch")
    patched = original
    for change in spec["replacements"]:
        if patched.count(change["old"].encode()) != 1:
            raise RuntimeError("Ambiguous WebSockets patch anchor")
        patched = patched.replace(change["old"].encode(), change["new"].encode())
    if hashlib.sha256(patched).hexdigest() != spec["patched_sha256"]:
        raise RuntimeError("Invalid WebSockets patch result")
    source.write_bytes(patched)


# Post extra script runs after dependency discovery, before compilation.
if "Import" in globals():
    Import("env")
    apply_patch(Path(env.subst("$PROJECT_LIBDEPS_DIR")) / env.subst("$PIOENV") / "WebSockets", env.subst("$PROJECT_DIR"))
