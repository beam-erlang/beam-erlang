"""Compiler seed reproduction and rejection of unattested bootstrap inputs."""

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import runpy
import re
import subprocess
import sys
import tempfile


def beam_chunks(path):
    data = path.read_bytes()
    chunks, offset = {}, 12
    while offset < len(data):
        size = struct.unpack_from('>I', data, offset + 4)[0]
        chunks[data[offset:offset + 4]] = data[offset + 8:offset + 8 + size]
        offset += 8 + (size + 3) // 4 * 4
    return chunks


def beam_table(path, table):
    """Read a named bytecode MFA table independently of runtime module loading."""
    chunks = beam_chunks(path)
    atoms = chunks[b'AtU8']
    count = struct.unpack_from('>i', atoms)[0]
    names, offset = [''], 4
    for _ in range(abs(count)):
        first = atoms[offset]
        offset += 1
        if count >= 0:
            length = first
        elif not first & 8:
            assert not first & 7
            length = first >> 4
        else:
            assert not first & 23
            length = ((first & 0xe0) << 3) | atoms[offset]
            offset += 1
        names.append(atoms[offset:offset + length].decode('utf-8'))
        offset += length
    entries = struct.iter_unpack('>III', chunks[table][4:])
    if table == b'ExpT':
        return {(names[function], arity) for function, arity, _ in entries}
    return {(names[module], names[function], arity) for module, function, arity in entries}


def beam_exports(path):
    return beam_table(path, b'ExpT')


def tracing_artifacts(source, build):
    """Ordinary native slots and delivery callbacks have separate inventories."""
    public = {('session_create',3), ('session_destroy',1), ('session_info',1),
              ('process',4), ('port',4), ('function',4), ('send',3), ('recv',3),
              ('info',3), ('delivered',2), ('system',3), ('set_token',1),
              ('set_token',2), ('get_token',0), ('get_token',1), ('print',1),
              ('print',2), ('reset_trace',0), ('set_system_tracer',1),
              ('get_system_tracer',0)}
    slots = {('trace_session_create',3), ('trace_session_destroy',1), ('trace',4),
             ('trace_pattern',4), ('trace_info',3), ('trace_delivered',1),
             ('seq_trace',2), ('seq_trace_info',1), ('seq_trace_print',1),
             ('seq_trace_print',2)}
    callbacks = {('enabled',3), ('trace',5)}
    generated = {('module_info',0), ('module_info',1)}
    path = build/'components/tracing/ebin/beam_trace.beam'
    metadata = runpy.run_path(str(source/'src/beam/runtime_support/attest.py'))['native_metadata']
    assert beam_exports(path) == public | slots | callbacks | {('load_nif',0)} | generated
    assert set(metadata(beam_chunks(path)[b'Meta'])['beam_native_slots']) == slots
    imports = beam_table(path, b'ImpT')
    assert {('beam_trace',f,a) for f,a in slots} <= imports, 'native stubs were inlined'
    assert ('erts_internal','system_monitor',3) in imports
    assert not any(module == 'beam_internal' for module,_,_ in imports)
    cleaner = source/'src/beam/core/ebin/beam_internal.beam'
    assert beam_exports(cleaner) == generated | {('trace_cleaner_start',0),
            ('trace_cleaner_check',0), ('trace_cleaner_send_signal',1),
            ('module_soft_purge',1)}
    private = metadata(beam_chunks(cleaner)[b'Meta'])['beam_private_natives']
    cleaner_ids = {identifier for _,identifier in private if 'trace_cleaner' in identifier}
    assert cleaner_ids == {'vm_erts_trace_cleaner_check_0',
                           'vm_erts_trace_cleaner_send_trace_clean_signal_1'}


