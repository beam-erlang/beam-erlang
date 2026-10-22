#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Build and validate the experimental BEAM application descriptor (ETF v1)."""
import argparse
import hashlib
from pathlib import Path
import re
import struct
import sys


class Atom(str):
    pass


def encode(value):
    if isinstance(value, Atom):
        raw = value.encode('utf-8')
        if len(raw) > 255:
            raise ValueError('oversized atom')
        return b'w' + bytes([len(raw)]) + raw
    if isinstance(value, bytes):
        return b'm' + struct.pack('>I', len(value)) + value
    if isinstance(value, str):
        return encode(value.encode('utf-8'))
    if isinstance(value, int) and 0 <= value <= 0x7fffffff:
        return b'a' + bytes([value]) if value < 256 else b'b' + struct.pack('>I', value)
    if isinstance(value, tuple) and len(value) < 256:
        return b'h' + bytes([len(value)]) + b''.join(map(encode, value))
    if isinstance(value, list):
        return b'l' + struct.pack('>I', len(value)) + b''.join(map(encode, value)) + b'j' if value else b'j'
    raise ValueError('unsupported ETF value')


def decode(data):
    offset = 0
    nodes = 0

    def take(n):
        nonlocal offset
        if n < 0 or offset + n > len(data):
            raise ValueError('truncated ETF')
        result = data[offset:offset+n]
        offset += n
        return result

    def term(depth=0):
        nonlocal nodes
        nodes += 1
        if depth > 32 or nodes > 65536:
            raise ValueError('ETF limit exceeded')
        tag = take(1)
        if tag == b'a':
            return take(1)[0]
        if tag == b'b':
            value = struct.unpack('>i', take(4))[0]
            if value < 0:
                raise ValueError('negative integer')
            return value
        if tag == b'm':
            return take(struct.unpack('>I', take(4))[0])
        if tag == b'w':
            return Atom(take(take(1)[0]).decode('utf-8'))
        if tag == b'h':
            return tuple(term(depth+1) for _ in range(take(1)[0]))
        if tag == b'j':
            return []
        if tag == b'l':
            n = struct.unpack('>I', take(4))[0]
            if n > 4096:
                raise ValueError('oversized list')
            result = [term(depth+1) for _ in range(n)]
            if take(1) != b'j':
                raise ValueError('improper list')
            return result
        raise ValueError('unsupported or compressed ETF')

    if take(1) != b'\x83':
        raise ValueError('missing ETF version')
    result = term()
    if offset != len(data):
        raise ValueError('trailing ETF data')
    return result


