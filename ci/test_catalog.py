"""Boundary tests for matrix selection, cross-host isolation and archive identity."""
import importlib.util
import json
import os
import runpy
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import catalog
import sys
sys.path.insert(0,str(catalog.ROOT/'release'))
import release

class Contracts(unittest.TestCase):
    def test_unavailable_integration_emits_separate_nonpassing_vm_report(self):
        target = 'linux-ubuntu24.04-amd64-gcc-emu'
        row = dict(catalog.target(target), family='integration', execution_reason='executor unavailable')
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            with patch.object(catalog, 'target', return_value=row), \
                    patch.object(sys, 'argv', ['unexecuted.py', '--target', target, '--output', temporary]):
                runpy.run_path(str(catalog.ROOT / 'ci/unexecuted.py'), run_name='__main__')
            report = release.read(output / (target + '-beam-vm.json'))
            self.assertFalse(report['eligible'])
            self.assertEqual({item['name'] for item in report['tests']}, release.VM_TESTS)
            self.assertEqual({item['status'] for item in report['tests']}, {'unexecuted'})
            self.assertTrue((output / (target + '-otp.json')).is_file())
            self.assertFalse((output / (target + '-beam.json')).exists())

    def test_matrix_canonical_android_and_windows_variants(self):
        catalog.validate()
        android=[r for r in catalog.targets() if r['family']=='android']
        self.assertEqual({r['host'] for r in android},{'linux','windows','macos'})
        self.assertEqual({r['host'] for r in android if r['release']},{'linux'})
        windows=[r for r in catalog.targets() if r['family']=='windows']
        self.assertEqual({(r['arch'],r['compiler']) for r in windows},{('amd64','msvc'),('amd64','ucrt64'),('amd64','clang64'),('arm64','msvc'),('arm64','clangarm64')})
    def test_disabled_arm_executor_is_not_removed(self):
        with patch.dict(os.environ,{},clear=True):rows=catalog.matrix('android')['include']
        arm=[r for r in rows if r['arch']=='arm64']
        self.assertTrue(arm);self.assertTrue(all(not r['execution_enabled'] for r in arm))
        with patch.dict(os.environ,{'BEAM_ANDROID_ARM64_RUNNER':'isolated-mac'}):rows=catalog.matrix('android')['include']
        self.assertTrue(all(r['execution_enabled'] and r['test_runner']=='isolated-mac' for r in rows if r['arch']=='arm64'))
    def test_zip_roundtrip_and_traversal(self):
        import zipfile
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);tree=root/'tree';tree.mkdir();(tree/'beam.exe').write_bytes(b'target')
            release.archive_tree(tree,root/'sdk.zip','sdk')
            installed=release.extract(root/'sdk.zip',root/'SDK ü')
            self.assertEqual((installed/'beam.exe').read_bytes(),b'target')
            for name in ['../escape','sdk/../../escape','C:/escape','sdk\\escape']:
                with zipfile.ZipFile(root/'bad.zip','w') as z:z.writestr(name,b'bad')
                with self.assertRaises(ValueError):release.extract(root/'bad.zip',root/'bad')
                self.assertFalse((root/'bad').exists())
    def test_host_manifest_rejects_source_host_and_payload_mismatch(self):
        spec=importlib.util.spec_from_file_location('host_tools',catalog.ROOT/'cmake/host_tools.py');module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp).resolve();source=root/'source';source.mkdir();(source/'CMakeLists.txt').write_text('project(test)');(source/'BEAM_VERSION').write_text('17.0-r1')
            (source/'tools').mkdir()
            (source/'tools/stage-documentation.py').write_text('documentation staging')
            build=root/'build';(build/'bin').mkdir(parents=True);(build/'compiler').mkdir();(build/'compiler/verified.stamp').touch();(build/'CMakeCache.txt').write_text('BEAM_HOST_TOOLS_ROOT:PATH=\n')
            import platform
            suffix='.exe' if platform.system()=='Windows' else ''
            for name in ['beam_bootstrap','beam_seed_compiler','yielding_c_fun_host']:(build/'bin'/(name+suffix)).write_bytes(b'host')
            expected=module.record(source,build)
            relocated = source / 'src/erts/preloaded/ebin/core.beam'
            relocated.parent.mkdir(parents=True)
            relocated.write_bytes(b'core')
            self.assertNotEqual(expected['source'], module.record(source,build)['source'])
            expected=module.record(source,build)
            (build/'host-tools.json').write_text(json.dumps(expected))
            arguments=['host_tools.py','verify','--source',str(source),'--build',str(build)]
            with patch.object(sys,'argv',arguments):module.main()
            relocated.write_bytes(b'changed-core')
            with patch.object(sys,'argv',arguments), self.assertRaisesRegex(ValueError,'differ'):
                module.main()
            relocated.write_bytes(b'core')
            documentation_tool = source / 'tools/stage-documentation.py'
            documentation_tool.write_text('changed documentation staging')
            self.assertNotEqual(expected['source'],module.record(source,build)['source'])
            with patch.object(sys,'argv',arguments), self.assertRaisesRegex(ValueError,'differ'):
                module.main()
            documentation_tool.write_text('documentation staging')
            (build/'bin'/('beam_bootstrap'+suffix)).write_bytes(b'changed')
            self.assertNotEqual(expected,module.record(source,build))
            with patch.object(sys,'argv',arguments), self.assertRaisesRegex(ValueError,'differ'):
                module.main()
            (source/'BEAM_VERSION').write_text('18.0-r1')
            self.assertNotEqual(expected['source'],module.record(source,build)['source'])
            with patch.object(module.platform,'machine',return_value='other-host'):
                self.assertNotEqual(expected['machine'],module.record(source,build)['machine'])