def native_catalog(source):
    """Reconcile execution contracts, compiler facts and product selections."""
    emulator = source / 'src/erts/emulator/beam'
    declarations = (emulator / 'bif.tab').read_text()
    assert not re.search(r'^(?:bif|ubif|hbif)\s', declarations, re.M), \
        'fixed public native registration remains'
    rows = re.findall(r'^optional-(bif|ubif|hbif)\s+(\w+)/(\d+)\s+(\w+)\s*$',
                      declarations, re.M)
    contracts = {name + '_' + arity: (int(arity), kind, entry)
                 for kind, name, arity, entry in rows}
    assert len(contracts) == len(rows), 'duplicate implementation identifier'
    instructions = re.findall(
        r'BEAM_NATIVE_INSTRUCTION\((\w+),\s*(\w+),\s*(\d+),\s*(\w+),\s*([01])\)',
        (emulator / 'beam_native_instructions.def').read_text())
    for identifier, _, arity, opcode, _ in instructions:
        assert identifier not in contracts
        contracts[identifier] = (int(arity), 'instruction', opcode)
    facts = re.findall(
        r"^native_contract\('([^']+)'\) -> \{'([^']+)','([^']+)',(\d+),(true|false)\};$",
        (source / 'src/beam/compiler/passes/beam_native_contracts.hrl').read_text(), re.M)
    compiler = {identifier: (module, function, int(arity), guard == 'true')
                for identifier, module, function, arity, guard in facts}
    assert len(compiler) == len(facts)
    assert compiler.keys() == contracts.keys(), 'compiler and VM catalogs differ'
    for identifier, (_, _, arity, eligible) in compiler.items():
        assert contracts[identifier][0] == arity
        if eligible:
            assert contracts[identifier][1] == 'ubif', identifier
    origins = re.findall(
        r'BEAM_NATIVE_CONTRACT\("([^"]+)","([^"]+)","([^"]+)",(\d+),([01])\)',
        (emulator / 'beam_native_contracts.def').read_text())
    migrated = set()
    for identifier, module, function, arity, guard in origins:
        assert identifier not in migrated
        migrated.add(identifier)
        assert compiler[identifier] == (module, function, int(arity), guard == '1')
    migrated.update(identifier for identifier, *_ in instructions)
    tracing_ids = {"vm_erts_internal_trace_session_create_3", "vm_erts_internal_trace_session_destroy_1",
                   "vm_erts_internal_trace_4", "vm_erts_internal_trace_pattern_4", "vm_erts_internal_trace_info_3",
                   "vm_trace_delivered_1", "vm_seq_trace_2", "vm_seq_trace_info_1",
                   "vm_seq_trace_print_1", "vm_seq_trace_print_2"}
    withheld = {"vm_trace_info_2", "vm_erts_trace_cleaner_check_0",
                "vm_erts_trace_cleaner_send_trace_clean_signal_1"}
    for relative, modern in [('src/beam/modern_native_bindings.def', True),
                             ('src/beam/compiler/native_bindings.def', False)]:
        bindings = re.findall(
            r'\{sizeof\(beam_builtin_binding\),\s*"([^"]+)",\s*"([^"]+)",\s*"([^"]+)",\s*(\d+)\}',
            (source / relative).read_text())
        ids, destinations = set(), set()
        for identifier, module, function, arity in bindings:
            destination = (module, function, int(arity))
            assert identifier in contracts and identifier not in ids
            assert destination not in destinations
            assert int(arity) == contracts[identifier][0]
            ids.add(identifier)
            destinations.add(destination)
            if identifier in migrated:
                origin_module, origin_function, origin_arity, _ = compiler[identifier]
                expected_module = ('beam_trace' if identifier in tracing_ids else
                                   'beam' if origin_module == 'erlang' else
                                   'beam_' + origin_module if origin_module in
                                   {'binary','string','unicode','records','maps','lists','math'} else
                                   {'erl_ddll':'beam_ddll','erl_debugger':'beam_debugger'}.get(origin_module,origin_module)) if modern else origin_module
                assert destination == (expected_module, origin_function, origin_arity)
        assert (migrated - withheld if modern else migrated) <= ids, f'incomplete product selection: {relative}'
    dirty = re.findall(r'^dirty-(?:cpu|io|cpu-test|io-test)\s+(\S+)\s*$',
                       (emulator / 'erl_dirty_bif.tab').read_text(), re.M)
    assert set(dirty) <= contracts.keys(), 'dirty annotations must use descriptor IDs'
    traits = re.findall(r'BEAM_NATIVE_TRAITS\((\w+),',
                        (emulator / 'beam_native_traits.def').read_text())
    assert set(traits) <= contracts.keys()


