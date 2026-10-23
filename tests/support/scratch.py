"""Bound disposable test output while preserving compact diagnostics."""
from contextlib import contextmanager
from pathlib import Path
import json
import os
import shutil
import signal
import sys
import tempfile

LIMIT = 8 * 1024 * 1024
FILE_LIMIT = 1024 * 1024


def compact(root, outcome):
    """Keep bounded text evidence; never follow links into selected inputs."""
    root = Path(root)
    if not root.exists():
        return
    if root.is_symlink():
        raise ValueError(f"Scratch root must not be a symlink: {root}")
    retained, omitted, used = [], 0, 0
    with tempfile.TemporaryDirectory(prefix="diagnostics-", dir=root.parent) as temporary:
        evidence = Path(temporary)
        for directory, dirs, files in os.walk(root, followlinks=False):
            dirs[:] = sorted(d for d in dirs if not (Path(directory) / d).is_symlink())
            for name in sorted(files):
                source = Path(directory) / name
                relative = source.relative_to(root)
                if source.is_symlink() or not source.is_file():
                    continue
                diagnostic = (len(relative.parts) == 1 or "logs" in relative.parts)
                if not diagnostic or source.suffix not in {".log", ".json", ".html", ".term", ".txt"}:
                    continue
                size = source.stat().st_size
                # Structured results must remain parseable; only text logs may
                # be truncated. Oversized records are explicitly reported.
                if source.suffix in {".json", ".term"} and size > FILE_LIMIT:
                    omitted += 1
                    continue
                count = min(size, FILE_LIMIT)
                if used + count > LIMIT or len(retained) >= 256:
                    omitted += 1
                    continue
                target = evidence / relative
                target.parent.mkdir(parents=True, exist_ok=True)
                with source.open("rb") as stream:
                    if size > count:
                        stream.seek(size - count)
                    target.write_bytes(stream.read(count))
                retained.append({"path": str(relative), "original_bytes": size,
                                 "retained_bytes": count})
                used += count
        shutil.rmtree(root)
        root.mkdir()
        for entry in evidence.iterdir():
            shutil.move(str(entry), root / entry.name)
    (root / "cleanup.json").write_text(json.dumps({
        "outcome": outcome, "command": sys.argv, "retained": retained,
        "omitted_diagnostics": omitted, "retained_bytes": used,
        "payloads_removed": True}, indent=2) + "\n")


@contextmanager
def scratch(root):
    """Own only a runner's disposable directory, never an acquired prefix."""
    root = Path(root)
    if root.is_symlink():
        raise ValueError(f"Scratch root must not be a symlink: {root}")
    previous = signal.getsignal(signal.SIGTERM)
    def terminate(signum, frame):
        raise SystemExit(128 + signum)
    signal.signal(signal.SIGTERM, terminate)
    outcome = "completed"
    try:
        yield root
    except BaseException as error:
        outcome = f"{type(error).__name__}: {error}"[:8192]
        raise
    finally:
        signal.signal(signal.SIGTERM, previous)
        if os.environ.get("BEAM_TEST_KEEP_WORK") == "1":
            size = sum(p.lstat().st_size for p in root.rglob("*") if p.is_file() and not p.is_symlink())
            print(f"Debug scratch retained: {root} ({size} bytes)", file=sys.stderr)
        else:
            compact(root, outcome)
