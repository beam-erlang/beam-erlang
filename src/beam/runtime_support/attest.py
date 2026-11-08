#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Bind ordinary runtime artifacts to their sources and verified build compiler."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def input_digests(paths, root):
    inputs = {}
    for path in paths:
        relative = str(path.relative_to(root))
        if relative in inputs:
            raise SystemExit(f'duplicate attested input: {relative}')
        inputs[relative] = digest(path)
    return inputs


def direct_input_digests(paths, root):
    inputs = input_digests(paths, root)
    modules = set()
    declaration = re.compile(r'^-module\(\s*([a-z][a-zA-Z0-9_]*)\s*\)\.', re.M)
    for path in paths:
        leading = declaration.search(path.read_text(encoding='utf-8'))
        if path.suffix != '.erl' or leading is None or leading.group(1) != path.stem:
            raise SystemExit(f'direct runtime module does not match source filename: {path}')
        if path.stem in modules:
            raise SystemExit(f'duplicate direct runtime module: {path.stem}')
        modules.add(path.stem)
    return inputs


def write_record(path, record):
    path.write_text(json.dumps(record, indent=2, sort_keys=True) + '\n')



def native_metadata(data):
    """Read compiler metadata without executing Erlang code."""
    offset = 0

    def take(count):
        nonlocal offset
        result = data[offset:offset + count]
        if len(result) != count:
            raise ValueError('truncated native metadata')
        offset += count
        return result

    def integer(width):
        return int.from_bytes(take(width), 'big')

    def term():
        tag = integer(1)
        if tag == 97:
            return integer(1)
        if tag == 98:
            return int.from_bytes(take(4), 'big', signed=True)
        if tag in (100, 118, 115, 119):
            size = integer(1 if tag in (115, 119) else 2)
            return take(size).decode('utf-8' if tag in (118, 119) else 'latin1')
        if tag == 106:
            return []
        if tag in (104, 105):
            return tuple(term() for _ in range(integer(1 if tag == 104 else 4)))
        if tag == 108:
            values = [term() for _ in range(integer(4))]
            if term() != []:
                raise ValueError('improper native metadata list')
            return values
        if tag in (107, 109):
            return take(integer(2 if tag == 107 else 4))
        raise ValueError(f'unsupported native metadata tag {tag}')

    if not data or integer(1) != 131:
        raise ValueError('invalid native metadata version')
    result = dict(term())
    if offset != len(data) or result.get('beam_native_version') != 1:
        raise ValueError('invalid native requirements')
    return result


def native_origins():
    root = Path(__file__).resolve().parents[3]
    catalog = root / 'src/erts/emulator/beam/beam_native_contracts.def'
    origins = {(module, function, int(arity)): identifier
               for identifier, module, function, arity in re.findall(
                r'BEAM_NATIVE_CONTRACT\("([^"]+)","([^"]+)","([^"]+)",(\d+),[01]\)',
                catalog.read_text())}
    instructions = root / 'src/erts/emulator/beam/beam_native_instructions.def'
    origins.update({('erlang', function, int(arity)): identifier
                    for identifier, function, arity in re.findall(
                        r'BEAM_NATIVE_INSTRUCTION\((\w+),\s*(\w+),\s*(\d+),',
                        instructions.read_text())})
    return origins


STDLIB_MODULES = frozenset(('array', 'dict', 'graph', 'gb_sets', 'gb_trees',
                          'orddict', 'ordsets', 'proplists', 'queue', 'sets',
                          'sofs', 'digraph', 'digraph_utils'))
STDLIB_PROVIDERS = STDLIB_MODULES | {'beam', 'beam_lists', 'beam_maps', 'beam_math', 'beam_ets'}


