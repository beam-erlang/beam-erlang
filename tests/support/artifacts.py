"""Resolve installed native artifacts without assuming ELF filenames."""
import hashlib
import json
from pathlib import Path


def relative(prefix, name):
    prefix=Path(prefix)
    record=json.loads((prefix/'lib/beam/runtime-artifacts.json').read_text())['artifacts'][name]
    path=Path(record['path'])
    if path.is_absolute() or '..' in path.parts or not path.parts:
        raise AssertionError(f'invalid installed artifact path: {name}')
    return path.as_posix()


def artifact(prefix,name):
    return Path(prefix)/relative(prefix,name)


def native_executable(path):
    magic=Path(path).read_bytes()[:4]
    # Native ELF, thin Mach-O and universal Mach-O executables.
    return magic in (b'\x7fELF',b'\xcf\xfa\xed\xfe',b'\xfe\xed\xfa\xcf',b'\xca\xfe\xba\xbe',b'\xbe\xba\xfe\xca')