def native_generation(source, work):
    """Calling class and scheduler policy belong to contracts, not public names."""
    with tempfile.TemporaryDirectory(prefix='native-table-', dir=work) as temporary:
        fixture = Path(temporary)
        atoms = fixture / 'atoms'
        atoms.write_text('atom erlang\n')
        declarations = fixture / 'native.tab'
        declarations.write_text(
            'optional-bif regular/1 regular_1\n'
            'optional-ubif guard/1 guard_1\n'
            'optional-hbif heavy/1 heavy_1\n'
            'optional-bif test/1 test_1\n')
        dirty = fixture / 'dirty.tab'
        dirty.write_text('dirty-cpu regular_1\ndirty-io heavy_1\n'
                         'dirty-cpu guard_1\ndirty-cpu-test test_1\n')
        generator = source / 'src/erts/emulator/utils/make_tables'
        for mode in ('no', 'yes'):
            output = fixture / mode
            (output / 'src').mkdir(parents=True)
            (output / 'include').mkdir()
            result = subprocess.run(
                ['perl', str(generator), '-src', str(output / 'src'),
                 '-include', str(output / 'include'), '-dst', mode, '-jit', 'no',
                 str(atoms), str(dirty), str(declarations)],
                capture_output=True, text=True, timeout=20)
            assert result.returncode == 0, result.stdout + result.stderr
            table = (output / 'src/erl_bif_table.c').read_text()
            contracts = re.findall(
                r'\{"(regular_1|guard_1|heavy_1|test_1)", "", "", 1, '
                r'(BIF_KIND_\w+), "(\w+)"\}', table)
            assert dict((identifier, (kind, scheduler))
                        for identifier, kind, scheduler in contracts) == {
                'regular_1': ('BIF_KIND_REGULAR', 'dirty_cpu'),
                'guard_1': ('BIF_KIND_GUARD', 'normal'),
                'heavy_1': ('BIF_KIND_HEAVY', 'dirty_io'),
                'test_1': ('BIF_KIND_REGULAR', 'dirty_cpu' if mode == 'yes' else 'normal')}
            guards = (output / 'src/erl_guard_bifs.c').read_text()
            assert '{guard_1, BIF_guard_1}' in guards
            wrappers = (output / 'src/erl_dirty_bif_wrap.c').read_text()
            assert 'ERTS_SCHED_DIRTY_CPU' in wrappers
            assert 'ERTS_SCHED_DIRTY_IO' in wrappers
            assert 'schedule_dirty_cpu_guard_1' not in wrappers

        dirty.write_text('dirty-cpu erlang:regular/1\n')
        result = subprocess.run(
            ['perl', str(generator), '-src', str(output / 'src'),
             '-include', str(output / 'include'), '-dst', 'no', '-jit', 'no',
             str(atoms), str(dirty), str(declarations)],
            capture_output=True, text=True, timeout=20)
        assert result.returncode != 0 and 'invalid BIF implementation' in result.stderr

        # Preloading must retain the same checksum-covered binding metadata
        # that the ordinary loader consumes, even when debug chunks are stripped.
        core = source / 'src/erts/preloaded/ebin/erts_dirty_process_signal_handler.beam'
        generated = subprocess.run(
            ['perl', str(source / 'src/erts/emulator/utils/make_preload'), str(core)],
            capture_output=True, text=True, timeout=20)
        assert generated.returncode == 0, generated.stderr
        code = '\n'.join(line.split('/*', 1)[0] for line in generated.stdout.splitlines())
        embedded = bytes(int(value, 16) for value in re.findall(r'0x([0-9a-f]{2}),', code))

        def chunks(data):
            result, offset = {}, 12
            assert data[:4] == b'FOR1' and data[8:12] == b'BEAM'
            while offset < len(data):
                size = struct.unpack_from('>I', data, offset + 4)[0]
                result[data[offset:offset + 4]] = data[offset + 8:offset + 8 + size]
                offset += 8 + (size + 3) // 4 * 4
            return result

        original, stripped = chunks(core.read_bytes()), chunks(embedded)
        assert original[b'Meta'] == stripped[b'Meta']
        for name, data in stripped.items():
            assert original[name] == data, name


