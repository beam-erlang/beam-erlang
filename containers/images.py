"""Build, qualify and promote OCI archives without rebuilding at publication."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'release'));import release
sys.path.insert(0,str(ROOT/'ci'));import catalog
REPOSITORY='ghcr.io/beam-erlang/beam'


def run(*args,**kwargs):return subprocess.run(list(map(str,args)),check=True,**kwargs)
def output(*args):return subprocess.check_output(list(map(str,args)),text=True).strip()


def build(a):
    row=catalog.target(a.target);work=a.work.resolve();work.mkdir(parents=True,exist_ok=True)
    payload=work/'payload';payload.mkdir(exist_ok=True)
    development=bool(a.ci_payload)
    if development:
        record=release.read(a.ci_payload/('candidate-'+a.target+'.json'));files=a.ci_payload
    else:
        tag=a.version or json.loads(output('gh','release','view','--repo','beam-erlang/beam-erlang','--json','tagName'))['tagName']
        run('gh','release','download',tag,'--repo','beam-erlang/beam-erlang','--pattern','release-manifest.json','--dir',payload)
        run('gh','attestation','verify',payload/'release-manifest.json','--repo','beam-erlang/beam-erlang','--signer-workflow','beam-erlang/beam-erlang/.github/workflows/release-publish.yml')
        manifest=release.read(payload/'release-manifest.json')
        if manifest.get('eligible') is not True:raise ValueError('release is not qualified')
        record=manifest['candidates'][a.target];files=payload
        run('gh','release','download',tag,'--repo','beam-erlang/beam-erlang','--pattern',record['archive']['name'],'--dir',payload)
    release.validate_candidate_version(record)
    if record['target']!=a.target:raise ValueError('wrong image SDK target')
    archive=files/record['archive']['name'];release.verify_asset(archive,record['archive'])
    sdk=release.extract(archive,work/'extracted')
    context=work/'context';context.mkdir();shutil.copytree(sdk,context/'sdk',symlinks=True)
    image=work/(a.target+'.oci.tar')
    run('docker','buildx','build','--platform','linux/'+row['arch'],'--provenance=false','--build-arg','BASE='+row['container'],'--label','org.opencontainers.image.version='+record['version'],'--label','org.erlangsters.sdk.sha256='+record['archive']['sha256'],'--output','type=oci,dest='+str(image),'-f',ROOT/'containers/Dockerfile',context)
    local='beam-acceptance:'+a.target
    run('skopeo','copy','oci-archive:'+str(image),'docker-daemon:'+local)
    fixture=ROOT/'tests/fixtures/hello_world.erl'
    result=output('docker','run','--rm','-v',str(fixture)+':/work/hello_world.erl:ro',local,'beam','run','hello_world.erl')
    if result!='Hello, World!':raise ValueError('container source execution failed')
    run('docker','run','--rm','-v',str(ROOT/'tests/installed/sdk/consumer/first-call')+':/consumer:ro',local,'sh','-ec','cmake -S /consumer -B /tmp/consumer -G Ninja && cmake --build /tmp/consumer && /tmp/consumer/first-call')
    packages=output('docker','run','--rm',local,'sh','-ec','if command -v dpkg-query >/dev/null; then dpkg-query -W; else apk info -vv; fi')
    release.write(work/(a.target+'-image.json'),{'schema':1,'version':record['version'],'target':a.target,'base':row['container'],'sdk':record['archive'],'image':release.asset(image),'eligible':not development,'development':development,'packages':packages,'tests':['source-execution','installed-sdk-first-call'],'source':output('git','-C',ROOT,'rev-parse','HEAD'),'run_id':os.environ.get('GITHUB_RUN_ID')})


def collect(a):
    a.work.mkdir(parents=True,exist_ok=True)
    records=[release.read(p) for p in sorted(a.inputs.rglob('*-image.json'))]
    expected={r['id'] for r in catalog.targets() if r['family']=='linux'}
    if len(records)!=len(expected) or {r['target'] for r in records}!=expected:raise ValueError('complete Linux image matrix required')
    if len({r['version'] for r in records})!=1 or len({r['source'] for r in records})!=1:raise ValueError('mixed image identities')
    groups={}
    for r in records:
        row=catalog.target(r['target']);groups.setdefault(row['baseline']+'-'+row['flavor'],[]).append(r)
    result=[]
    for group,items in groups.items():
        with tempfile.TemporaryDirectory() as tmp:
            layout=Path(tmp);(layout/'blobs/sha256').mkdir(parents=True);manifests=[]
            for record in items:
                path=next(a.inputs.rglob(record['image']['name']));release.verify_asset(path,record['image'])
                with tempfile.TemporaryDirectory() as extracted:
                    with tarfile.open(path) as tar:tar.extractall(extracted,filter='data')
                    src=Path(extracted);index=release.read(src/'index.json')
                    if len(index['manifests'])!=1:raise ValueError('one image manifest per architecture required')
                    descriptor=index['manifests'][0];descriptor['platform']={'os':'linux','architecture':catalog.target(record['target'])['arch']};manifests.append(descriptor)
                    for blob in (src/'blobs/sha256').iterdir():
                        if release.digest(blob)!=blob.name:raise ValueError('OCI blob digest mismatch')
                        shutil.copy2(blob,layout/'blobs/sha256'/blob.name)
            content=json.dumps({'schemaVersion':2,'mediaType':'application/vnd.oci.image.index.v1+json','manifests':manifests},separators=(',',':')).encode()
            digest=hashlib.sha256(content).hexdigest();(layout/'blobs/sha256'/digest).write_bytes(content)
            release.write(layout/'oci-layout',{'imageLayoutVersion':'1.0.0'})
            release.write(layout/'index.json',{'schemaVersion':2,'manifests':[{'mediaType':'application/vnd.oci.image.index.v1+json','digest':'sha256:'+digest,'size':len(content),'annotations':{'org.opencontainers.image.ref.name':'beam'}}]})
            archive=a.work/(group+'.oci.tar')
            with tarfile.open(archive,'w') as tar:
                for path in layout.iterdir():tar.add(path,arcname=path.name)
            version=items[0]['version'];tag=version+'-'+group+'-i'+digest[:12]
            result.append({'tag':tag,'group':group,'digest':'sha256:'+digest,'archive':release.asset(archive)})
    release.write(a.work/'images.json',{'schema':1,'version':records[0]['version'],'source':records[0]['source'],'run_id':os.environ.get('GITHUB_RUN_ID'),'eligible':all(r['eligible'] for r in records),'records':records,'images':result})


def publish(a):
    path=a.work/'images.json'
    if release.digest(path)!=a.digest:raise ValueError('reviewed image manifest differs')
    manifest=release.read(path)
    if manifest['eligible'] is not True or manifest['run_id']!=a.run:raise ValueError('image candidate is not eligible')
    info=json.loads(output('gh','api','repos/beam-erlang/beam-erlang/actions/runs/'+a.run))
    if info['path']!='.github/workflows/containers-candidate.yml' or info['head_sha']!=manifest['source'] or info['conclusion']!='success' or info['event']=='pull_request':raise ValueError('untrusted image candidate origin')
    release.check_line(manifest['version'],info['head_branch'],release.read(ROOT/'release/policy.json'))
    if not json.loads(output('gh','api','repos/beam-erlang/beam-erlang/branches/'+info['head_branch'])).get('protected'):
        raise ValueError('image source branch must be protected')
    run('gh','attestation','verify',path,'--repo','beam-erlang/beam-erlang','--signer-workflow','beam-erlang/beam-erlang/.github/workflows/containers-candidate.yml','--source-digest',manifest['source'])
    for image in manifest['images']:
        archive=a.work/image['archive']['name'];release.verify_asset(archive,image['archive'])
        destination=REPOSITORY+':'+image['tag']
        current=subprocess.run(['skopeo','inspect','--format','{{.Digest}}','docker://'+destination],text=True,capture_output=True)
        if current.returncode==0 and current.stdout.strip()!=image['digest']:raise ValueError('immutable image tag already differs')
        if current.returncode and 'manifest unknown' not in current.stderr.lower() and 'name unknown' not in current.stderr.lower():
            raise ValueError('cannot establish whether immutable image tag exists: '+current.stderr)
        run('skopeo','copy','--all','--preserve-digests','oci-archive:'+str(archive),'docker://'+destination)
        if output('skopeo','inspect','--raw','docker://'+destination):
            raw=subprocess.check_output(['skopeo','inspect','--raw','docker://'+destination])
            if 'sha256:'+hashlib.sha256(raw).hexdigest()!=image['digest']:raise ValueError('published OCI index differs')
        if a.update_channels:
            channel=release.version_metadata(manifest['version'])['channel']
            run('skopeo','copy','--all','--preserve-digests','docker://'+destination,'docker://'+REPOSITORY+':'+channel+'-'+image['group'])


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('command',choices=['build','collect','publish']);p.add_argument('--work',type=Path,required=True);p.add_argument('--target');p.add_argument('--version');p.add_argument('--ci-payload',type=Path);p.add_argument('--inputs',type=Path);p.add_argument('--run');p.add_argument('--digest');p.add_argument('--update-channels',action='store_true');a=p.parse_args();globals()[a.command](a)

if __name__=='__main__':main()
