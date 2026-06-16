"""Container ownership, command transport and disposable-output boundaries."""
import os
from pathlib import Path
import subprocess
import tempfile
import tarfile
import importlib.util
import unittest
from unittest.mock import patch
import linux_container
from workspace import workspace


class LinuxContainer(unittest.TestCase):
    def test_extracted_consumer_interface(self):
        import sdk_snapshot
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary)
            archive=root/'interface.tar.gz'
            with tarfile.open(archive,'w:gz') as tar:
                for name in sdk_snapshot.CONSUMER_INTERFACE_FILES:
                    tar.add(sdk_snapshot.ROOT/name,arcname=name)
            extracted=root/'extracted'
            with tarfile.open(archive) as tar: tar.extractall(extracted,filter='data')
            spec=importlib.util.spec_from_file_location('extracted_linux_container',extracted/'ci/linux_container.py')
            module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
            with patch.object(subprocess,'run') as run:
                module.run_container(extracted,{'arch':'amd64','container':'selected'},root/'work',['literal λ','a b'])
            command=run.call_args_list[0].args[0]
            self.assertEqual(command[-2:],['literal λ','a b'])
            self.assertTrue((extracted/'ci/container_user.py').is_file())
            self.assertIn(f'type=bind,source={extracted},target=/source,readonly',command)

    def test_literal_transport_and_nonroot_identity(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary)
            row={'arch':'amd64','container':'alpine@sha256:test'}
            with patch.object(os,'getuid',return_value=1234), patch.object(os,'getgid',return_value=5678), \
                    patch.dict(os.environ,{'GITHUB_SHA':'abc','CMAKE_BUILD_PARALLEL_LEVEL':'3'}), \
                    patch.object(subprocess,'run') as run:
                linux_container.run_container(root,row,root/'work',['python3','check.py','a b',';literal','--extended'])
            command=run.call_args_list[0].args[0]
            self.assertIn('BEAM_CONTAINER_UID=1234',command)
            self.assertIn('BEAM_CONTAINER_GID=5678',command)
            self.assertIn(f'type=bind,source={root},target=/source,readonly',command)
            self.assertEqual(command[-5:],['python3','check.py','a b',';literal','--extended'])
            self.assertIn('linux/amd64',command)
            self.assertIn('CMAKE_BUILD_PARALLEL_LEVEL',command)
            self.assertEqual(run.call_args_list[-1].args[0][-1],command[command.index('--name')+1])

    def test_root_is_rejected(self):
        with patch.object(os,'getuid',return_value=0):
            with self.assertRaisesRegex(ValueError,'unprivileged'):
                linux_container.run_container('.',{},'.',[])

    def test_debian_keeps_bash_provisioning(self):
        with tempfile.TemporaryDirectory() as temporary:
            with patch.object(subprocess,'run') as run:
                linux_container.run_container(temporary,{'arch':'amd64','container':'debian:13'},temporary,['true'])
            command=run.call_args_list[0].args[0]
            self.assertEqual(command[command.index('debian:13')+1:][-3:],['bash','ci/linux-container.sh','true'])

    def test_owned_container_stopped_after_failure(self):
        with tempfile.TemporaryDirectory() as temporary:
            with patch.object(subprocess,'run',side_effect=[KeyboardInterrupt(),None]) as run:
                with self.assertRaises(KeyboardInterrupt):
                    linux_container.run_container(temporary,{'arch':'amd64','container':'test'},temporary,['false'])
            self.assertEqual(run.call_args_list[-1].args[0][:3],['docker','rm','--force'])

    def test_cleanup_preserves_transported_inputs_and_results(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary)
            (root/'payload').mkdir();(root/'payload'/'input').write_text('keep')
            for failure in (False,True):
                try:
                    with workspace(root) as scratch:
                        (scratch/'build').mkdir();(scratch/'build'/'binary').write_bytes(b'x'*1024)
                        (scratch/'Testing').mkdir();(scratch/'Testing'/'result.log').write_text('diagnostic')
                        (scratch/'inaccessible').mkdir();(scratch/'inaccessible'/'input').write_text('temporary')
                        (scratch/'inaccessible').chmod(0)
                        if failure: raise KeyboardInterrupt()
                except KeyboardInterrupt:
                    pass
                self.assertFalse(scratch.exists())
                self.assertEqual((root/'payload'/'input').read_text(),'keep')
            self.assertEqual(len(list((root/'diagnostics').glob('*/cleanup.json'))),2)


if __name__=='__main__':
    unittest.main()
