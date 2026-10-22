"""OCI transport identity checks, independent of Docker/runtime qualification."""
import importlib.util
import json
from pathlib import Path
import tarfile
import tempfile
from types import SimpleNamespace
import unittest
import catalog

spec=importlib.util.spec_from_file_location('beam_images',catalog.ROOT/'containers/images.py')
images=importlib.util.module_from_spec(spec);spec.loader.exec_module(images)
release=images.release


class ContainerTransport(unittest.TestCase):
    def inputs(self,root):
        inputs=root/'inputs';inputs.mkdir()
        for row in catalog.targets():
            if row['family']!='linux':continue
            layout=root/'layout';(layout/'blobs/sha256').mkdir(parents=True,exist_ok=True)
            # Two real descriptor objects suffice to exercise OCI transport; no
            # claim is made that these unit-test images can boot a runtime.
            content=json.dumps({'schemaVersion':2,'target':row['id']}).encode()
            import hashlib
            digest=hashlib.sha256(content).hexdigest()
            (layout/'blobs/sha256'/digest).write_bytes(content)
            release.write(layout/'index.json',{'schemaVersion':2,'manifests':[{'mediaType':'application/vnd.oci.image.manifest.v1+json','digest':'sha256:'+digest,'size':len(content)}]})
            archive=inputs/(row['id']+'.oci.tar')
            with tarfile.open(archive,'w') as tar:
                for path in layout.iterdir():tar.add(path,arcname=path.name)
            release.write(inputs/(row['id']+'-image.json'),{'target':row['id'],'version':'17.0-r1-preview.1','source':'a'*40,'eligible':False,'image':release.asset(archive)})
        return inputs

    def test_multiarch_index_preserves_both_architectures_and_pr_ineligibility(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);inputs=self.inputs(root);out=root/'candidate'
            images.collect(SimpleNamespace(inputs=inputs,work=out))
            manifest=release.read(out/'images.json')
            self.assertFalse(manifest['eligible']);self.assertEqual(len(manifest['images']),4)
            for record in manifest['images']:
                with tarfile.open(out/record['archive']['name']) as tar:
                    data=tar.extractfile('blobs/sha256/'+record['digest'].removeprefix('sha256:')).read()
                import hashlib
                self.assertEqual('sha256:'+hashlib.sha256(data).hexdigest(),record['digest'])
                self.assertEqual({m['platform']['architecture'] for m in json.loads(data)['manifests']},{'amd64','arm64'})

    def test_modified_archive_and_duplicate_target_are_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);inputs=self.inputs(root)
            record=next(inputs.glob('*-image.json'))
            (inputs/'duplicate-image.json').write_bytes(record.read_bytes())
            with self.assertRaisesRegex(ValueError,'complete Linux image matrix'):
                images.collect(SimpleNamespace(inputs=inputs,work=root/'duplicate'))
            (inputs/'duplicate-image.json').unlink()
            archive=inputs/release.read(record)['image']['name'];archive.write_bytes(b'altered')
            with self.assertRaisesRegex(ValueError,'asset mismatch'):
                images.collect(SimpleNamespace(inputs=inputs,work=root/'modified'))

if __name__=='__main__':unittest.main()