def adopted_base_apis(source, build):
    """The adopted math, driver and debugger APIs include ordinary exports."""
    apis = {
        'beam_math': ({(name, 1) for name in
                       ('acos', 'acosh', 'asin', 'asinh', 'atan', 'atanh', 'ceil',
                        'cos', 'cosh', 'erf', 'erfc', 'exp', 'floor', 'log',
                        'log10', 'log2', 'sin', 'sinh', 'sqrt', 'tan', 'tanh')} |
                      {('atan2', 2), ('fmod', 2), ('pow', 2), ('pi', 0), ('tau', 0)},
                      {('pi', 0), ('tau', 0)}),
        'beam_ddll': ({('demonitor', 1), ('format_error', 1), ('format_error_int', 1),
                      ('info', 0), ('info', 1), ('info', 2), ('load', 2),
                      ('load_driver', 2), ('loaded_drivers', 0), ('monitor', 2),
                      ('reload', 2), ('reload_driver', 2), ('start', 0), ('stop', 0),
                      ('try_load', 3), ('try_unload', 2), ('unload', 1),
                      ('unload_driver', 1)},
                      {('format_error', 1), ('info', 0), ('info', 1), ('load', 2),
                       ('load_driver', 2), ('reload', 2), ('reload_driver', 2),
                       ('start', 0), ('stop', 0), ('unload', 1), ('unload_driver', 1)}),
        'beam_debugger': ({('breakpoint', 3), ('breakpoints', 1), ('breakpoints', 3),
                          ('instrumentations', 0), ('peek_stack_frame_slot', 4),
                          ('peek_xreg', 3), ('register', 1), ('stack_frames', 2),
                          ('supported', 0), ('toggle_instrumentations', 1),
                          ('unregister', 2), ('whereis', 0), ('xregs_count', 1)}, set()),
    }
    generated = {('module_info', 0), ('module_info', 1)}
    metadata = runpy.run_path(str(source/'src/beam/runtime_support/attest.py'))['native_metadata']
    for module, (expected, ordinary) in apis.items():
        path = build/'lib/beam/library/ebin'/(module+'.beam')
        assert beam_exports(path) == expected | generated, module
        imports = beam_table(path, b'ImpT')
        assert set(metadata(beam_chunks(path)[b'Meta'])['beam_native_slots']) == expected - ordinary, module
        assert not any(m in {'math', 'erl_ddll', 'erl_debugger'} for m, _, _ in imports), module