class ConsumerBuild(unittest.TestCase):
    def setUp(self):
        spec=importlib.util.spec_from_file_location('target_execute',catalog.ROOT/'tests/target/execute.py')
        self.runner=importlib.util.module_from_spec(spec);spec.loader.exec_module(self.runner)
        temporary=tempfile.TemporaryDirectory();self.addCleanup(temporary.cleanup)
        self.root=Path(temporary.name)
        for name in ('sdk','support'):
            tree=self.root/name;tree.mkdir();(tree/'input').write_text(name)
            release.archive_tree(tree,self.root/(name+'.tar.gz'),name)
        version=(catalog.ROOT/'BEAM_VERSION').read_text().strip()
        self.record=self.root/'candidate.json'
        release.write(self.record,{'schema':1,'version':version,**release.version_metadata(version),
            'target':'android-arm64-ndk-emu-linux','sdk':{'identity':version,'build-id':'d'*64},
            'archive':release.asset(self.root/'sdk.tar.gz'),
            'acceptance':release.asset(self.root/'support.tar.gz')})

    def invoke(self,*options,fail_stage=None):
        calls=[]
        def compile(*args):
            calls.append(list(map(str,args)))
            if fail_stage is not None and len(calls)==fail_stage:
                raise RuntimeError('consumer compilation failed')
        arguments=['execute.py','--record',str(self.record),'--work',str(self.root/'work'),
                   '--report',str(self.root/'report.json'),*options]
        with patch.object(sys,'argv',arguments),patch.dict(os.environ,{'BEAM_TEST_KEEP_WORK':'0'}), \
                patch.object(self.runner,'run',side_effect=compile), \
                patch.object(self.runner,'android') as android,patch.object(self.runner,'simulator') as simulator:
            status=0
            try:self.runner.main()
            except SystemExit as error:status=error.code
        return release.read(self.root/'report.json'),calls,android,simulator,status

    def test_build_only_compiles_without_execution_and_cleans_payloads(self):
        report,calls,android,simulator,status=self.invoke('--build-only')
        self.assertEqual(status,0);self.assertEqual(len(calls),3)
        self.assertEqual(report['kind'],'consumer-build');self.assertFalse(report['eligible'])
        self.assertEqual(report['status'],'passed');self.assertEqual(report['target_execution'],'untested')
        self.assertEqual(report['candidate'],release.digest(self.record))
        self.assertEqual(report['tests'],[{'name':'acquired-sdk-consumer-build','status':'passed'}])
        self.assertTrue(any(arg.startswith('-DCMAKE_TOOLCHAIN_FILE=') and 'SDK ü/' in arg
                            and arg.endswith('/lib/cmake/BEAM/BEAMToolchain.cmake') for arg in calls[0]))
        android.assert_not_called();simulator.assert_not_called()
        self.assertFalse(any(p.name in ('sdk','support','consumer','deployment') for p in (self.root/'work').rglob('*')))
        self.assertTrue(next((self.root/'work').rglob('cleanup.json')).is_file())

    def test_build_only_reports_archive_and_compilation_failures(self):
        for stage in (1,2,3):
            with self.subTest(stage=stage):
                report,calls,android,simulator,status=self.invoke('--build-only',fail_stage=stage)
                self.assertEqual(status,1);self.assertEqual(report['status'],'failed')
                self.assertFalse(report['eligible']);self.assertEqual(report['tests'][-1]['status'],'failed')
                android.assert_not_called();simulator.assert_not_called()
        for name in ('sdk','support'):
            path=self.root/(name+'.tar.gz');saved=path.read_bytes();path.write_bytes(saved+b'tampered')
            with self.subTest(archive=name):
                report,calls,android,simulator,status=self.invoke('--build-only')
                self.assertEqual(status,1);self.assertEqual(calls,[])
                self.assertIn('asset mismatch',report['tests'][-1]['error'])
            path.write_bytes(saved)
        self.record.write_text('{}')
        report,calls,android,simulator,status=self.invoke('--build-only')
        self.assertEqual(status,1);self.assertEqual(calls,[])
        self.assertIn('unsupported candidate schema',report['tests'][-1]['error'])

    def test_default_and_extended_still_execute(self):
        for options,count in (([],1),(['--extended'],2)):
            with self.subTest(options=options):
                report,calls,android,simulator,status=self.invoke(*options)
                self.assertEqual(status,0);self.assertEqual(android.call_count,count)
                self.assertEqual(report['kind'],'acquired');self.assertTrue(report['eligible'])
                self.assertIn({'name':'acquired-sdk-runtime','status':'passed'},report['tests'])
                simulator.assert_not_called()

    def test_interrupted_build_records_failure_and_cleans_inputs(self):
        arguments=['execute.py','--build-only','--record',str(self.record),
                   '--work',str(self.root/'work'),'--report',str(self.root/'report.json')]
        with patch.object(sys,'argv',arguments),patch.dict(os.environ,{'BEAM_TEST_KEEP_WORK':'0'}), \
                patch.object(self.runner,'run',side_effect=KeyboardInterrupt),self.assertRaises(KeyboardInterrupt):
            self.runner.main()
        report=release.read(self.root/'report.json')
        self.assertEqual(report['status'],'failed');self.assertFalse(report['eligible'])
        self.assertFalse(any(p.name in ('sdk','support','consumer','deployment') for p in (self.root/'work').rglob('*')))

if __name__=='__main__':unittest.main()
