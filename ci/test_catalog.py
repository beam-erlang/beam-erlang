"""Boundary tests for matrix selection, cross-host isolation and archive identity."""
import importlib.util
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import catalog
import sys
sys.path.insert(0,str(catalog.ROOT/'release'))
import release

class Contracts(unittest.TestCase):
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
            root=Path(tmp);source=root/'source';source.mkdir();(source/'CMakeLists.txt').write_text('project(test)');(source/'BEAM_VERSION').write_text('17.0-r1')
            build=root/'build';(build/'bin').mkdir(parents=True);(build/'compiler').mkdir();(build/'compiler/verified.stamp').touch();(build/'CMakeCache.txt').write_text('BEAM_HOST_TOOLS_ROOT:PATH=\n')
            import platform
            suffix='.exe' if platform.system()=='Windows' else ''
            for name in ['beam_bootstrap','beam_seed_compiler','yielding_c_fun_host']:(build/'bin'/(name+suffix)).write_bytes(b'host')
            expected=module.record(source,build)
            (build/'host-tools.json').write_text(json.dumps(expected))
            arguments=['host_tools.py','verify','--source',str(source),'--build',str(build)]
            with patch.object(sys,'argv',arguments):module.main()
            (build/'bin'/('beam_bootstrap'+suffix)).write_bytes(b'changed')
            self.assertNotEqual(expected,module.record(source,build))
            with patch.object(sys,'argv',arguments), self.assertRaisesRegex(ValueError,'differ'):
                module.main()
            (source/'BEAM_VERSION').write_text('18.0-r1')
            self.assertNotEqual(expected['source'],module.record(source,build)['source'])
            with patch.object(module.platform,'machine',return_value='other-host'):
                self.assertNotEqual(expected['machine'],module.record(source,build)['machine'])
if __name__=='__main__':unittest.main()