def direct_library_artifacts(source, build, work, compiler):
    """Direct libraries retain native contracts and reject changed compile inputs."""
    modules = {'beam_binary', 'beam_lists', 'beam_maps', 'beam_records',
               'beam_string', 'beam_unicode', 'beam_math', 'beam_ddll', 'beam_debugger'}
    script = source/'src/beam/runtime_support/attest.py'
    verifier = runpy.run_path(str(script))
    receipt = json.loads((build/'runtime-support/attestations/beam_direct_library_modules.json').read_text())
    assert receipt['source_mode'] == 'direct' and 'projection_inputs' not in receipt
    assert {entry['module'] for entry in receipt['modules']} == modules
    header = Path('src/beam/library/include/swar_ascii.hrl')
    assert receipt['resources'] == {str(header): hashlib.sha256((source/header).read_bytes()).hexdigest()}
    projected = json.loads((build/'runtime-support/attestations/beam_public_library_modules.json').read_text())
    assert modules.isdisjoint(entry['module'] for entry in projected['modules'])
    adopted_base_apis(source, build)
    bindings = re.findall(
        r'\{sizeof\(beam_builtin_binding\),\s*"[^"]+",\s*"([^"]+)",\s*"([^"]+)",\s*(\d+)\}',
        (source/'src/beam/modern_native_bindings.def').read_text())
    for entry in receipt['modules']:
        path = source/'src/beam/library'/(entry['module']+'.erl')
        assert entry['source'] == str(path.relative_to(source))
        assert hashlib.sha256(path.read_bytes()).hexdigest() == entry['source_sha256']
        text = path.read_text()
        assert re.search(r'^-module\('+entry['module']+r'\)\.', text, re.M)
        assert not re.search(r'^-compile\([^\n]*(?:parse_transform|core_transform)', text, re.M)
        assert '-compile(beam_modern_map_comprehensions).' in text
        beam = build/'lib/beam/library/ebin'/(entry['module']+'.beam')
        assert hashlib.sha256(beam.read_bytes()).hexdigest() == entry['bytecode_sha256']
        verifier['audit_imports'](beam)
        metadata = verifier['native_metadata'](beam_chunks(beam)[b'Meta'])
        expected = {(function, int(arity)) for module, function, arity in bindings
                    if module == entry['module']}
        assert set(metadata['beam_native_slots']) == expected, entry['module']

    # Mandatory build verification owns full base compilation. These private
    # copies exercise the independent attestation boundary and its failure paths.
    with tempfile.TemporaryDirectory(prefix='direct-library-', dir=work) as temporary:
        fixture = Path(temporary)
        inputs, outputs = fixture/'source', fixture/'ebin'
        outputs.mkdir()
        relative_sources = [Path(entry['source']) for entry in receipt['modules']]
        contracts = Path('src/erts/emulator/beam/beam_native_contracts.def')
        instructions = Path('src/erts/emulator/beam/beam_native_instructions.def')
        manifest = Path('src/beam/compiler/seeds/manifest.json')
        for relative in [*relative_sources, header, contracts, instructions, manifest]:
            target = inputs/relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source/relative, target)
        for module in modules:
            shutil.copyfile(build/'lib/beam/library/ebin'/(module+'.beam'), outputs/(module+'.beam'))
        compiler_copy = fixture/'compiler'
        shutil.copyfile(compiler, compiler_copy)
        sdk = fixture/'sdk.toml'
        shutil.copyfile(build/'sdk.toml', sdk)
        snapshot, final = fixture/'inputs.json', fixture/'receipt.json'
        command = [sys.executable, str(script), '--root', str(inputs),
                   '--compiler', str(compiler_copy), '--sdk', str(sdk),
                   '--beam-directory', str(outputs), '--resources', str(inputs/header),
                   '--direct-sources', *[str(inputs/path) for path in relative_sources]]

        def attest(arguments, success, diagnostic=None):
            result = subprocess.run(command+arguments, capture_output=True, text=True, timeout=30)
            output = result.stdout+result.stderr
            assert (result.returncode == 0) == success, output
            if diagnostic:
                assert diagnostic in output, output

        attest(['--output', str(snapshot), '--capture-inputs'], True)
        finalize = ['--output', str(final), '--input-snapshot', str(snapshot)]
        attest(finalize, True)
        for path in (inputs/relative_sources[0], inputs/header, compiler_copy,
                     inputs/contracts, inputs/instructions, inputs/manifest, sdk):
            original = path.read_bytes()
            try:
                path.write_bytes(original+b'\nchanged attested input\n')
                attest(finalize, False, 'direct runtime inputs changed since capture')
            finally:
                path.write_bytes(original)
        attest(finalize, True)
        # A canonical dependency must still fail after all input identities match.
        shutil.copyfile(build/'support/ebin/lists.beam', outputs/'beam_lists.beam')
        attest(finalize, False, 'unresolved runtime import')
        attest(['--output', str(final)], False, 'requires --capture-inputs or --input-snapshot')
        attest(['--output', str(snapshot), '--capture-inputs', '--direct-sources',
                str(inputs/relative_sources[0]), str(inputs/relative_sources[0])],
               False, 'duplicate attested input')
        path = inputs/relative_sources[0]
        original = path.read_bytes()
        try:
            path.write_text(re.sub(r'^-module\([^\n]+\)\.', '-module(wrong_module).',
                                   original.decode(), count=1, flags=re.M))
            attest(['--output', str(snapshot), '--capture-inputs'], False,
                   'direct runtime module does not match source filename')
        finally:
            path.write_bytes(original)


