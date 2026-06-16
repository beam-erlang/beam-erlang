"""Reversible mutations of acceptance-owned installation copies."""

from contextlib import contextmanager


@contextmanager
def hidden_paths(paths):
    moved = []
    try:
        for path in paths:
            saved = path.with_name(path.name + ".acceptance-hidden")
            if saved.exists() or saved.is_symlink():
                raise AssertionError(f"isolation destination already exists: {saved}")
            path.rename(saved)
            moved.append((path, saved))
        yield
    finally:
        for path, saved in reversed(moved):
            saved.rename(path)


@contextmanager
def hidden(path):
    with hidden_paths([path]):
        yield
