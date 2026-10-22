"""Build and publish immutable CI-only SDK snapshots without package bootstrapping."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tarfile
import tempfile
import catalog

ROOT=catalog.ROOT


def run(*argv,**kwargs):
    return subprocess.run([str(a) for a in argv],check=True,**kwargs)


def digest(path):
    h=hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda:stream.read(1024*1024),b''):h.update(chunk)
    return h.hexdigest()


def asset(path):return {'name':path.name,'sha256':digest(path),'size':path.stat().st_size}

def producer():
    revision=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip()
    if subprocess.check_output(['git','status','--porcelain','--untracked-files=normal'],cwd=ROOT,text=True).strip():
        raise ValueError('hosted SDK snapshots require a clean reviewed source commit')
    identity={'repository':os.environ['GITHUB_REPOSITORY'],'source_revision':revision,
              'workflow':'sdk-snapshot.yml','run_id':os.environ['GITHUB_RUN_ID'],
              'run_attempt':os.environ['GITHUB_RUN_ATTEMPT']}
    if identity['repository']!='beam-erlang/beam-erlang' or not re.fullmatch('[a-f0-9]{40}',revision):
        raise ValueError('unsupported SDK producer')
    if not all(str(identity[k]).isdigit() for k in ('run_id','run_attempt')):raise ValueError('invalid producer run')
    if os.environ.get('GITHUB_SHA')!=revision:raise ValueError('workflow checkout revision mismatch')
    return identity


def archive_tree(source,destination):
    with tarfile.open(destination,'w:gz') as archive:
        for path in sorted(source.iterdir()):archive.add(path,arcname=path.name)


def build(a):
    row=catalog.target(a.target);work=a.work.resolve();work.mkdir(parents=True,exist_ok=True)
    if not row['release'] or row['family']=='integration':raise ValueError('snapshot requires a canonical SDK producer')
    if row['container'] and not a.inside:
        command=['docker','run','--rm','--init','-v',f'{ROOT}:/source','-v',f'{work}:/work','-w','/source']
        for name in ('GITHUB_REPOSITORY','GITHUB_SHA','GITHUB_RUN_ID','GITHUB_RUN_ATTEMPT'):
            command+=['-e',name]
        command += [row['container'],'bash' if 'debian' in row['container'] else 'sh','ci/linux-container.sh','python3','ci/sdk_snapshot.py','build','--target',a.target,'--work','/work','--inside']
        run(*command);return
    identity=producer();out=work/'payload';out.mkdir(exist_ok=True)
    build_dir=work/'build';prefix=work/'prefix'
    run('cmake','--preset',row['preset'],'-B',build_dir,'-DBEAM_BUNDLE_PACKAGE_TOOL=OFF','-DCMAKE_INSTALL_PREFIX='+str(prefix),cwd=ROOT)
    run('cmake','--build',build_dir,'--parallel',os.environ.get('CMAKE_BUILD_PARALLEL_LEVEL','2'),cwd=ROOT)
    run('cmake','--install',build_dir,cwd=ROOT)
    tests=['target-sdk-layout'] if row['qualification']!='desktop' else ['installed-compile','installed-application']
    available=json.loads(subprocess.check_output(['ctest','--test-dir',str(build_dir),'--show-only=json-v1'],text=True))['tests']
    names={t['name'] for t in available}
    # Bootstrap checks are their own evidence, never a distribution/OTP release gate.
    if not set(tests)<=names:raise ValueError('SDK bootstrap acceptance is not registered: '+str(set(tests)-names))
    run('ctest','--test-dir',build_dir,'--output-on-failure','-R','^('+'|'.join(tests)+')$')
    filename=out/(row['id']+'.tar.gz');archive_tree(prefix,filename)
    record={'schema':1,'kind':'beam-ci-sdk','target':row['id'],'producer':identity,
            'archive':asset(filename),'bootstrap_checks':tests,
            'target_metadata_sha256':digest(prefix/'lib/beam/target.toml')}
    (out/(row['id']+'.json')).write_text(json.dumps(record,indent=2)+'\n')


def assemble(a):
    identity=producer();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
    records={}
    required={r['id'] for r in catalog.targets() if r['release'] and r['family']!='integration'}
    tag='ci-sdk-'+identity['source_revision']+'-'+identity['run_id']+'-'+identity['run_attempt']
    base='https://github.com/'+identity['repository']+'/releases/download/'+tag+'/'
    for path in a.inputs.rglob('*.json'):
        record=json.loads(path.read_text())
        if record.get('kind')!='beam-ci-sdk':continue
        if record['producer']!=identity or record['target'] in records:raise ValueError('inconsistent or duplicate SDK producer')
        archive=path.parent/record['archive']['name']
        if asset(archive)!=record['archive']:raise ValueError('altered SDK archive')
        import shutil
        shutil.copy2(archive,out/archive.name)
        record['archive']['url']=base+archive.name;records[record['target']]=record
    if set(records)!=required:raise ValueError('missing canonical SDK snapshots: '+str(required-set(records)))
    # Export only BEAM-owned orchestration/toolchain inputs. No test fixtures or
    # package production code is part of this versioned transport interface.
    interface=out/'consumer-interface-v1.tar.gz'
    files=['ci/executor_v1.py','ci/provision.py','ci/catalog.py','ci/targets.json','ci/toolchains.json',
           'ci/windows-env.cmd','ci/linux-container.sh','cmake/toolchains/android.cmake',
           'cmake/toolchains/ios-device.cmake','cmake/toolchains/ios-simulator.cmake']
    with tarfile.open(interface,'w:gz') as tar:
        for name in files:tar.add(ROOT/name,arcname=name)
    interface_record=dict(asset(interface),url=base+interface.name,interface='beam-consumer-v1')
    manifest={'schema':1,'kind':'beam-ci-sdk-snapshot','producer':identity,'tag':tag,
              'sdks':records,'consumer_interface':interface_record,'product_release_eligible':False}
    (out/'snapshot.json').write_text(json.dumps(manifest,indent=2,sort_keys=True)+'\n')


def matrix(_):
    catalog.validate()
    print(json.dumps({'include':[r for r in catalog.targets() if r['release'] and r['family']!='integration']}))


def publish(a):
    policy=json.loads((ROOT/'ci/sdk-snapshot-policy.json').read_text())
    if not policy.get('publication_enabled'):raise ValueError('CI SDK snapshot publication is disabled')
    identity=producer();manifest=json.loads((a.input/'snapshot.json').read_text())
    if manifest.get('kind')!='beam-ci-sdk-snapshot' or manifest['producer']!=identity or manifest.get('product_release_eligible') is not False:
        raise ValueError('invalid CI-only snapshot provenance')
    repo=identity['repository'];tag=manifest['tag']
    files=[a.input/'snapshot.json']
    for record in [*(r['archive'] for r in manifest['sdks'].values()),manifest['consumer_interface']]:
        path=a.input/record['name']
        if path.name!=record['name'] or path.stat().st_size!=record['size'] or digest(path)!=record['sha256']:raise ValueError('altered snapshot asset')
        files.append(path)
    found=subprocess.run(['gh','release','view',tag,'--repo',repo,'--json','isPrerelease,targetCommitish'],capture_output=True,text=True)
    if found.returncode:
        run('gh','release','create',tag,'--repo',repo,'--target',identity['source_revision'],'--prerelease',
            '--title',tag,'--notes','CI-only installed SDK snapshot. Not a qualified BEAM product release.')
    else:
        existing=json.loads(found.stdout)
        if not existing['isPrerelease'] or existing['targetCommitish']!=identity['source_revision']:raise ValueError('conflicting existing snapshot release')
    for path in files:
        with tempfile.TemporaryDirectory() as tmp:
            downloaded=subprocess.run(['gh','release','download',tag,'--repo',repo,'--pattern',path.name,'--dir',tmp],capture_output=True)
            if downloaded.returncode==0:
                previous=Path(tmp)/path.name
                if previous.stat().st_size!=path.stat().st_size or digest(previous)!=digest(path):raise ValueError('conflicting immutable snapshot asset: '+path.name)
                continue
        # No --clobber: conflicts and publication races fail closed.
        run('gh','release','upload',tag,path,'--repo',repo)


if __name__=='__main__':
    p=argparse.ArgumentParser();sub=p.add_subparsers(dest='command',required=True)
    sub.add_parser('matrix')
    b=sub.add_parser('build');b.add_argument('--target',required=True);b.add_argument('--work',type=Path,required=True);b.add_argument('--inside',action='store_true')
    m=sub.add_parser('assemble');m.add_argument('--inputs',type=Path,required=True);m.add_argument('--output',type=Path,required=True)
    r=sub.add_parser('publish');r.add_argument('--input',type=Path,required=True)
    a=p.parse_args();{'matrix':matrix,'build':build,'assemble':assemble,'publish':publish}[a.command](a)