def stdlib_artifacts(source, build, work, compiler):
    """The ordinary library preserves its pinned complete API and import closure."""
    contract = json.loads((source/'tests/checks/stdlib_api.json').read_text())
    modules = contract['modules']
    assert sum(len(m['functions']) for m in modules.values()) == 467
    assert sum(len(m['types']) for m in modules.values()) == 44
    root = build/'lib/beam/stdlib'
    assert (root/'modules.txt').read_text().splitlines() == list(modules)
    assert {p.name for p in (root/'ebin').iterdir()} == {m+'.beam' for m in modules}
    helpers = runpy.run_path(str(source/'src/beam/runtime_support/attest.py'))
    providers = {m: root/'ebin'/(m+'.beam') for m in modules}
    providers.update({m: build/'lib/beam/library/ebin'/(m+'.beam')
                      for m in ('beam','beam_lists','beam_maps','beam_math','beam_ets')})
    available = set(helpers['native_origins']()) | {
        ('erlang','get_module_info',1), ('erlang','get_module_info',2),
        ('erts_internal','mc_iterator',1), ('erts_internal','mc_refill',1)}
    for module, path in providers.items():
        available.update((module, name, arity) for name, arity in beam_exports(path))
    receipt = json.loads((build/'runtime-support/attestations/beam_stdlib_modules.json').read_text())
    assert receipt['source_mode'] == 'direct' and receipt['import_policy'] == 'stdlib'
    assert {m['module'] for m in receipt['modules']} == set(modules)
    resource = 'src/beam/stdlib/doc/sofs.md'
    assert receipt['resources'] == {resource: contract['sofs_doc_sha256']}
    assert hashlib.sha256((source/resource).read_bytes()).hexdigest() == contract['sofs_doc_sha256']
    type_cases = []
    for module, expected in modules.items():
        path = root/'ebin'/(module+'.beam')
        assert beam_exports(path) == {tuple(fa) for fa in expected['functions']} | {
            ('module_info',0), ('module_info',1)}, module
        metadata = helpers['native_metadata'](beam_chunks(path)[b'Meta'])
        assert not metadata.get('beam_native_slots', []), module
        helpers['audit_imports'](path, 'stdlib')
        assert not beam_table(path, b'ImpT') - available, module
        entry = next(m for m in receipt['modules'] if m['module'] == module)
        relative = 'src/beam/stdlib/'+module+'.erl'
        assert entry['source'] == relative
        assert entry['source_sha256'] == receipt['inputs'][relative] == hashlib.sha256((source/relative).read_bytes()).hexdigest()
        assert entry['bytecode_sha256'] == hashlib.sha256(path.read_bytes()).hexdigest()
        types = ','.join('{%s,%d}' % tuple(fa) for fa in expected['types'])
        type_cases.append('{%s,[%s]}' % (json.dumps(str(path)), types))
    # Read actual exported types from the compiler's documentation metadata,
    # independently of the source declarations and function export table.
    with tempfile.TemporaryDirectory(prefix='stdlib-check-', dir=work) as temporary:
        fixture = Path(temporary)
        result = subprocess.run([str(compiler), '--deterministic', '-o', str(fixture), '--',
                                 str(source/'tests/checks/stdlib_checks.erl')],
                                capture_output=True, text=True, timeout=30)
        assert result.returncode == 0, result.stdout+result.stderr
        probe = fixture/'stdlib_types_probe.erl'
        probe.write_text('-module(stdlib_types_probe).\n'
                         '-compile({parse_transform,stdlib_checks}).\n'
                         '-stdlib_types(['+','.join(type_cases)+']).\n'
                         '-export([ok/0]).\nok()->ok.\n')
        result = subprocess.run([str(compiler), '--deterministic', '-o', str(fixture), '--', str(probe)],
                                capture_output=True, text=True, timeout=30)
        assert result.returncode == 0, result.stdout+result.stderr
    interactive = (build/'lib/beam/runtime/beam/interactive/modules.txt').read_text().splitlines()
    assert not set(modules) & set(interactive), 'duplicate ordinary library supplier'


