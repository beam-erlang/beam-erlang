"""Compiler seed reproduction and rejection of unattested bootstrap inputs."""

import argparse
import json
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    for name in ('cmake', 'source-dir', 'build-dir', 'test-dir'):
        parser.add_argument('--' + name, required=True)
    args = parser.parse_args()
    source, build, work = map(Path, (args.source_dir, args.build_dir, args.test_dir))
    work.mkdir(parents=True, exist_ok=True)
    original = source / 'src/beam/compiler/seeds'
    manifest = json.loads((original / 'manifest.json').read_text())
    records = []
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
    (work / 'results.json').write_text(json.dumps(records, indent=2) + '\n')


if __name__ == '__main__':
    main()
