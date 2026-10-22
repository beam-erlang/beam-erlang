#!/usr/bin/env python3
"""Preview transport and qualification. Native SDK schemas remain SDK-owned."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import urllib.request
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
SHA = re.compile(r'[0-9a-f]{40}')
DIGEST = re.compile(r'[0-9a-f]{64}')
MAX_RELEASE_COUNTER = 9007199254740991
RELEASE_VERSION = re.compile(
    r'(?P<erts>(?:0|[1-9][0-9]*)(?:\.(?:0|[1-9][0-9]*))+)-r(?P<revision>[1-9][0-9]*)'
    r'(?:-preview\.(?P<preview>[1-9][0-9]*))?')
PACKAGE_TESTS = {'package-commands', 'package-builds', 'package-projects', 'package-tasks'}
OTP_TESTS = set('installed-sdk-smoke installed-sdk-boundary otp-native-public-boundary otp-host-public-boundary otp-bootstrap-integrity source-built-compiler otp-native-activation otp-host-preparation otp-terminal-lifecycle otp-boot-observer otp-compatibility-transfer initial-product isolated-product otp-command-policy otp-development-support otp-et-event-file otp-installed-tree otp-standard-release'.split())
ASSETS = {'reference-emu', 'reference-jit', 'provenance-emu', 'provenance-jit', 'rebar3', 'postgres', 'ldap', 'ftp'}


def version_metadata(version):
    """Parse an ERTS baseline plus distribution revision and optional preview."""
    match = RELEASE_VERSION.fullmatch(version) if isinstance(version, str) else None
    if match is None:
        raise ValueError('invalid BEAM release version: expected <erts>-r<N>[-preview.<N>]')
    revision = int(match['revision'])
    preview = int(match['preview']) if match['preview'] else None
    if revision > MAX_RELEASE_COUNTER or (preview is not None and preview > MAX_RELEASE_COUNTER):
        raise ValueError('release counters must not exceed 9007199254740991')
    return {'erts_version': match['erts'], 'distribution_revision': revision,
            'channel': 'preview' if preview is not None else 'stable', 'preview_number': preview}


def validate_version_record(record, expected_version=None):
    metadata = version_metadata(record.get('version'))
    for name, value in metadata.items():
        if name not in record or type(record[name]) is not type(value) or record[name] != value:
            raise ValueError('inconsistent release version metadata: ' + name)
    if expected_version is not None and record['version'] != expected_version:
        raise ValueError('release version does not match expected identity')
    return metadata


def validate_candidate_version(record, expected_version=None):
    if type(record.get('schema')) is not int or record['schema'] != 1:
        raise ValueError('unsupported candidate schema')
    metadata = validate_version_record(record, expected_version)
    if record['sdk']['identity'] != record['version']:
        raise ValueError('SDK release identity mismatch')
    return metadata


def read(path):
    return json.loads(Path(path).read_text())


def write(path, data):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2, sort_keys=True) + '\n')


def run(*args, cwd=None):
    return subprocess.check_output([str(a) for a in args], cwd=cwd, text=True).strip()


def digest(path):
    with Path(path).open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()


def asset(path):
    p = Path(path)
    return {'name': p.name, 'sha256': digest(p), 'size': p.stat().st_size}


def verify_asset(path, record):
    if not DIGEST.fullmatch(record.get('sha256', '')) or asset(path) != record:
        raise ValueError(f'asset mismatch: {path}')


def inputs(path, qualification=False):
    data = read(path)
    validate_inputs(data, qualification)
    return data


def validate_inputs(data, qualification=False):
    if type(data.get('schema')) is not int or data['schema'] != 1:
        raise ValueError('unsupported input schema')
    for key, repo in [('package', 'beam-erlang/beam-erlang-package'), ('otp', 'beam-erlang/erlang-otp')]:
        item = data[key]
        if item['repository'] != repo or not SHA.fullmatch(item['commit']):
            raise ValueError(f'{key}: exact reviewed repository and commit required')
    if data['upstream_otp'] != '550d7b7898706c7822362c42e7b93120c1d1f29a':
        raise ValueError('unexpected upstream compatibility pin')
    if set(data['qualification_assets']) != ASSETS:
        raise ValueError('incomplete qualification input inventory')
    missing = [k for k, v in data['qualification_assets'].items() if not v]
    for key, item in data['qualification_assets'].items():
        if item and (not item['url'].startswith('https://') or not DIGEST.fullmatch(item['sha256'])
                     or type(item['size']) is not int or item['size'] <= 0 or not item.get('identity')):
            raise ValueError(f'invalid asset pin: {key}')
    if qualification and (missing or data.get('pending')):
        raise ValueError('qualification inputs unresolved: ' + '; '.join(missing + data.get('pending', [])))


def acquire_source(item, destination):
    destination = Path(destination)
    if destination.exists():
        raise ValueError(f'source destination already exists: {destination}')
    run('git', 'init', destination)
    run('git', '-C', destination, 'fetch', '--depth=1',
        'https://github.com/' + item['repository'] + '.git', item['commit'])
    run('git', '-C', destination, 'checkout', '--detach', 'FETCH_HEAD')
    if run('git', '-C', destination, 'rev-parse', 'HEAD') != item['commit']:
        raise ValueError('source revision mismatch')


def acquire(args):
    data = inputs(args.inputs, args.kind == 'qualification')
    dest = Path(args.destination)
    dest.mkdir(parents=True, exist_ok=True)
    if args.kind == 'package':
        acquire_source(data['package'], dest / 'package')
    else:
        acquire_source(data['otp'], dest / 'otp')
        for key, item in data['qualification_assets'].items():
            target = dest / key
            with urllib.request.urlopen(item['url'], timeout=120) as response, target.open('wb') as out:
                shutil.copyfileobj(response, out)
            verify_asset(target, {'name': key, 'sha256': item['sha256'], 'size': item['size']})


def extract(archive, destination):
    """Reject traversal and escaping symlinks before writing any archive entry."""
    dest = Path(destination).resolve()
    if dest.exists():
        raise ValueError('extraction destination must be fresh')
    with tarfile.open(archive, 'r:gz') as tar:
        members = tar.getmembers()
        roots = {Path(m.name).parts[0] for m in members if Path(m.name).parts}
        if len(roots) != 1:
            raise ValueError('archive must contain one root')
        for m in members:
            path = Path(m.name)
            if path.is_absolute() or '..' in path.parts or m.isdev() or m.isfifo() or m.islnk() or m.mode & 0o7000:
                raise ValueError('unsafe archive member')
            if m.issym():
                target = path.parent / m.linkname
                resolved = (dest / target).resolve()
                root = dest / next(iter(roots))
                if not resolved.is_relative_to(root):
                    raise ValueError('escaping archive symlink')
        dest.mkdir(parents=True)
        # data_filter also rejects writes through archive-created symlinks.
        def checked_mode(member, destination):
            checked = tarfile.data_filter(member, destination)
            return checked.replace(mode=member.mode)
        tar.extractall(dest, filter=checked_mode)
    return dest / next(iter(roots))


def archive_tree(tree, output, root_name):
    with tarfile.open(output, 'w:gz', dereference=False) as tar:
        tar.add(tree, arcname=root_name)


def clean_revision(tree):
    if run('git', '-C', tree, 'status', '--porcelain', '--untracked-files=normal'):
        raise ValueError(f'release source has uncommitted changes: {tree}')
    return run('git', '-C', tree, 'rev-parse', 'HEAD')


def export_source(tree, revision, output, name):
    with Path(output).open('wb') as f:
        subprocess.run(['git', '-C', str(tree), 'archive', '--format=tar.gz', '--prefix=' + name + '/', revision], stdout=f, check=True)


def environment(build, prefix):
    libs = {}
    for path in sorted(Path(prefix).rglob('*')):
        if path.is_file() and not path.is_symlink():
            with path.open('rb') as f:
                elf = f.read(4) == b'\x7fELF'
            if elf:
                result = subprocess.run(['ldd', str(path)], text=True, capture_output=True)
                libs[str(path.relative_to(prefix))] = {
                    'loader_resolution': result.stdout + result.stderr,
                    'elf_headers': run('readelf', '-l', path),
                    'elf_notes': run('readelf', '-n', path),
                    'symbol_versions': run('readelf', '--version-info', path)}
    return {'kernel': platform.release(), 'machine': platform.machine(),
            'cpu': run('lscpu'),
            'runner_image': os.environ.get('ImageOS'), 'runner_image_version': os.environ.get('ImageVersion'),
            'os_release': Path('/etc/os-release').read_text(), 'compiler': run('cc', '--version'),
            'packages': run('dpkg-query', '-W'), 'runtime_libraries': libs,
            'cmake_cache': (Path(build) / 'CMakeCache.txt').read_text(),
            'compile_commands': read(Path(build) / 'compile_commands.json'),
            'reproducible_native_binaries': False}


def inventory(build):
    return json.loads(run('ctest', '--test-dir', build, '--show-only=json-v1'))['tests']


def export_support(build, output):
    build = Path(build).resolve()
    tests = [t for t in inventory(build) if t['name'].startswith('installed-')]
    if not tests:
        raise ValueError('installed acceptance not registered')
    with tempfile.TemporaryDirectory() as temp:
        support = Path(temp) / 'support'
        support.mkdir()
        for part in ['compiler', 'pkg', 'run', 'interactive', 'cli']:
            shutil.copytree(build / part / 'include', support / part / 'include')
        for t in tests:
            command = t['command']
            for i, arg in enumerate(command):
                path = Path(arg)
                if path.is_absolute() and path.is_relative_to(build) and path.is_file():
                    to = support / path.relative_to(build)
                    to.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(path, to)
            t['command'] = [arg.replace(str(build), '{support}').replace(str(ROOT), '{source}') for arg in command]
        write(support / 'tests.json', tests)
        archive_tree(support, output, 'support')


def pack(args):
    data = inputs(args.inputs)
    version = (ROOT / 'BEAM_VERSION').read_text().strip()
    metadata = version_metadata(version)
    revision = clean_revision(ROOT)
    if os.environ.get('GITHUB_SHA') and revision != os.environ['GITHUB_SHA']:
        raise ValueError('BEAM source differs from selected workflow commit')
    package_revision = clean_revision(args.package)
    if package_revision != data['package']['commit']:
        raise ValueError('package source does not match release input pin')
    cache = (Path(args.build) / 'CMakeCache.txt').read_text()
    cache_values = {line.split(':', 1)[0]: line.split('=', 1)[1]
                    for line in cache.splitlines() if ':' in line and '=' in line and not line.startswith(('#', '//'))}
    if Path(cache_values['CMAKE_HOME_DIRECTORY']).resolve() != ROOT or Path(cache_values['BEAM_PACKAGE_SOURCE_DIR']).resolve() != Path(args.package).resolve():
        raise ValueError('producer build source selections do not match release sources')
    if (Path(args.prefix)/'lib/beam/sdk.toml').read_bytes() != (Path(args.build)/'sdk.toml').read_bytes():
        raise ValueError('installation belongs to a different SDK build')
    if 'BEAM_RELEASE_DEBUG_INFO:BOOL=ON' not in cache or 'CMAKE_BUILD_TYPE:STRING=Release' not in cache:
        raise ValueError('release archives require optimized Release with debug information enabled at configuration')
    os_release = Path('/etc/os-release').read_text()
    if 'ID=ubuntu' not in os_release or 'VERSION_ID="24.04"' not in os_release or platform.machine() != 'x86_64':
        raise ValueError('preview producer must be Ubuntu 24.04 x86_64')
    out = Path(args.output)
    out.mkdir(parents=True, exist_ok=True)
    import tomllib
    sdk = tomllib.loads((Path(args.prefix) / 'lib/beam/sdk.toml').read_text())
    # SDK identity is captured at production; consumers receive it from this record.
    if sdk['identity'] != version or sdk['flavor'] != args.flavor:
        raise ValueError('SDK release identity mismatch')
    stem = f'beam-erlang-{version}-ubuntu-24.04-x86_64-{args.flavor}'
    payload = out / (stem + '.tar.gz')
    with tempfile.TemporaryDirectory() as temporary:
        staged = Path(temporary) / stem
        shutil.copytree(args.prefix, staged, symlinks=True)
        notices = staged / 'share/doc/beam-erlang'
        notices.mkdir(parents=True, exist_ok=True)
        shutil.copy2(ROOT / 'LICENSE.txt', notices / 'LICENSE.txt')
        shutil.copy2(ROOT / 'AUTHORS', notices / 'AUTHORS')
        shutil.copytree(ROOT / 'LICENSES', notices / 'LICENSES')
        for relative in ['emulator/asmjit/LICENSE.md', 'emulator/ryu/LICENSE-Apache2',
                         'emulator/ryu/LICENSE-Boost', 'emulator/ryu/xcharconv_ryu.h.sha.license',
                         'emulator/pcre/LICENCE', 'emulator/zstd/LICENSE', 'emulator/zstd/COPYING']:
            target = notices / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(ROOT / relative, target)
        zlib_header = (ROOT/'emulator/zlib/zlib.h').read_text()
        zlib_notice = re.match(r'/\*.*?\*/', zlib_header, re.DOTALL)
        if not zlib_notice:
            raise ValueError('zlib source license notice missing')
        (notices/'zlib-NOTICE.txt').write_text(zlib_notice.group() + '\n')
        package_notice = staged / 'share/doc/beam-erlang-package'
        package_notice.mkdir(parents=True)
        shutil.copy2(Path(args.package) / 'LICENSE.txt', package_notice / 'LICENSE.txt')
        archive_tree(staged, payload, stem)
    support = out / (stem + '-acceptance.tar.gz')
    export_support(args.build, support)
    for name, tree, rev in [('beam-erlang', ROOT, revision), ('beam-erlang-package', args.package, package_revision)]:
        export_source(tree, rev, out / f'{name}-{version}-source.tar.gz', f'{name}-{version}')
    envfile = out / f'environment-{args.flavor}.json'
    write(envfile, environment(args.build, args.prefix))
    record = {'schema': 1, 'version': version, **metadata,
              'target': 'ubuntu-24.04-x86_64', 'flavor': args.flavor,
              'source': revision, 'inputs': data, 'sdk': sdk, 'archive': asset(payload),
              'acceptance': asset(support), 'environment': asset(envfile),
              'sources': [asset(p) for p in sorted(out.glob('*-source.tar.gz'))],
              'origin': {'repository': os.environ.get('GITHUB_REPOSITORY'),
                         'workflow': '.github/workflows/preview-candidate.yml',
                         'run_id': os.environ.get('GITHUB_RUN_ID'), 'attempt': os.environ.get('GITHUB_RUN_ATTEMPT')}}
    write(out / f'candidate-{args.flavor}.json', record)


def acquired(args):
    record = read(args.record)
    validate_candidate_version(record, (ROOT/'BEAM_VERSION').read_text().strip())
    files = Path(args.record).parent
    work = Path(args.work).resolve()
    work.mkdir(parents=True, exist_ok=True)
    for field in ['archive', 'acceptance']:
        verify_asset(files / record[field]['name'], record[field])
    prefix = extract(files / record['archive']['name'], work / 'acquired SDK ü')
    support = extract(files / record['acceptance']['name'], work / 'acceptance inputs')
    tests = read(support / 'tests.json')
    results = []
    for t in tests:
        command = [a.replace('{support}', str(support)).replace('{source}', str(ROOT)) for a in t['command']]
        idx = command.index('--build-dir')
        command[idx:idx+2] = ['--acquired-prefix', str(prefix), '--test-support-build', str(support)]
        idx = command.index('--test-dir')
        command[idx+1] = str(work / ('tests ü ' + t['name']))
        command[0] = sys.executable
        command[command.index('--cmake')+1] = shutil.which('cmake')
        timeout = next((p['value'] for p in t['properties'] if p['name'] == 'TIMEOUT'), 900)
        with (work / (t['name'] + '.log')).open('w') as log:
            try:
                status = subprocess.run(command, cwd=ROOT / 'tests', stdout=log, stderr=subprocess.STDOUT, timeout=timeout).returncode
            except subprocess.TimeoutExpired:
                status = 124
        results.append({'name': t['name'], 'status': 'passed' if status == 0 else 'failed', 'exit_code': status})
    report = {'schema': 1, 'kind': 'acquired', 'candidate': digest(args.record), 'tests': results,
              'eligible': all(t['status'] == 'passed' for t in results), 'host': qualification_host()}
    write(args.report, report)
    if not report['eligible']:
        raise ValueError('acquired-prefix acceptance failed; see retained logs')


def qualification_host():
    return {'kernel': platform.release(), 'machine': platform.machine(),
            'runner_image': os.environ.get('ImageOS'),
            'runner_image_version': os.environ.get('ImageVersion'),
            'os_release': Path('/etc/os-release').read_text(),
            'cpu': run('lscpu'), 'compiler': run('cc', '--version'),
            'packages': run('dpkg-query', '-W')}


def upstream_results(build):
    build = Path(build)
    roots = [build / 'checks']
    for line in (build / 'CMakeCache.txt').read_text().splitlines():
        if line.startswith('OTP_SHORT_TEST_ROOT:PATH=') and line.split('=', 1)[1]:
            roots.append(Path(line.split('=', 1)[1]))
    return [{'file': str(path), 'sha256': digest(path), 'result': read(path)}
            for root in roots for path in sorted(root.rglob('result.json'))]


def ctest(args):
    tests = inventory(args.build)
    names = {t['name'] for t in tests}
    required = OTP_TESTS if args.kind == 'otp' else PACKAGE_TESTS if args.kind == 'package' else {'installed-sdk', 'installed-compile', 'installed-components', 'compiler-artifacts'}
    if not required <= names:
        raise ValueError('mandatory tests missing: ' + ', '.join(sorted(required - names)))
    junit = Path(args.report).resolve().with_suffix('.xml')
    junit.parent.mkdir(parents=True, exist_ok=True)
    status = subprocess.run(['ctest', '--test-dir', args.build, '--output-on-failure', '--output-junit', str(junit)]).returncode
    results = []
    executed = {}
    if junit.exists():
        executed = {e.attrib['name']: e for e in ET.parse(junit).iter('testcase')}
    for test in tests:
        e = executed.get(test['name'])
        disabled = any(p['name'] == 'DISABLED' and p['value'] for p in test['properties'])
        state = 'untested' if e is None else 'failed' if e.find('failure') is not None else 'skipped' if e.find('skipped') is not None else 'passed'
        # The owning CMake selection marks wx deferrals explicitly.
        if disabled and args.kind == 'otp' and ('wx' in test['name'] or any(p['name'] == 'LABELS' and 'deferred' in p['value'] for p in test['properties'])):
            state = 'deferred'
        results.append({'name': test['name'], 'status': state})
    allowed_skip = {'vm-ethread-thread_name'} if args.kind == 'beam' else set()
    eligible = status == 0 and all(t['status'] in ('passed', 'deferred') or (t['status'] == 'skipped' and t['name'] in allowed_skip) for t in results)
    write(args.report, {'schema': 1, 'kind': args.kind, 'candidate': digest(args.record) if args.record else None,
                        'exit_code': status, 'tests': results, 'eligible': eligible,
                        'host': qualification_host(),
                        'upstream_results': upstream_results(args.build) if args.kind == 'otp' else []})
    if not eligible:
        raise ValueError('qualification failed or contains mandatory skipped/untested coverage')


def eligible(manifest, policy):
    if (type(manifest.get('schema')) is not int or manifest['schema'] != 1
            or manifest.get('repository') != policy['repository']):
        raise ValueError('untrusted release manifest')
    validate_version_record(manifest)
    if policy.get('promotion_enabled') is not True or policy['blockers']:
        raise ValueError('public promotion is disabled: ' + '; '.join(policy['blockers']))
    for role in ['release_owners', 'recovery_owners']:
        if not isinstance(policy[role], list) or not policy[role] or any(not isinstance(owner, str) or not owner.strip() for owner in policy[role]):
            raise ValueError('release and recovery owners must be configured')
    if set(manifest['candidates']) != {'emu', 'jit'}:
        raise ValueError('both complete flavors required')
    for flavor, record in manifest['candidates'].items():
        validate_candidate_version(record, manifest['version'])
        if record['target'] != policy['target'] or record['flavor'] != flavor or record['version'] != manifest['version'] or record['source'] != manifest['source']:
            raise ValueError('mixed candidate identities')
        if record['sdk']['identity'] != manifest['version'] or record['sdk']['flavor'] != flavor or not DIGEST.fullmatch(record['sdk']['build-id']):
            raise ValueError('invalid SDK candidate identity')
        if record['origin']['repository'] != policy['repository'] or record['origin']['workflow'] != policy['candidate_workflow'] or record['origin']['run_id'] != manifest['run_id']:
            raise ValueError('untrusted candidate workflow origin')
        inputs_data = record['inputs']
        validate_inputs(inputs_data, qualification=True)
        if inputs_data != manifest['candidates']['emu']['inputs']:
            raise ValueError('flavors have different release inputs')
    expected_reports = {flavor + '-' + kind for flavor in ['emu', 'jit']
                        for kind in ['beam', 'acquired', 'package', 'otp']}
    if set(manifest['qualification']) != expected_reports or set(manifest['candidate_digests']) != {'emu', 'jit'}:
        raise ValueError('incomplete or unexpected qualification inventory')
    for flavor in ['emu', 'jit']:
        installed = {t['name'] for t in manifest['qualification'][flavor + '-beam']['tests']
                     if t['name'].startswith('installed-')}
        acquired_names = {t['name'] for t in manifest['qualification'][flavor + '-acquired']['tests']}
        if acquired_names != installed:
            raise ValueError('acquired acceptance differs from the complete installed selection')
        for kind in ['beam', 'acquired', 'package', 'otp']:
            report = manifest['qualification'].get(flavor + '-' + kind)
            if not report or report.get('eligible') is not True or not report.get('tests'):
                raise ValueError('incomplete or failing qualification: ' + flavor + '-' + kind)
            if report.get('kind') != kind:
                raise ValueError('qualification report kind mismatch')
            names = {t['name'] for t in report['tests']}
            if len(names) != len(report['tests']):
                raise ValueError('duplicate qualification test entries')
            required = OTP_TESTS if kind == 'otp' else PACKAGE_TESTS if kind == 'package' else {'installed-sdk', 'installed-compile', 'installed-components'}
            if not required <= names:
                raise ValueError('mandatory test registration incomplete: ' + flavor + '-' + kind)
            for test in report['tests']:
                if test['status'] == 'passed':
                    continue
                if kind == 'otp' and test['status'] == 'deferred' and (test['name'] == 'wx-product' or test['name'].startswith('upstream-wx_')):
                    continue
                if kind == 'beam' and test['status'] == 'skipped' and test['name'] == 'vm-ethread-thread_name':
                    continue
                raise ValueError('nonpassing mandatory test: ' + test['name'])
            if report.get('candidate') != manifest['candidate_digests'][flavor]:
                raise ValueError('qualification is for a different candidate')


def collect(args):
    root = Path(args.directory)
    records = {f: read(root / f'candidate-{f}.json') for f in ['emu', 'jit']}
    for record in records.values():
        validate_candidate_version(record)
    if records['emu']['source'] != records['jit']['source'] or records['emu']['version'] != records['jit']['version']:
        raise ValueError('flavors have different source identities')
    reports = {}
    for f in ['emu', 'jit']:
        for kind in ['beam', 'acquired', 'package', 'otp']:
            p = root / f'{f}-{kind}.json'
            if p.exists():
                reports[f + '-' + kind] = read(p)
    manifest = {'schema': 1, 'repository': 'beam-erlang/beam-erlang', 'version': records['emu']['version'],
                **version_metadata(records['emu']['version']),
                'source': records['emu']['source'], 'run_id': records['emu']['origin']['run_id'],
                'candidates': records, 'candidate_digests': {f: digest(root / f'candidate-{f}.json') for f in records},
                'qualification': reports, 'reports': [asset(p) for p in sorted(root.glob('*-*.json')) if p.name.split('-')[-1] in {'beam.json','acquired.json','package.json','otp.json'}]}
    missing = []
    for flavor, record in records.items():
        missing.extend(flavor + ': ' + item for item in record['inputs'].get('pending', []))
        missing.extend(flavor + ': missing qualification asset ' + key
                       for key, item in record['inputs']['qualification_assets'].items() if not item)
        for kind in ['beam', 'acquired', 'package', 'otp']:
            report = reports.get(flavor + '-' + kind)
            if not report:
                missing.append(flavor + ': missing ' + kind + ' qualification report')
            elif not report.get('eligible'):
                missing.append(flavor + ': nonpassing ' + kind + ' qualification')
    try:
        eligible(manifest, read(args.policy))
        manifest['eligible'] = True
        manifest['blockers'] = []
    except (ValueError, KeyError) as e:
        manifest['eligible'] = False
        manifest['blockers'] = [str(e)] + missing
    write(args.output, manifest)
    print(json.dumps({'eligible': manifest['eligible'], 'blockers': manifest['blockers']}))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    sub = p.add_subparsers(dest='command', required=True)
    a = sub.add_parser('acquire'); a.add_argument('--inputs', default=ROOT/'release/inputs.json'); a.add_argument('--kind', choices=['package', 'qualification'], required=True); a.add_argument('--destination', required=True); a.set_defaults(func=acquire)
    a = sub.add_parser('pack'); a.add_argument('--inputs', default=ROOT/'release/inputs.json'); a.add_argument('--flavor', choices=['emu','jit'], required=True)
    for flag in ['build', 'prefix', 'package', 'output']: a.add_argument('--'+flag, required=True)
    a.set_defaults(func=pack)
    a = sub.add_parser('acquired')
    for flag in ['record', 'work', 'report']: a.add_argument('--'+flag, required=True)
    a.set_defaults(func=acquired)
    a = sub.add_parser('ctest'); a.add_argument('--kind', choices=['beam','package','otp'], required=True); a.add_argument('--build', required=True); a.add_argument('--report', required=True); a.add_argument('--record'); a.set_defaults(func=ctest)
    a = sub.add_parser('collect'); a.add_argument('--directory', required=True); a.add_argument('--output', required=True); a.add_argument('--policy', default=ROOT/'release/policy.json'); a.set_defaults(func=collect)
    args = p.parse_args()
    try: args.func(args)
    except (ValueError, KeyError, OSError, subprocess.CalledProcessError) as e:
        p.exit(1, str(e)+'\n')

if __name__ == '__main__': main()
