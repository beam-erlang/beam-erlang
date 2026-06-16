"""Producer integrity at consumer configuration and runtime acquisition boundaries."""
from pathlib import Path

from support.bytecode import BASE_ASSETS, BOOT_ASSETS, SERVICES_ASSETS, TEST_SUPPORT_DIR
from support.process import check


def check_bytecode(execute, cmake, source, sdk, client, root):
    for inventory, assets, name, other in (
        ('base', BASE_ASSETS, 'beam_map', 'beam_list'),
        ('boot', BOOT_ASSETS, 'beam_boot', 'beam_return_boot'),
        ('services', SERVICES_ASSETS, 'beam_output', 'beam_code'),
    ):
        work = root / inventory
        work.mkdir()
        check_inventory(execute, cmake, source, sdk, client, work, assets, name, other)


def check_inventory(execute, cmake, source, sdk, client, root, assets, name, other):
    replacement = root / 'replacement'
    replacement.mkdir()
    erl = replacement / f'{name}.erl'
    erl.write_text(f'-module({name}).\n-export([replacement/0]).\nreplacement() -> different.\n')
    execute([sdk / 'bin/beam', 'compile', '--deterministic', '-o', replacement, '--', erl])
    manifest = assets[0]
    module = str(Path(manifest).parent / f'ebin/{name}.beam')
    canonical = {relative: (sdk / relative).read_bytes() for relative in assets}
    # Configuration covers missing and changed producer inputs. Runtime cases
    # use the already compiled consumer; neither boundary may trust replacements.
    cases = [
        ('missing-manifest', manifest, None, True),
        ('duplicate-manifest', manifest, (name + '\n' + name + '\n').encode(), True),
        ('malformed-manifest', manifest, b'\x00not-a-manifest\n', True),
        ('missing-module', module, None, False),
        ('wrong-name', module, canonical[str(Path(manifest).parent / f'ebin/{other}.beam')], False),
        ('same-name-replacement', module, (replacement / f'{name}.beam').read_bytes(), True),
    ]
    command = [client / 'returning_c_host', sdk, client / 'sdk_returning.beam']
    for name, relative, value, configure in cases:
        asset = sdk / relative
        try:
            if value is None:
                asset.unlink()
            else:
                asset.write_bytes(value)
            rejected = execute(command, status=None)
            check(rejected.returncode != 0 and not rejected.stdout and rejected.stderr,
                  f'consumer accepted altered bytecode: {name}')
            if configure:
                rejected = execute([cmake, '-S', source, '-B', root / f'rejected-{name}',
                                    f'-DCMAKE_PREFIX_PATH={sdk}',
                                    f'-DTEST_SUPPORT_DIR={TEST_SUPPORT_DIR}'], status=None)
                check(rejected.returncode != 0 and
                      ('asset is missing' in rejected.stderr or
                       'differs from its producer expectations' in rejected.stderr),
                      f'consumer configured with altered producer inputs: {name}')
        finally:
            asset.write_bytes(canonical[relative])
        restored = execute(command)
        check(restored.stdout == 'Installed returning SDK\n' and not restored.stderr,
              f'consumer failed after restoring {name}')
