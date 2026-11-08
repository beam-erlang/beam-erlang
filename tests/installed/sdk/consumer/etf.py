"""Standalone codec incorporation, VM-independent lifetime and artifact integrity."""
import json
import shutil
from support.process import check


def check_etf(execute, cmake, source, prefix, root, phase):
    client = root / (phase + '-etf-runtime')
    execute([cmake, '-S', source / 'etf', '-B', client,
             f'-DCMAKE_PREFIX_PATH={prefix}'])
    execute([cmake, '--build', client])
    for language in ('c', 'cxx'):
        for form in ('Runtime', 'RuntimeStatic'):
            result = execute([client / f'etf_{language}_{form}', client / 'etf_echo.beam'])
            check(result.stdout == 'ETF SDK OK\n' and not result.stderr, 'ETF VM lifetime failed')

    codec = root / (phase + '-codec-sdk')
    for relative in ('include', 'lib/cmake/BEAM', 'lib/beam/etf'):
        shutil.copytree(prefix / relative, codec / relative)
    for relative in ('lib/beam/sdk.toml', 'lib/beam/target.toml',
                     'lib/beam/compiler-generation.json', 'lib/beam/runtime-artifacts.json'):
        shutil.copy2(prefix / relative, codec / relative)
    copied = root / (phase + '-codec-source')
    shutil.copytree(source / 'etf', copied)

    def configure(name, status=0):
        return execute([cmake, '-S', copied, '-B', root / (phase + '-codec-' + name),
                        f'-DCMAKE_PREFIX_PATH={codec}', '-DCODEC_ONLY=ON'], status=status)
    configure('build')
    build = root / (phase + '-codec-build')
    execute([cmake, '--build', build])
    # Running from an unrelated directory needs neither the SDK nor a VM image.
    archived = codec.with_name(codec.name + '-hidden')
    codec.rename(archived)
    try:
        for language in ('c', 'cxx'):
            result = execute([build / f'etf_{language}'])
            check(result.stdout == 'ETF SDK OK\n' and not result.stderr, 'standalone ETF failed')
    finally:
        archived.rename(codec)
    manifest = codec / 'lib/beam/runtime-artifacts.json'
    entries = json.loads(manifest.read_bytes())
    object_path = codec / next(e['path'] for e in entries['artifacts'].values()
                              if e['kind'] == 'codec-object')
    original = object_path.read_bytes()
    for damage in ('missing', 'corrupt'):
        try:
            if damage == 'missing':
                object_path.unlink()
            else:
                object_path.write_bytes(b'corrupt object payload')
            result = configure(damage, None)
            check(result.returncode != 0 and 'artifact' in result.stderr.lower(),
                  f'ETF accepted {damage} object')
        finally:
            object_path.write_bytes(original)
    original = manifest.read_bytes()
    try:
        entries['build-id'] = 'mismatched-sdk'
        manifest.write_text(json.dumps(entries))
        result = configure('identity', None)
        check(result.returncode != 0, 'ETF accepted a mismatched SDK identity')
    finally:
        manifest.write_bytes(original)