def main():
    parser = argparse.ArgumentParser()
    for name in ('cmake', 'source-dir', 'build-dir', 'test-dir', 'bootstrap', 'projection'):
        parser.add_argument('--' + name, required=True)
    args = parser.parse_args()
    source, build, work = map(Path, (args.source_dir, args.build_dir, args.test_dir))
    work.mkdir(parents=True, exist_ok=True)
    original = source / 'src/beam/compiler/seeds'
    manifest = json.loads((original / 'manifest.json').read_text())
    records = []
    native_catalog(source)
    records.append({'case': 'native-catalog', 'status': 0})
    native_generation(source, work)
    records.append({'case': 'native-generation', 'status': 0})
    tracing_artifacts(source, build)
    records.append({'case': 'tracing-artifacts', 'status': 0})
    direct_library_artifacts(source, build, work, Path(args.bootstrap))
    records.append({'case': 'direct-library-artifacts', 'status': 0})
    stdlib_artifacts(source, build, work, Path(args.bootstrap))
    records.append({'case': 'stdlib-artifacts', 'status': 0})
    with tempfile.TemporaryDirectory(prefix='seed-check-', dir=work) as temporary:
        fixture = Path(temporary)
        seeds, inputs, generated = (fixture / name for name in ('seeds', 'source', 'generated'))
        shutil.copytree(original / 'ebin', seeds)
        for entry in manifest['inputs']:
            root = source if entry['scope'] == 'source' else build / 'compiler/generated'
            destination = inputs if entry['scope'] == 'source' else generated
            output = destination / entry['path']
            output.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(root / entry['path'], output)
        metadata = fixture / 'manifest.json'
        metadata.write_text(json.dumps(manifest))
        command = [args.cmake, '-DMANIFEST=' + str(metadata), '-DSEEDS=' + str(seeds),
                   '-DINVENTORY=' + str(build / 'lib/beam/tools/compiler/modules.txt'),
                   '-DSOURCE_ROOT=' + str(inputs), '-DGENERATED_ROOT=' + str(generated),
                   '-DOUTPUTS=' + str(build / 'lib/beam/tools/compiler/ebin'),
                   '-DVERIFY_INPUTS=ON', '-P', str(source / 'cmake/VerifyCompilerSeeds.cmake')]

        def check(name, success, diagnostic=None):
            result = subprocess.run(command, capture_output=True, text=True, timeout=30)
            text = result.stdout + result.stderr
            (work / (name + '.log')).write_text(text)
            assert (result.returncode == 0) == success, (name, result.returncode, text)
            if diagnostic:
                assert diagnostic in text, (name, text)
            records.append({'case': name, 'status': result.returncode})

        check('reproduced', True)
        seed = seeds / (manifest['modules'][0]['name'] + '.beam')
        original_bytes = seed.read_bytes()
        seed.write_bytes(original_bytes + b'changed')
        check('tampered-seed', False, 'Compiler seed hash mismatch')
        seed.unlink()
        check('missing-seed', False)
        seed.write_bytes(original_bytes)
        extra = seeds / 'undeclared.beam'
        extra.write_bytes(original_bytes)
        check('undeclared-seed', False, 'undeclared modules')
        extra.unlink()
        entry = manifest['modules'][0]['source']
        input_file = (inputs if entry['scope'] == 'source' else generated) / entry['path']
        original_source = input_file.read_bytes()
        input_file.write_bytes(original_source + b'\n% changed source\n')
        check('unattested-source', False, 'Compiler source attestation mismatch')
        input_file.write_bytes(original_source)
        manifest['modules'].append(manifest['modules'][0])
        metadata.write_text(json.dumps(manifest))
        check('duplicate-module', False, 'duplicate compiler seed module')
        manifest['modules'].pop()
        manifest['modules'][0]['source'] = {'scope': 'source', 'path': 'unattested.erl'}
        metadata.write_text(json.dumps(manifest))
        check('missing-source-attestation', False, 'no attested source')
    # The runtime projection has an independent inventory and never rewrites seeds.
    with tempfile.TemporaryDirectory(prefix='projection-check-', dir=work) as temporary:
        fixture=Path(temporary)
        compiler=Path(args.bootstrap)
        transform=Path(args.projection)
        shutil.copyfile(transform,fixture/transform.name)
        checks=source / 'tests/checks/projection_checks.erl'
        shutil.copyfile(checks,fixture/checks.name)
        for unit in (fixture/checks.name,):
            result=subprocess.run([str(compiler),'--deterministic','-o',str(fixture),'--',str(unit)],
                                  capture_output=True,text=True,timeout=30)
            assert result.returncode==0,result.stdout+result.stderr
        probe=fixture/'projection_probe.erl'
        probe.write_text('-module(projection_probe).\n-compile({parse_transform,beam_runtime_projection}).\n-compile({parse_transform,projection_checks}).\n-export([ok/0]).\nok()->ok.\n')
        result=subprocess.run([str(compiler),'--deterministic','-o',str(fixture),'--',str(probe)],
                              capture_output=True,text=True,timeout=30)
        (work/'projection.log').write_text(result.stdout+result.stderr)
        assert result.returncode==0,result.stdout+result.stderr
        records.append({'case':'runtime-projection','status':result.returncode})
        attestation=build/'runtime-support/attestations/beam_runtime_support_modules.json'
        runtime=json.loads(attestation.read_text())
        audit=runpy.run_path(str(source/'src/beam/runtime_support/attest.py'))['audit_imports']
        try:
            audit(build/'support/ebin/sets.beam')
        except SystemExit as error:
            assert 'unresolved runtime import' in str(error),error
        else:
            raise AssertionError('canonical dependency escaped the runtime import audit')
        # Removed runtime helpers must not become undeclared executable imports.
        for module, function, arguments in [('dict', 'new', ''), ('gb_trees', 'empty', ''),
                                            ('math', 'sqrt', 'X'), ('erl_ddll', 'loaded_drivers', ''),
                                            ('erl_debugger', 'supported', '')]:
            dependency = fixture/'canonical_dependency.erl'
            head = 'X' if arguments else ''
            dependency.write_text('-module(canonical_dependency).\n'
                                  f'-export([value/{1 if arguments else 0}]).\n'
                                  f'value({head})->{module}:{function}({arguments}).\n')
            result=subprocess.run([str(compiler),'--deterministic','-o',str(fixture),'--',str(dependency)],
                                  capture_output=True,text=True,timeout=30)
            assert result.returncode==0,result.stdout+result.stderr
            try:
                audit(fixture/'canonical_dependency.beam')
            except SystemExit as error:
                assert f'{module}:{function}/{1 if arguments else 0}' in str(error),error
            else:
                raise AssertionError(f'undeclared {module} dependency escaped the runtime import audit')
        staged=build/'runtime-support/src'
        command=[str(compiler),'--deterministic','--modern-target','-I',str(source/'src/beam/support/include'),
                 '-I',str(source/'src/beam/support/src'),'-o',str(fixture),'--']
        command += [str(staged/(m['module']+'.erl')) for m in runtime['modules']]
        result=subprocess.run(command,capture_output=True,text=True,timeout=120)
        (work/'projection-reproduction.log').write_text(result.stdout+result.stderr)
        assert result.returncode==0,result.stdout+result.stderr
        for module in runtime['modules']:
            output=fixture/(module['module']+'.beam')
            actual=hashlib.sha256(output.read_bytes()).hexdigest()
            assert actual==module['bytecode_sha256'],module['module']
            audit(output)
        def chunk(path, name):
            data=path.read_bytes()
            offset=12
            while offset<len(data):
                size=struct.unpack('>I',data[offset+4:offset+8])[0]
                if data[offset:offset+4]==name:
                    return data[offset+8:offset+8+size]
                offset += 8+(size+3)//4*4
            raise AssertionError((path,name))
        assert chunk(build/'lib/beam/stdlib/ebin/sofs.beam',b'Docs')==chunk(build/'support/ebin/sofs.beam',b'Docs')
        # Public delivery has its own export contract; the compiler copy stays complete.
        canonical = beam_exports(build/'support/ebin/ets.beam')
        declared = set()
        for body in re.findall(r'^-export\(\[(.*?)\]\)\.',
                               (source/'src/beam/support/src/ets.erl').read_text(), re.S | re.M):
            declared.update((name,int(arity)) for name,arity in re.findall(r'(\w+)\s*/\s*(\d+)',body))
        generated = {('module_info',0),('module_info',1)}
        assert canonical == declared | generated
        omitted = {('table',1),('table',2),('from_dets',2),('to_dets',2)}
        pending = {('file2tab',1),('file2tab',2),('tab2file',2),('tab2file',3),
                   ('tabfile_info',1),('i',0),('i',1),('i',2),('i',3)}
        public = build/'modern-library/ebin/beam_ets.beam'
        assert beam_exports(public) == canonical - omitted - pending
        audit(public)
        assert not ({module for module, _, _ in beam_table(public, b'ImpT')} &
                    {'qlc','dets','file','disk_log','io','erl_eval'})
        base = json.loads((build/'runtime-support/attestations/beam_public_library_modules.json').read_text())
        ets = next(m for m in base['modules'] if m['module']=='beam_ets')
        assert {tuple(fa) for fa in ets['withheld_exports']['omitted']} == omitted
        assert {tuple(fa) for fa in ets['withheld_exports']['pending']} == pending
        assert 'beam_ets' not in {m['module'] for m in runtime['modules']}
        assert 'ms_transform' in {m['module'] for m in runtime['modules']}
        assert 'ets' not in {m['module'] for m in runtime['modules']}
        # Generated test inputs may be outside the repository in a fresh build.
        fault = json.loads((build/'runtime-support/attestations/beam_file_fault_modules.json').read_text())
        [generated_module] = fault['modules']
        original = build/'tests/file-faults/prim_file.erl'
        assert source/generated_module['source'] == original
        assert generated_module['source_sha256'] == hashlib.sha256(original.read_bytes()).hexdigest()
        records.append({'case':'runtime-reproduction','modules':len(runtime['modules']),'status':0})

    (work / 'results.json').write_text(json.dumps(records, indent=2) + '\n')


if __name__ == '__main__':
    main()