def audit_imports(path, policy='runtime'):
    """Reject executable facade dependencies, including calls introduced by lowering."""
    data=path.read_bytes()
    if data[:4]!=b'FOR1' or data[8:12]!=b'BEAM':
        raise SystemExit(f'invalid runtime bytecode: {path}')
    chunks={}
    offset=12
    while offset<len(data):
        size=struct.unpack_from('>I',data,offset+4)[0]
        chunks[data[offset:offset+4]]=data[offset+8:offset+8+size]
        offset += 8+(size+3)//4*4
    atoms=chunks[b'AtU8']
    count=struct.unpack_from('>i',atoms)[0]
    offset=4
    names=['']
    for _ in range(abs(count)):
        first=atoms[offset];offset+=1
        if count>=0:
            length=first
        elif first&7:
            raise SystemExit(f'invalid atom length tag: {path}')
        elif not first&8:
            length=first>>4
        elif not first&16:
            length=((first&0xe0)<<3)|atoms[offset];offset+=1
        else:
            # Valid UTF-8 atom names have at most 1020 bytes.
            raise SystemExit(f'overlong projected atom: {path}')
        names.append(atoms[offset:offset+length].decode('utf-8'))
        offset+=length
    imports=chunks[b'ImpT']
    count=struct.unpack_from('>I',imports)[0]
    forbidden={'ets','binary','lists','maps','string','unicode','unicode_util','records',
               'atomics','counters','persistent_term','math','erl_ddll','erl_debugger',
               'dict','gb_trees','queue','sets','proplists','orddict'}
    migrated_time = {
        ("monotonic_time", 0),
        ("monotonic_time", 1),
        ("system_time", 0),
        ("system_time", 1),
        ("time_offset", 0),
        ("time_offset", 1),
        ("timestamp", 0),
        ("start_timer", 3),
        ("start_timer", 4),
        ("send_after", 3),
        ("send_after", 4),
        ("cancel_timer", 1),
        ("cancel_timer", 2),
        ("read_timer", 1),
        ("read_timer", 2),
        ("now", 0),
        ("convert_time_unit", 3),
    }
    origins = native_origins()
    metadata = native_metadata(chunks[b'Meta']) if b'Meta' in chunks else {}
    if policy == 'stdlib' and metadata.get('beam_native_slots', []):
        raise SystemExit(f'standard-library module has native slots: {path.name}')
    private = dict(metadata.get('beam_private_natives', []))
    selections = dict(metadata.get('beam_native_selections', []))
    for i in range(count):
        module,function,arity=struct.unpack_from('>III',imports,4+12*i)
        mfa = names[module], names[function], arity
        if i in private and private[i] == origins.get(mfa):
            continue
        # These imports become load-time booleans. They neither call a facade
        # nor acquire its native bindings for the evaluator.
        if i in selections and selections[i] == origins.get(mfa):
            continue
        if policy == 'stdlib':
            if names[module] in STDLIB_PROVIDERS:
                continue
            # Compiler-generated inspection is not a public facade dependency.
            if mfa in {('erlang', 'get_module_info', 1), ('erlang', 'get_module_info', 2)}:
                continue
            # Map generators use these ordinary helpers in the embedded core.
            if mfa in {('erts_internal', 'mc_iterator', 1), ('erts_internal', 'mc_refill', 1)}:
                continue
            raise SystemExit(f'unresolved standard-library import in {path.name}: '
                             f'{names[module]}:{names[function]}/{arity}')
        if names[module] in forbidden or (names[module] == 'erlang' and
                ((names[function], arity) in migrated_time or mfa in origins)):
            raise SystemExit(f'unresolved runtime import in {path.name}: '
                             f'{names[module]}:{names[function]}/{arity}')

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('root', 'compiler', 'sdk', 'output', 'beam-directory'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--resources', type=Path, nargs='*', default=[])
    parser.add_argument('--import-policy', choices=('runtime', 'stdlib'), default='runtime')
    sources = parser.add_mutually_exclusive_group(required=True)
    sources.add_argument('--sources', type=Path, nargs='+')
    sources.add_argument('--direct-sources', type=Path, nargs='+')
    snapshot = parser.add_mutually_exclusive_group()
    snapshot.add_argument('--capture-inputs', action='store_true')
    snapshot.add_argument('--input-snapshot', type=Path)
    args = parser.parse_args()
    if args.direct_sources:
        if not args.capture_inputs and args.input_snapshot is None:
            parser.error('--direct-sources requires --capture-inputs or --input-snapshot')
    elif args.capture_inputs or args.input_snapshot is not None:
        parser.error('input snapshots require --direct-sources')
    if args.import_policy == 'stdlib' and not args.direct_sources:
        parser.error('standard-library import policy requires --direct-sources')
    sdk = dict(re.findall(r'^([\w-]+)\s*=\s*"([^"]*)"', args.sdk.read_text(), re.M))
    record = {'format': 1, 'sdk': sdk, 'compiler_sha256': digest(args.compiler),
              'seed_manifest_sha256': digest(args.root / 'src/beam/compiler/seeds/manifest.json'),
              'native_contracts_sha256': digest(args.root / 'src/erts/emulator/beam/beam_native_contracts.def'),
              'native_instructions_sha256': digest(args.root / 'src/erts/emulator/beam/beam_native_instructions.def'),
              'resources': input_digests(args.resources, args.root)}
    if args.direct_sources:
        record.update(source_mode='direct', sdk_sha256=digest(args.sdk),
                      verifier_sha256=digest(Path(__file__)),
                      inputs=direct_input_digests(args.direct_sources, args.root))
        if args.import_policy == 'stdlib':
            if {p.stem for p in args.direct_sources} != STDLIB_MODULES:
                raise SystemExit('standard-library policy requires the complete thirteen-module inventory')
            record['import_policy'] = 'stdlib'
        if args.capture_inputs:
            write_record(args.output, record)
            return
        if json.loads(args.input_snapshot.read_text()) != record:
            raise SystemExit('direct runtime inputs changed since capture; recompile from matching inputs')
        record['modules'] = []
        for source in args.direct_sources:
            beam = args.beam_directory / (source.stem + '.beam')
            audit_imports(beam, args.import_policy)
            relative = str(source.relative_to(args.root))
            record['modules'].append({'source': relative,
                                      'source_sha256': record['inputs'][relative],
                                      'module': source.stem,
                                      'bytecode_sha256': digest(beam)})
        write_record(args.output, record)
        return
    record['projection_inputs'] = {name: digest(Path(__file__).parent / name)
                                   for name in ('beam_runtime_projection.erl', 'stage.py', 'attest.py')}
    record['modules'] = []
    for staged in args.sources:
        receipt = json.loads(staged.with_suffix('.source.json').read_text())
        original = Path(receipt['source'])
        if digest(original) != receipt['source_sha256'] or digest(staged) != receipt['stage_sha256']:
            raise SystemExit(f'projection inputs changed while compiling {staged.name}')
        beam = args.beam_directory / (staged.stem + '.beam')
        audit_imports(beam)
        try:
            source_name = str(original.relative_to(args.root))
        except ValueError:
            # Explicit generated inputs may live in an out-of-source build.
            # Their staging receipts and hashes still bind the selected files.
            source_name = str(original)
        module = {'source': source_name,
                  'source_sha256': receipt['source_sha256'],
                  'module': staged.stem, 'bytecode_sha256': digest(beam)}
        if 'withheld_exports' in receipt:
            module['withheld_exports'] = receipt['withheld_exports']
        record['modules'].append(module)
    write_record(args.output, record)


if __name__ == '__main__':
    main()
