"""Run acquired-SDK embedding checks through Android or Apple simulators."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'release'))
import release
sys.path.insert(0,str(ROOT/'ci'))
import catalog
import executor_v1


def run(*args,**kwargs):
    return subprocess.run(list(map(str,args)),check=True,**kwargs)


def android(sdk,work,binaries,arch,page16=False):
    with executor_v1.android_session(work,arch,16384 if page16 else 4096) as adb:
        remote='/data/local/tmp/beam-sdk-consumer'
        run(*adb,'shell','rm','-rf',remote);run(*adb,'shell','mkdir','-p',remote+'/lib')
        run(*adb,'push',sdk/'lib',remote)
        for binary in binaries:
            run(*adb,'push',binary,remote+'/'+binary.name)
            run(*adb,'shell','chmod','755',remote+'/'+binary.name)
            run(*adb,'shell',remote+'/'+binary.name,timeout=300)
        bundle=work/'deployment'
        run(*adb,'push',bundle,remote)
        deployed=remote+'/deployment'
        command=[*adb,'shell',deployed+'/bin/deployment_host']
        for mode in ['boot','file','network','zlib']:
            run(*command,mode,deployed,timeout=300)
        # Exercise missing assets and bytecode identity after successful entry.
        asset=deployed+'/lib/beam/boot/ebin/beam_boot.beam'
        run(*adb,'shell','mv',asset,asset+'.saved')
        if subprocess.run(command+['boot',deployed],timeout=300).returncode==0:
            raise ValueError('missing mobile boot asset was accepted')
        run(*adb,'shell','cp',deployed+'/lib/beam/library/ebin/beam_time.beam',asset)
        if subprocess.run(command+['boot',deployed],timeout=300).returncode==0:
            raise ValueError('mismatched mobile boot asset was accepted')
        run(*adb,'shell','mv',asset+'.saved',asset)
        run(*command,'boot',deployed,timeout=300)

def simulator(binaries,bundle):
    with executor_v1.simulator_session() as udid:
        for binary in binaries:
            # Spawn the simulator executable directly to retain its real exit status.
            run('xcrun','simctl','spawn',udid,binary,timeout=300)
        app=bundle/'deployment_host.app'
        command=['xcrun','simctl','spawn',udid,str(app/'deployment_host')]
        for mode in ['boot','file','network','zlib']:
            run(*command,mode,app,timeout=300)
        asset=app/'lib/beam/boot/ebin/beam_boot.beam'
        saved=asset.read_bytes();asset.unlink()
        try:
            if subprocess.run(command+['boot',str(app)],timeout=300).returncode==0:
                raise ValueError('missing simulator boot asset was accepted')
            asset.write_bytes((app/'lib/beam/library/ebin/beam_time.beam').read_bytes())
            if subprocess.run(command+['boot',str(app)],timeout=300).returncode==0:
                raise ValueError('mismatched simulator boot asset was accepted')
        finally:asset.write_bytes(saved)
        run(*command,'boot',app,timeout=300)


def main():
    p=argparse.ArgumentParser();p.add_argument('--record',type=Path,required=True);p.add_argument('--work',type=Path,required=True);p.add_argument('--report',type=Path,required=True);p.add_argument('--extended',action='store_true');a=p.parse_args()
    record=release.read(a.record);release.validate_candidate_version(record);row=catalog.target(record['target'])
    work=a.work.resolve();work.mkdir(parents=True,exist_ok=True)
    for field in ['archive','acceptance']:release.verify_asset(a.record.parent/record[field]['name'],record[field])
    sdk=release.extract(a.record.parent/record['archive']['name'],work/'SDK ü')
    support=release.extract(a.record.parent/record['acceptance']['name'],work/'support')
    results=[]
    try:
        build=work/'consumer'
        flags=['-DCMAKE_PREFIX_PATH='+str(sdk),'-DBEAM_TEST_ANSWER_HEADER='+str(support/'answer_beam.h')]
        if row['family']=='android':
            flags+=['-DCMAKE_TOOLCHAIN_FILE='+str(ROOT/'cmake/toolchains/android.cmake'),'-DANDROID_ABI='+('arm64-v8a' if row['arch']=='arm64' else 'x86_64')]
        else:flags+=['-DCMAKE_TOOLCHAIN_FILE='+str(ROOT/'cmake/toolchains/ios-'+('device' if row['executor']=='ios-device' else 'simulator')+'.cmake')]
        run('cmake','-S',Path(__file__).parent,'-B',build,'-G','Ninja',*flags)
        run('cmake','--build',build,'--parallel','2')
        run('cmake','--install',build,'--prefix',work/'deployment')
        results.append({'name':'acquired-sdk-consumer-build','status':'passed'})
        binaries=[build/name for name in ['first-call','api-validation']]
        if row['family']=='ios':binaries=[build/(name+'.app')/name for name in ['first-call','api-validation']]
        if row['executor']=='ios-simulator':simulator(binaries,work/'deployment')
        elif row['family']=='android':
            android(sdk,work,binaries,row['arch'])
            if a.extended:android(sdk,work,binaries,row['arch'],True)
        if row['executor']!='ios-device':
            results.extend({'name':name,'status':'passed'} for name in ['acquired-sdk-runtime','acquired-sdk-deployment','acquired-sdk-components','acquired-sdk-missing-and-identity','acquired-sdk-lifecycle'])
        if a.extended and row['family']=='android':
            results.append({'name':'acquired-sdk-16k-pages','status':'passed'})
    except Exception as error:
        results.append({'name':'acquired-sdk-acceptance','status':'failed','error':str(error)})
    report={'schema':1,'kind':'acquired','candidate':release.digest(a.record),'tests':results,'eligible':all(t['status']=='passed' for t in results),'host':release.qualification_host(),'physical_device_execution':'untested'}
    release.write(a.report,report)
    if not report['eligible']:raise SystemExit(1)

if __name__=='__main__':main()
