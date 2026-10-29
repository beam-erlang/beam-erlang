"""Transport and fail-closed publication contracts, independent of native builds."""
import copy
import io
import json
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile
import unittest
from unittest.mock import patch
import release
import promote


class ReleaseTests(unittest.TestCase):
    def test_linux_archive_target_facts(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);assets=root/'lib/beam';assets.mkdir(parents=True)
            (assets/'compiler-generation.json').write_text('{}\n')
            sdk={'format':1,'identity':'17.0-r1-preview.1','build-id':'a'*64,'os':'Linux',
                 'architecture':'x86_64','triple':'x86_64-linux-musl','flavor':'emu'}
            facts={'format':1,'sdk-build-id':sdk['build-id'],'system':'Linux','architecture':'amd64','processor':'x86_64',
                   'abi':sdk['triple'],'libc':'musl','baseline':'3.24','compiler-environment':'GNU',
                   'flavor':'emu','compiler-generation':release.digest(assets/'compiler-generation.json')}
            def write(values):
                for name,record in [('sdk',sdk),('target',values)]:
                    (assets/(name+'.toml')).write_text('\n'.join(k+' = '+json.dumps(v) for k,v in record.items())+'\n')
                release.write(assets/'runtime-artifacts.json',{'format':1,'version':sdk['identity'],
                    **{k:sdk[k] for k in ('flavor','build-id','architecture','triple')},
                    'sdk-metadata-sha256':release.digest(assets/'sdk.toml'),'artifacts':{
                    key:{'path':path,'sha256':release.digest(root/path)} for key,path in
                    [('sdk.target','lib/beam/target.toml'),('compiler.generation','lib/beam/compiler-generation.json')]}})
            row=release.catalog.target('linux-alpine3.24-amd64-gcc-emu')
            write(facts)
            self.assertEqual(release.validate_sdk_target(root,row),sdk)
            for key,value in [('libc','glibc'),('baseline','13'),('compiler-environment','Clang'),
                              ('abi','x86_64-pc-linux-gnu'),('architecture','arm64'),
                              ('processor','aarch64'),('flavor','jit'),('sdk-build-id','b'*64)]:
                with self.subTest(field=key):
                    write(dict(facts,**{key:value}))
                    with self.assertRaises(ValueError):release.validate_sdk_target(root,row)
            write(facts)
            (assets/'target.toml').write_text('format = 1\n')
            with self.assertRaisesRegex(ValueError,'provenance'):
                release.validate_sdk_target(root,row)

    def vm_results(self, flavor):
        return [{'file': suite + '/result.json', 'sha256': 'a' * 64, 'result': {
            'suite': suite, 'complete': True, 'expected_cases': len(release._vm_manifest.CASES[suite]),
            'expected_case_names': release._vm_manifest.CASES[suite],
            'cases': [{'name': case, 'status': 'passed'} for case in release._vm_manifest.CASES[suite]],
            'suites': {suite: {'passed': len(release._vm_manifest.CASES[suite]), 'failed': 0,
                               'skipped': 0, 'user_skipped': 0, 'auto_skipped': 0}},
            'source_sha256': {name: release.digest(release.ROOT / 'tests/vm' / name)
                              for name in release._vm_manifest.INPUTS[suite]},
            'installation': {'provenance_sha256': 'f' * 64},
            'sdk': {'version': '17.0-r1-preview.1', 'build_id': 'd' * 64, 'flavor': flavor}}}
            for suite in sorted(release.VM_SUITES)]

    def test_runtime_notices_preserve_archive_paths(self):
        with tempfile.TemporaryDirectory() as temporary:
            notices = Path(temporary) / 'share/doc/beam-erlang'
            release.stage_runtime_notices(notices)
            for relative in ['asmjit/LICENSE.md', 'ryu/LICENSE-Apache2',
                             'ryu/LICENSE-Boost', 'ryu/xcharconv_ryu.h.sha.license',
                             'pcre/LICENCE']:
                self.assertEqual((notices / 'emulator' / relative).read_bytes(),
                                 (release.ROOT / 'src/erts/emulator' / relative).read_bytes())
            self.assertEqual((notices / 'LICENSE.txt').read_bytes(),
                             (release.ROOT / 'LICENSE.txt').read_bytes())
            header = (release.ROOT / 'src/erts/emulator/zlib/zlib.h').read_text()
            self.assertEqual((notices / 'zlib-NOTICE.txt').read_text(),
                             header[:header.index('*/') + 2] + '\n')
            self.assertFalse((notices / 'src').exists())

    def test_cmake_and_release_version_parsers_agree(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / 'cmake').mkdir()
            shutil.copy2(release.ROOT / 'cmake/Version.cmake', root / 'cmake/Version.cmake')
            script = root / 'parse.cmake'
            script.write_text('''include("${CMAKE_CURRENT_LIST_DIR}/cmake/Version.cmake")
file(WRITE "${CMAKE_CURRENT_LIST_DIR}/parsed.txt"
    "${BEAM_RELEASE_VERSION}\\n${BEAM_ERTS_VERSION}\\n${BEAM_DISTRIBUTION_REVISION}\\n"
    "${BEAM_PREVIEW_NUMBER}\\n${BEAM_RELEASE_CHANNEL}\\n${BEAM_PROJECT_VERSION}\\n")
''')
            for version, projection in [('17.0-r1-preview.1', '17.0.0'),
                                        ('17.0-r1', '17.0.0'),
                                        ('15.2.7.13-r2-preview.3', '15.2.7'),
                                        ('17.0-r9007199254740991-preview.9007199254740991', '17.0.0')]:
                with self.subTest(version=version):
                    (root / 'BEAM_VERSION').write_text(version + '\n')
                    result = subprocess.run(['cmake', '-P', str(script)], capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    metadata = release.version_metadata(version)
                    self.assertEqual((root / 'parsed.txt').read_text().splitlines(), [version,
                        metadata['erts_version'], str(metadata['distribution_revision']),
                        str(metadata['preview_number']) if metadata['preview_number'] is not None else '',
                        metadata['channel'], projection])
            for version in ['17-r1', '17.00-r1', '017.0-r1', '17.0-r0', '17.0-r01',
                            '17.0-r1-preview.0', '17.0-r1-preview.01', '0.1.0-preview.1',
                            '17.0-r9007199254740992', '17.0-r1-preview.9007199254740992',
                            '17.0-r1\n17.0-r2']:
                with self.subTest(version=version):
                    (root / 'BEAM_VERSION').write_text(version + '\n')
                    result = subprocess.run(['cmake', '-P', str(script)], capture_output=True, text=True)
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn('BEAM_VERSION', result.stderr)
                    with self.assertRaises(ValueError):
                        release.version_metadata(version)

    def test_release_version_components_and_canonical_spelling(self):
        for version, erts, revision, preview in [
                ('17.0-r1-preview.1', '17.0', 1, 1),
                ('17.0.1-r2', '17.0.1', 2, None),
                ('15.2.7.13-r10-preview.12', '15.2.7.13', 10, 12),
                ('17.0-r9007199254740991-preview.9007199254740991', '17.0', 9007199254740991, 9007199254740991),
                ('0.0-r1', '0.0', 1, None)]:
            with self.subTest(version=version):
                metadata = release.version_metadata(version)
                self.assertEqual(metadata, {'erts_version': erts, 'distribution_revision': revision,
                    'channel': 'preview' if preview is not None else 'stable', 'preview_number': preview})
                record = {'version': version, **metadata}
                release.validate_version_record(record, version)
                self.assertEqual(promote.preview_tag(record), 'v' + version)
        for version in ['', None, True, ['17.0-r1'], '17-r1', '17.0', '017.0-r1', '17.00-r1',
                        '17.0-r0', '17.0-r01', '17.0-r1-preview.0', '17.0-r1-preview.01',
                        '17.0-r1-preview1', '17.0-r1-rc1', '0.1.0-preview.1',
                        '17.0-r9007199254740992', '17.0-r1-preview.9007199254740992',
                        '17.0-r1-preview.1\n', ' 17.0-r1', '17.0-r1+build.1']:
            with self.subTest(version=version), self.assertRaises(ValueError):
                release.version_metadata(version)

    def kit(self, root):
        inputs=root/'offline-inputs'; inputs.mkdir(exist_ok=True)
        release.write(inputs/'subjects.json',{name:release.digest(root/name)
            for name in ['sdk.tar.gz','release-manifest.json']})
        release.archive_tree(inputs,root/'offline-verification.tar.gz','offline-verification')

    def test_source_pins_and_pending_requirements(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)/'inputs.json'
            original = release.read(release.ROOT/'release/inputs.json')
            release.write(path, original)
            release.inputs(path)
            for invalid_schema in [True, 1.0, '1']:
                with self.subTest(schema=invalid_schema), self.assertRaises(ValueError):
                    release.validate_inputs({**original, 'schema': invalid_schema})
            with self.assertRaisesRegex(ValueError, 'unresolved'):
                release.inputs(path, qualification=True)
            for bad in ['main', '', 'a'*39]:
                data = copy.deepcopy(original)
                data['package']['commit'] = bad
                release.write(path,data)
                with self.assertRaises(ValueError): release.inputs(path)
            data = copy.deepcopy(original)
            del data['qualification_assets']['rebar3']
            release.write(path,data)
            with self.assertRaises(ValueError): release.inputs(path)

    def test_digest_size_and_transport(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            tree = root/'source'; tree.mkdir()
            binary = tree/'beam'; binary.write_bytes(b'payload'); binary.chmod(0o775)
            (tree/'alias').symlink_to('beam')
            archive = root/'sdk.tar.gz'
            release.archive_tree(tree,archive,'SDK')
            record = release.asset(archive)
            release.verify_asset(archive,record)
            extracted = release.extract(archive,root/'prefix ü')
            self.assertEqual((extracted/'beam').read_bytes(), b'payload')
            self.assertEqual((extracted/'beam').stat().st_mode & 0o777, 0o775)
            self.assertEqual((extracted/'alias').readlink(),Path('beam'))
            archive.write_bytes(archive.read_bytes()+b'altered')
            with self.assertRaises(ValueError): release.verify_asset(archive,record)

    def test_acquired_reinstallation_preserves_links_and_recovers_missing_files(self):
        import sys
        from types import SimpleNamespace
        sys.path.insert(0,str(release.ROOT/'tests'))
        from support.installation import stage_installation
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp); source=root/'source ü';source.mkdir()
            (source/'beam').write_bytes(b'original')
            (source/'beam-pkg').symlink_to('beam')
            args=SimpleNamespace(acquired_prefix=source)
            destination=root/'stage ü'
            stage_installation(args,destination)
            (destination/'beam').unlink()
            stage_installation(args,destination)
            self.assertEqual((destination/'beam-pkg').readlink(),Path('beam'))
            self.assertEqual((destination/'beam').read_bytes(),b'original')
            with self.assertRaises(ValueError): stage_installation(args,source/'nested')

    def test_archive_traversal_and_link_escape(self):
        for name, link in [('../escape', None), ('sdk/link', '/tmp/escape'), ('sdk/link','../../escape')]:
            with self.subTest(name=name,link=link), tempfile.TemporaryDirectory() as tmp:
                root=Path(tmp); archive=root/'bad.tar.gz'
                with tarfile.open(archive,'w:gz') as tar:
                    info=tarfile.TarInfo(name)
                    if link:
                        info.type=tarfile.SYMTYPE;info.linkname=link
                    tar.addfile(info,io.BytesIO())
                with self.assertRaises(ValueError): release.extract(archive,root/'out')
                self.assertFalse((root/'out').exists())

    def fixture(self):
        policy=release.read(release.ROOT/'release/policy.json')
        policy.update(promotion_enabled=True,blockers=[],release_owners=['release-team'],recovery_owners=['recovery-team'])
        inputs=release.read(release.ROOT/'release/inputs.json')
        inputs['pending']=[]
        inputs['qualification_assets']={key:{'url':'https://example.invalid/'+key,
            'sha256':'c'*64,'size':1,'identity':'test fixture'} for key in release.ASSETS}
        manifest={'schema':1,'repository':policy['repository'],'version':'17.0-r1-preview.1','source':'a'*40,
                  'run_id':'123','candidates':{},'candidate_digests':{},'qualification':{}}
        metadata=release.version_metadata(manifest['version'])
        manifest.update(metadata)
        manifest['branch']='master'
        for target in policy['targets'] + policy['integration_targets']:
            row=release.catalog.target(target);flavor=row['flavor']
            manifest['candidate_digests'][target]='b'*64
            manifest['candidates'][target]={'schema':1,**metadata,'target':target,'platform':row,'development':False,'flavor':flavor,'version':manifest['version'],'source':manifest['source'],
                'sdk':{'identity':manifest['version'],'flavor':flavor,'build-id':'d'*64},
                'origin':{'repository':policy['repository'],'workflow':policy['candidate_workflow'],'run_id':'123','branch':'master'},
                'inputs':copy.deepcopy(inputs)}
            for kind in release.report_kinds(target):
                names=release.OTP_TESTS if kind=='otp' else release.VM_TESTS if kind=='beam-vm' else release.PACKAGE_TESTS if kind=='package' else {'installed-sdk','installed-compile','installed-components','installed-run'}
                if row['qualification']!='desktop':
                    names={'target-sdk-layout'} if kind=='beam' else {'acquired-sdk-consumer-build'} if row['executor']=='ios-device' else {'acquired-sdk-consumer-build','acquired-sdk-runtime','acquired-sdk-deployment','acquired-sdk-components','acquired-sdk-missing-and-identity','acquired-sdk-lifecycle'}
                    if kind=='acquired' and row['family']=='android':names.add('acquired-sdk-16k-pages')
                    if kind=='ios-app': names=release.IOS_APP_TESTS
                    if kind=='android-app': names=release.ANDROID_APP_TESTS
                manifest['qualification'][target+'-'+kind]={'kind':kind,'physical_device_execution':'passed','eligible':True,'candidate':'b'*64,
                    'tests':[{'name':n,'status':'passed'} for n in names]}
                if kind == 'android-app':
                    abi = 'arm64-v8a' if row['arch'] == 'arm64' else 'x86_64'
                    destinations = [(35,4096,'emulator'),(35,16384,'emulator'),(36,4096,'emulator')]
                    destinations += [(36,4096,'physical')] if abi == 'arm64-v8a' else [(26,4096,'emulator')]
                    manifest['qualification'][target+'-'+kind]['executions'] = [dict(
                        sdk_build_id='d'*64, abi=abi, flavor=flavor, status='passed',
                        debuggable=False, debugger_attached=False, configuration='Release',
                        page_size_compatibility=False, signing_certificate_sha256='a'*64,
                        serial='explicit-fixture-device', os_version=str(api), api=api,
                        page_size=page, device_kind=kind) for api,page,kind in destinations]
                if kind == 'beam-vm':
                    manifest['qualification'][target+'-'+kind]['vm_results'] = self.vm_results(flavor)
        return manifest,policy

    def test_mobile_required_execution_cannot_be_disabled_or_omitted(self):
        manifest,policy=self.fixture()
        key='android-arm64-ndk-emu-linux-acquired'
        for status in ('unexecuted','untested','skipped','failed'):
            invalid=copy.deepcopy(manifest)
            invalid['qualification'][key]['tests'][0]['status']=status
            with self.subTest(status=status), self.assertRaises(ValueError):
                release.eligible(invalid,policy)
        invalid=copy.deepcopy(manifest)
        invalid['qualification'][key]['tests']=[t for t in invalid['qualification'][key]['tests'] if t['name']!='acquired-sdk-16k-pages']
        with self.assertRaisesRegex(ValueError,'mandatory tests missing'):
            release.eligible(invalid,policy)

    def test_consumer_build_cannot_replace_android_qualification(self):
        for kind in ('acquired','android-app'):
            for eligible in (False,True):
                manifest,policy=self.fixture()
                key='android-arm64-ndk-emu-linux-'+kind
                manifest['qualification'][key].update(kind='consumer-build',eligible=eligible,
                    target_execution='untested',tests=[{'name':'acquired-sdk-consumer-build','status':'passed'}])
                with self.subTest(kind=kind,eligible=eligible),self.assertRaisesRegex(ValueError,'qualification'):
                    release.eligible(manifest,policy)

    def test_ios_device_requires_real_signed_application_execution(self):
        manifest,policy=self.fixture()
        key='ios-device-arm64-emu-ios-app'
        for state in ('untested','build-only','failed'):
            invalid=copy.deepcopy(manifest)
            invalid['qualification'][key]['physical_device_execution']=state
            with self.subTest(state=state), self.assertRaisesRegex(ValueError,'physical device'):
                release.eligible(invalid,policy)
        invalid=copy.deepcopy(manifest)
        del invalid['qualification'][key]
        with self.assertRaisesRegex(ValueError,'qualification inventory'):
            release.eligible(invalid,policy)

    def test_android_requires_candidate_bound_complete_application_matrix(self):
        manifest,policy=self.fixture()
        key='android-arm64-ndk-emu-linux-android-app'
        report=manifest['qualification'][key]
        for field,value in [('sdk_build_id','e'*64),('abi','x86_64'),('flavor','jit'),
                            ('debuggable',True),('debugger_attached',True),
                            ('page_size_compatibility',True),('api',25),('page_size',8192),
                            ('signing_certificate_sha256',''),('serial',''),('status','untested')]:
            invalid=copy.deepcopy(manifest)
            invalid['qualification'][key]['executions'][0][field]=value
            with self.subTest(field=field),self.assertRaises(ValueError):
                release.eligible(invalid,policy)
        for index in range(len(report['executions'])):
            invalid=copy.deepcopy(manifest)
            del invalid['qualification'][key]['executions'][index]
            with self.subTest(missing=index),self.assertRaises(ValueError):
                release.eligible(invalid,policy)
        invalid=copy.deepcopy(manifest)
        invalid['qualification'][key]['physical_device_execution']='untested'
        with self.assertRaisesRegex(ValueError,'Physical Android'):
            release.eligible(invalid,policy)

    def test_publication_excludes_noncanonical_sdk_archives(self):
        manifest,policy=self.fixture()
        manifest['reports']=[]
        for target,record in manifest['candidates'].items():
            for kind in ('archive','acceptance','environment'):
                record[kind]={'name':target+'-'+kind,'sha256':'a'*64,'size':1}
            record['sources']=[]
        public=promote.payloads(manifest)
        for target,record in manifest['candidates'].items():
            self.assertEqual(record['archive']['name'] in public,record['platform']['release'])
            self.assertIn(record['environment']['name'],public)

    def test_version_mismatch_is_rejected_before_publication_network_access(self):
        from types import SimpleNamespace
        manifest, policy = self.fixture()
        manifest.update(eligible=True, blockers=[])
        original_read = release.read
        mutations = [
            lambda m: m.update(schema=True),
            lambda m: m.update(version='0.1.0-preview.1'),
            lambda m: m.update(erts_version='18.0'),
            lambda m: m.update(distribution_revision=2),
            lambda m: m.update(distribution_revision=True),
            lambda m: m.update(channel='stable'),
            lambda m: m.update(preview_number=2),
            lambda m: m.update(preview_number=True),
            lambda m: m['candidates']['linux-ubuntu24.04-amd64-gcc-jit'].update(schema=True),
            lambda m: m['candidates']['linux-ubuntu24.04-amd64-gcc-jit'].pop('erts_version'),
            lambda m: m['candidates']['linux-ubuntu24.04-amd64-gcc-jit'].update(distribution_revision=2),
            lambda m: m['candidates']['linux-ubuntu24.04-amd64-gcc-jit']['sdk'].update(identity='17.0-r2-preview.1')]
        for index, mutation in enumerate(mutations):
            with self.subTest(mutation=index), tempfile.TemporaryDirectory() as tmp:
                invalid = copy.deepcopy(manifest)
                mutation(invalid)
                path = Path(tmp) / 'candidate-manifest.json'
                release.write(path, invalid)
                args = SimpleNamespace(directory=tmp, digest=release.digest(path), run='123')
                with patch.object(release, 'read', side_effect=lambda p: policy if Path(p).name == 'policy.json' else original_read(p)), \
                        patch.object(promote, 'gh') as network:
                    with self.assertRaises(ValueError):
                        promote.prepare(args)
                    network.assert_not_called()

    def test_stable_requires_maintenance_line_and_retains_exact_identity(self):
        manifest,policy=self.fixture()
        version='17.0-r1'
        manifest.update(version=version,**release.version_metadata(version))
        for record in manifest['candidates'].values():
            record.update(version=version,**release.version_metadata(version))
            record['sdk']['identity']=version
        for report in manifest['qualification'].values():
            for item in report.get('vm_results', []):
                item['result']['sdk']['version'] = version
        with self.assertRaisesRegex(ValueError,'source line'):release.eligible(manifest,policy)
        manifest['branch']='maint-17'
        for record in manifest['candidates'].values():record['origin']['branch']='maint-17'
        release.eligible(manifest,policy)
        self.assertEqual(promote.preview_tag(manifest),'v17.0-r1')

    def test_promotion_complete_and_fail_closed(self):
        manifest,policy=self.fixture()
        release.eligible(manifest,policy)
        for mutation in [lambda m,p: p.update(promotion_enabled=False),
                         lambda m,p: p.update(promotion_enabled='false'),
                         lambda m,p: p.update(release_owners='not-a-list'),
                         lambda m,p: p.update(recovery_owners=[]),
                         lambda m,p: m['candidates'].pop('linux-ubuntu24.04-amd64-gcc-emu'),
                         lambda m,p: m['candidates']['linux-ubuntu24.04-amd64-gcc-emu']['origin'].update(repository='untrusted/repo'),
                         lambda m,p: m['candidates']['linux-ubuntu24.04-amd64-gcc-jit']['inputs']['otp'].update(commit='e'*40),
                         lambda m,p: m['candidates']['linux-ubuntu24.04-amd64-gcc-emu']['inputs']['qualification_assets'].pop('ftp'),
                         lambda m,p: m['qualification'].pop('linux-ubuntu24.04-amd64-gcc-jit-otp'),
                         lambda m,p: m['qualification']['linux-ubuntu24.04-amd64-gcc-emu-otp'].update(eligible=False),
                         lambda m,p: m['qualification']['linux-ubuntu24.04-amd64-gcc-emu-otp'].update(candidate='c'*64),
                         lambda m,p: m['qualification']['linux-ubuntu24.04-amd64-gcc-emu-otp'].update(tests=[]),
                         lambda m,p: m['qualification']['linux-ubuntu24.04-amd64-gcc-emu-acquired'].update(tests=[t for t in m['qualification']['linux-ubuntu24.04-amd64-gcc-emu-acquired']['tests'] if t['name']!='installed-run']),
                         lambda m,p: m['qualification']['linux-ubuntu24.04-amd64-gcc-emu-otp']['tests'][0].update(status='skipped')]:
            m,p=copy.deepcopy(manifest),copy.deepcopy(policy);mutation(m,p)
            with self.assertRaises(ValueError): release.eligible(m,p)

    def test_vm_qualification_requires_matching_complete_case_evidence(self):
        manifest, policy = self.fixture()
        key = 'linux-ubuntu24.04-amd64-gcc-emu-beam-vm'
        mutations = [lambda m: m['qualification'].pop(key),
            lambda m: m['qualification'][key].update(candidate='c' * 64),
            lambda m: m['qualification'][key].update(tests=[]),
            lambda m: m['qualification'][key].update(vm_results=[]),
            lambda m: m['qualification'][key]['vm_results'][0]['result'].update(complete=False),
            lambda m: m['qualification'][key]['vm_results'][0]['result'].update(cases=[]),
            lambda m: m['qualification'][key]['vm_results'][0]['result']['sdk'].update(build_id='0' * 64),
            lambda m: m['qualification'][key]['vm_results'][0]['result']['installation'].update(provenance_sha256='')]
        for status in ['failed', 'skipped', 'unexecuted', 'untested']:
            mutations += [lambda m, status=status: m['qualification'][key]['tests'][0].update(status=status),
                          lambda m, status=status: m['qualification'][key]['vm_results'][0]['result']['cases'][0].update(status=status)]
        def narrow_consistently(m):
            result = m['qualification'][key]['vm_results'][0]['result']
            result['expected_case_names'] = result['expected_case_names'][1:]
            result['expected_cases'] -= 1
            result['cases'] = result['cases'][1:]
            result['suites'][result['suite']]['passed'] -= 1
        mutations.append(narrow_consistently)
        mutations.append(lambda m: m['qualification'][key]['vm_results'][0]['result']['source_sha256'].update(
            {next(iter(m['qualification'][key]['vm_results'][0]['result']['source_sha256'])): '0' * 64}))
        for index, mutate in enumerate(mutations):
            with self.subTest(mutation=index):
                invalid = copy.deepcopy(manifest)
                mutate(invalid)
                with self.assertRaises(ValueError):
                    release.eligible(invalid, policy)

    def test_passing_ctest_without_vm_evidence_remains_ineligible(self):
        from types import SimpleNamespace
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            report = root / 'beam-vm.json'
            tests = [{'name': name, 'properties': []} for name in sorted(release.VM_TESTS)]
            report.with_suffix('.xml').write_text('<testsuite>' + ''.join(
                '<testcase name="' + item['name'] + '"/>' for item in tests) + '</testsuite>')
            args = SimpleNamespace(kind='beam-vm', build=root, report=report, record=None)
            with patch.object(release, 'inventory', return_value=tests), \
                    patch.object(release.subprocess, 'run', return_value=SimpleNamespace(returncode=0)), \
                    patch.object(release, 'qualification_host', return_value={}):
                with self.assertRaisesRegex(ValueError, 'qualification failed'):
                    release.ctest(args)
            result = release.read(report)
            self.assertFalse(result['eligible'])
            self.assertIn('mandatory VM suite evidence missing', result['evidence_error'])

    def test_disabled_vm_registration_cannot_reuse_passing_case_evidence(self):
        from types import SimpleNamespace
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            report = root / 'beam-vm.json'
            tests = [{'name': name, 'properties': [{'name': 'DISABLED', 'value': True}]}
                     for name in sorted(release.VM_TESTS)]
            report.with_suffix('.xml').write_text('<testsuite>' + ''.join(
                '<testcase name="' + item['name'] + '"/>' for item in tests) + '</testsuite>')
            for item in self.vm_results('emu'):
                release.write(root / 'results' / item['result']['suite'] / 'result.json', item['result'])
            args = SimpleNamespace(kind='beam-vm', build=root, report=report, record=None)
            with patch.object(release, 'inventory', return_value=tests), \
                    patch.object(release.subprocess, 'run', return_value=SimpleNamespace(returncode=0)), \
                    patch.object(release, 'qualification_host', return_value={}):
                with self.assertRaisesRegex(ValueError, 'qualification failed'):
                    release.ctest(args)
            result = release.read(report)
            self.assertFalse(result['eligible'])
            self.assertEqual({test['status'] for test in result['tests']}, {'unexecuted'})

    def test_existing_release_conflict_refuses_upload_and_publish(self):
        from types import SimpleNamespace
        manifest,policy=self.fixture()
        manifest.update(eligible=True,blockers=[])
        original_read=release.read
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp)
            release.write(root/'release-manifest.json',manifest)
            (root/'offline-verification.tar.gz').write_bytes(b'kit')
            (root/'sdk.tar.gz').write_bytes(b'new payload')
            self.kit(root)
            calls=[]
            def github(*args):
                calls.append(args)
                if args[:2]==('api','--paginate'):
                    if '/tags?' in args[-1]: return '[[]]'
                    return json.dumps([[{'tag_name':'v'+manifest['version']} ]])
                if args[:2]==('release','download'):
                    (Path(args[args.index('--dir')+1])/'sdk.tar.gz').write_bytes(b'old payload')
                return ''
            args=SimpleNamespace(directory=tmp,run='123',digest=release.digest(root/'release-manifest.json'))
            with patch.object(release,'read',side_effect=lambda p: policy if Path(p).name=='policy.json' else original_read(p)), \
                 patch.object(promote,'payloads',return_value={'sdk.tar.gz':release.asset(root/'sdk.tar.gz')}), \
                 patch.object(promote,'gh',side_effect=github):
                with self.assertRaisesRegex(ValueError,'conflict'): promote.publish(args)
            self.assertFalse(any(call[:2] in [('release','upload'),('release','create'),('release','edit')] for call in calls))
            expected=release.asset(root/'sdk.tar.gz')
            (root/'sdk.tar.gz').write_bytes(b'changed after preparation')
            with patch.object(release,'read',side_effect=lambda p: policy if Path(p).name=='policy.json' else original_read(p)), \
                 patch.object(promote,'payloads',return_value={'sdk.tar.gz':expected}), \
                 patch.object(promote,'gh') as network:
                with self.assertRaisesRegex(ValueError,'asset mismatch'): promote.publish(args)
                network.assert_not_called()

    def test_publication_retries_retain_identical_hosted_bytes(self):
        from types import SimpleNamespace
        manifest,policy=self.fixture()
        manifest.update(eligible=True,blockers=[])
        original_read=release.read
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp)
            release.write(root/'release-manifest.json',manifest)
            (root/'offline-verification.tar.gz').write_bytes(b'attested kit')
            (root/'sdk.tar.gz').write_bytes(b'qualified payload')
            hosted={}
            mutations=[]
            def github(*args):
                if args[:2]==('api','--paginate'):
                    if '/tags?' in args[-1]:
                        return json.dumps([[{'name':'v'+manifest['version'],'commit':{'sha':manifest['source']}}]] if hosted else [[]])
                    return json.dumps([[{'tag_name':'v'+manifest['version']}]] if hosted else [[]])
                if args[:2]==('release','download'):
                    for name,data in hosted.items():
                        (Path(args[args.index('--dir')+1])/name).write_bytes(data)
                if args[:2]==('release','create'):
                    mutations.append('create')
                    for name in ('sdk.tar.gz','release-manifest.json','offline-verification.tar.gz'):
                        hosted[name]=(root/name).read_bytes()
                if args[:2]==('release','upload'):
                    mutations.append('upload')
                return ''
            args=SimpleNamespace(directory=tmp,run='123',digest=release.digest(root/'release-manifest.json'))
            with patch.object(release,'read',side_effect=lambda p: policy if Path(p).name=='policy.json' else original_read(p)), \
                 patch.object(promote,'payloads',return_value={'sdk.tar.gz':release.asset(root/'sdk.tar.gz')}), \
                 patch.object(promote,'gh',side_effect=github):
                # Site records need the same archive descriptors that the real manifest carries.
                for record in manifest['candidates'].values(): record['archive']=release.asset(root/'sdk.tar.gz')
                release.write(root/'release-manifest.json',manifest)
                args.digest=release.digest(root/'release-manifest.json')
                self.kit(root)
                promote.publish(args)
                original_bytes=copy.deepcopy(hosted)
                promote.publish(args)
            self.assertEqual(hosted,original_bytes)
            self.assertEqual(mutations,['create'])
            self.assertEqual(release.read(root/'site-release.json')['manifest']['sha256'],args.digest)
            site_record=release.read(root/'site-release.json')
            self.assertEqual(release.validate_version_record(site_record),
                             release.version_metadata(manifest['version']))

    def test_blocked_publisher_performs_no_network_mutation(self):
        from types import SimpleNamespace
        manifest,policy=self.fixture()
        policy['promotion_enabled']=False
        manifest.update(eligible=False,blockers=['mandatory failures'])
        original_read=release.read
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp)/'candidate-manifest.json'
            release.write(path,manifest)
            args=SimpleNamespace(directory=tmp,digest=release.digest(path),run='123')
            with patch.object(release,'read',side_effect=lambda p: policy if Path(p).name=='policy.json' else original_read(p)), patch.object(promote,'gh') as network:
                with self.assertRaisesRegex(ValueError,'disabled'): promote.prepare(args)
                network.assert_not_called()

    def test_private_release_hosting_is_rejected(self):
        from types import SimpleNamespace
        manifest,policy=self.fixture()
        manifest.update(eligible=True,blockers=[])
        original_read=release.read
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp)/'candidate-manifest.json';release.write(path,manifest)
            args=SimpleNamespace(directory=tmp,digest=release.digest(path),run='123')
            with patch.object(release,'read',side_effect=lambda p: policy if Path(p).name=='policy.json' else original_read(p)), \
                 patch.object(promote,'gh',return_value='{"private":true,"visibility":"private"}') as network:
                with self.assertRaisesRegex(ValueError,'public release hosting'): promote.prepare(args)
                self.assertEqual(network.call_count,1)
                self.assertEqual(network.call_args.args,('api','repos/'+policy['repository']))

    def test_collection_reports_missing_qualification(self):
        from types import SimpleNamespace
        from contextlib import redirect_stdout
        manifest,policy=self.fixture()
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp)
            for flavor,record in manifest['candidates'].items():
                release.write(root/f'candidate-{flavor}.json',record)
            release.write(root/'policy.json',policy)
            output=root/'candidate-manifest.json'
            with redirect_stdout(io.StringIO()):
                release.collect(SimpleNamespace(directory=root,output=output,policy=root/'policy.json'))
            result=release.read(output)
            self.assertFalse(result['eligible'])
            self.assertTrue(any('nonpassing or missing report:' in message and message.endswith('-otp') for message in result['blockers']))
            self.assertEqual(set(result['candidate_digests']),set(policy['targets']+policy['integration_targets']))

    def test_publication_requires_protected_environment(self):
        environment={'protection_rules':[{'type':'required_reviewers','reviewers':[{'id':1}],
                                         'prevent_self_review':True}],
                     'deployment_branch_policy':{'protected_branches':True}}
        promote.protected_environment(environment)
        for invalid in [{}, {'protection_rules':[]},
                        dict(environment,deployment_branch_policy=None),
                        dict(environment,protection_rules=[{'type':'required_reviewers','reviewers':[{'id':1}]}])]:
            with self.assertRaises(ValueError): promote.protected_environment(invalid)

    def test_trusted_run_rejects_other_workflows_and_failed_jobs(self):
        manifest,policy=self.fixture()
        info={'event':'workflow_dispatch','path':policy['candidate_workflow'],
              'head_repository':{'full_name':policy['repository']},'head_branch':'master',
              'head_sha':manifest['source'],'id':123,'conclusion':'success'}
        jobs=[{'name':f'{kind} ({flavor})','conclusion':'success'} for kind in ['build','acquired','otp'] for flavor in ['emu','jit']]
        jobs.append({'name':'collect','conclusion':'success'})
        promote.trusted_run(info,manifest,policy,jobs)
        for key,bad in [('event','pull_request'),('head_branch','unreviewed'),('head_sha','f'*40),('path','other.yml')]:
            invalid=copy.deepcopy(info);invalid[key]=bad
            with self.assertRaises(ValueError): promote.trusted_run(invalid,manifest,policy,jobs)
        jobs[0]['conclusion']='failure'
        with self.assertRaises(ValueError): promote.trusted_run(info,manifest,policy,jobs)

    def test_missing_mandatory_registration_fails_before_execution(self):
        import argparse
        args=argparse.Namespace(build='unused',kind='otp',report='unused',record=None)
        with patch.object(release,'inventory',return_value=[]), patch.object(release.subprocess,'run') as execute:
            with self.assertRaisesRegex(ValueError,'mandatory tests missing'): release.ctest(args)
            execute.assert_not_called()

if __name__=='__main__': unittest.main()
