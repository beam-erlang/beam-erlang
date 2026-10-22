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


def run(*args,**kwargs):
    return subprocess.run(list(map(str,args)),check=True,**kwargs)


def android(sdk,work,binaries,arch,page16=False):
    tools=Path(os.environ.get('ANDROID_HOME') or os.environ['ANDROID_SDK_ROOT'])
    adb=tools/'platform-tools/adb';manager=Path(os.environ['BEAM_ANDROID_TOOLS_BIN'])/'avdmanager'
    image='system-images;android-35;'+('google_apis_ps16k' if page16 else 'google_apis')+';'+('arm64-v8a' if arch=='arm64' else 'x86_64')
    name='beam-acceptance-'+arch+('-16k' if page16 else '-4k')
    run(manager,'create','avd','--force','--name',name,'--package',image,input='no\n',text=True)
    process=subprocess.Popen([str(tools/'emulator/emulator'),'@'+name,'-no-window','-no-audio','-no-boot-anim','-no-snapshot','-gpu','swiftshader'])
    try:
        run(adb,'wait-for-device',timeout=240)
        for _ in range(120):
            if subprocess.check_output([str(adb),'shell','getprop','sys.boot_completed'],text=True).strip()=='1':break
            time.sleep(2)
        else:raise TimeoutError('Android boot did not complete')
        size=subprocess.check_output([str(adb),'shell','getconf','PAGESIZE'],text=True).strip()
        if size!=('16384' if page16 else '4096'):raise ValueError('unexpected Android page size: '+size)
        remote='/data/local/tmp/beam-acceptance'
        run(adb,'shell','rm','-rf',remote);run(adb,'shell','mkdir','-p',remote+'/lib')
        run(adb,'push',sdk/'lib',remote)
        for binary in binaries:
            run(adb,'push',binary,remote+'/'+binary.name)
            run(adb,'shell','chmod','755',remote+'/'+binary.name)
            run(adb,'shell',remote+'/'+binary.name,timeout=300)
        bundle=work/'deployment'
        run(adb,'push',bundle,remote)
        deployed=remote+'/deployment'
        command=[str(adb),'shell',deployed+'/bin/deployment_host']
        for mode in ['boot','file','network','zlib']:
            run(*command,mode,deployed,timeout=300)
        # Exercise missing assets and bytecode identity after successful entry.
        asset=deployed+'/lib/beam/boot/ebin/beam_boot.beam'
        run(adb,'shell','mv',asset,asset+'.saved')
        if subprocess.run(command+['boot',deployed],timeout=300).returncode==0:
            raise ValueError('missing mobile boot asset was accepted')
        run(adb,'shell','cp',deployed+'/lib/beam/library/ebin/beam_time.beam',asset)
        if subprocess.run(command+['boot',deployed],timeout=300).returncode==0:
            raise ValueError('mismatched mobile boot asset was accepted')
        run(adb,'shell','mv',asset+'.saved',asset)
        run(*command,'boot',deployed,timeout=300)
    finally:
        subprocess.run([str(adb),'emu','kill']);process.terminate()
        try:process.wait(timeout=20)
        except subprocess.TimeoutExpired:process.kill();process.wait()
        subprocess.run([str(manager),'delete','avd','--name',name])


def simulator(binaries,bundle):
    devices=json.loads(subprocess.check_output(['xcrun','simctl','list','devices','available','--json'],text=True))['devices']
    selected=next(device for key,values in devices.items() if 'iOS' in key for device in values)
    udid=selected['udid'];run('xcrun','simctl','boot',udid) if selected['state']!='Booted' else None
    try:
        run('xcrun','simctl','bootstatus',udid,'-b')
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
    finally: run('xcrun','simctl','shutdown',udid)


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