def validate(path):
    if not path.is_file() or path.stat().st_size > 2*1024*1024:
        raise ValueError('descriptor exceeds 2 MiB or is not a regular file')
    raw = path.read_bytes()
    if len(raw) > 2*1024*1024:
        raise ValueError('descriptor exceeds 2 MiB')
    d = decode(raw)
    if not isinstance(d, tuple) or len(d) != 12 or d[:2] != (Atom('beam_application'), 1):
        raise ValueError('unsupported application descriptor')
    for text in d[2:5]:
        if not isinstance(text, bytes) or not text or len(text) > 16384 or b'\0' in text:
            raise ValueError('invalid application text')
        text.decode('utf-8')
    def relative(root, text):
        if not isinstance(text, bytes) or not text or len(text) > 16384 or b'\0' in text or b'\\' in text or b':' in text or text.startswith(b'/'):
            raise ValueError('invalid relative application path')
        result = (root / text.decode('utf-8')).resolve()
        if not result.is_relative_to(root):
            raise ValueError('application path escapes its root')
        return result

    root = relative(path.parent.resolve(), d[4])
    if not root.is_relative_to(path.parent.resolve()):
        raise ValueError('application root escapes descriptor')
    entry = d[5]
    if not isinstance(entry, tuple) or len(entry) != 3 or entry[0] not in ('main', 'command', 'shell'):
        raise ValueError('invalid entry')
    def name(value):
        return isinstance(value, bytes) and re.fullmatch(rb'[a-z][A-Za-z0-9_@]{0,254}', value)
    if not name(entry[1]) or not name(entry[2]):
        raise ValueError('invalid entry module or function')
    for index, limit in ((6,16),(7,6),(8,4096),(9,1024),(10,256),(11,4096)):
        if not isinstance(d[index], list) or len(d[index]) > limit:
            raise ValueError('invalid or oversized descriptor list')
    if any(x not in ('application_v1', 'command_v1', 'native_v1') for x in d[6]):
        raise ValueError('unsupported feature')
    if len(set(d[7])) != len(d[7]) or any(x not in ('output','code','file','network','zlib','program') for x in d[7]):
        raise ValueError('invalid services')
    seen = set()
    total = 0
    for record in d[8]:
        if not isinstance(record, tuple) or len(record) != 5:
            raise ValueError('invalid module record')
        kind, module, location, size, digest = record
        if kind != 'file' or module in seen or not name(module):
            raise ValueError('invalid or duplicate module')
        seen.add(module)
        file = relative(root, location)
        if not isinstance(digest, bytes) or len(digest) != 32:
            raise ValueError('invalid module path/digest')
        if not isinstance(size, int) or size < 0 or size > 256*1024*1024-total or not file.is_file() or file.stat().st_size != size:
            raise ValueError('invalid module size or type')
        data = file.read_bytes()
        total += len(data)
        if total > 256*1024*1024 or len(seen) > 4096 or len(data) != size or hashlib.sha256(data).digest() != digest:
            raise ValueError('module integrity failure')
    if entry[0] != 'shell' and entry[1] not in seen:
        raise ValueError('entry module absent')
    bindings = set()
    for module, location in d[9]:
        file = relative(root, location)
        if module not in seen or not file.is_relative_to(root) or not file.is_file():
            raise ValueError('invalid native binding')
        if module in bindings:
            raise ValueError('duplicate native binding')
        bindings.add(module)
    if any(not re.fullmatch(rb'[0-9a-f]{64}', x) for x in d[10]):
        raise ValueError('invalid runtime build identity')
    if any(not name(x) for x in d[11]) or len(set(d[11])) != len(d[11]) or set(d[11]) & seen:
        raise ValueError('invalid or conflicting host requirements')
    return d


def main():
    p = argparse.ArgumentParser(description=__doc__)
    sub = p.add_subparsers(dest='command', required=True)
    check = sub.add_parser('validate')
    check.add_argument('descriptor', type=Path)
    create = sub.add_parser('create')
    create.add_argument('--root', type=Path, required=True)
    create.add_argument('--inventory', type=Path, required=True)
    create.add_argument('--entry', required=True)
    create.add_argument('--contract', choices=['main','command','shell'], default='main')
    create.add_argument('--name', required=True)
    create.add_argument('--version', required=True)
    create.add_argument('--runtime-build', action='append', default=[])
    create.add_argument('--service', action='append', default=[])
    create.add_argument('--require-module', action='append', default=[])
    create.add_argument('--native', action='append', default=[], metavar='MODULE=RELATIVE_PATH')
    create.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    if a.command == 'validate':
        validate(a.descriptor)
        return
    root = a.root.resolve()
    if a.output.resolve().parent != root:
        raise ValueError('descriptor must be generated at application root')
    entries = []
    for name in a.inventory.read_text().splitlines():
        if not re.fullmatch(r'[a-z][A-Za-z0-9_@]*', name):
            raise ValueError('invalid module name')
        relative = f'ebin/{name}.beam'
        data = (root / relative).read_bytes()
        entries.append((Atom('file'), name, relative, len(data), hashlib.sha256(data).digest()))
    module, _, function = a.entry.partition(':')
    features = [Atom('application_v1')]
    if a.contract == 'command':
        features.append(Atom('command_v1'))
    native = []
    for binding in a.native:
        module_name, separator, location = binding.partition('=')
        if not separator:
            raise ValueError('native bindings use MODULE=RELATIVE_PATH')
        native.append((module_name, location))
    if native:
        features.append(Atom('native_v1'))
    descriptor = (Atom('beam_application'), 1, a.name, a.version, '.',
                  (Atom(a.contract), module, function or a.contract), features,
                  [Atom(s) for s in a.service], entries, native, a.runtime_build, a.require_module)
    a.output.write_bytes(b'\x83' + encode(descriptor))
    validate(a.output)


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, TypeError, UnicodeError) as error:
        print(f'beam-application: {error}', file=sys.stderr)
        sys.exit(1)
