"""Version 1 transport for prepared mobile consumers; assertions are bundle-owned."""
import argparse
import contextlib
import hashlib
import json
import os
from pathlib import Path
import shlex
import socket
import subprocess
import time
import uuid


def run(*argv, **kwargs):
    return subprocess.run([str(a) for a in argv], check=True, **kwargs)


def confined(root, relative):
    if not isinstance(relative,str) or not relative or Path(relative).is_absolute() or '..' in Path(relative).parts:
        raise ValueError('invalid bundle path')
    result=(root/relative).resolve()
    if not result.is_relative_to(root.resolve()):raise ValueError('bundle path escapes root')
    return result


def load(bundle):
    spec=json.loads((bundle/'consumer.json').read_text())
    if spec.get('interface')!='beam-consumer-v1' or spec.get('system') not in ('Android','iOS'):
        raise ValueError('unsupported prepared consumer interface')
    if spec.get('architecture') not in ('amd64','arm64') or not spec.get('cases'):
        raise ValueError('invalid prepared consumer target/cases')
    files=spec['files']
    actual={p.relative_to(bundle).as_posix() for p in bundle.rglob('*') if p.is_file() and p!=bundle/'consumer.json'}
    if set(files)!=actual:raise ValueError('bundle file inventory mismatch')
    for name,record in files.items():
        path=confined(bundle,name)
        if path.stat().st_size!=record['size'] or hashlib.sha256(path.read_bytes()).hexdigest()!=record['sha256']:
            raise ValueError('bundle input integrity failure: '+name)
    return spec


@contextlib.contextmanager
def android_session(work,architecture,page_size,api=35):
    tools=Path(os.environ.get('ANDROID_HOME') or os.environ['ANDROID_SDK_ROOT'])
    adb=tools/'platform-tools/adb'
    manager=Path(os.environ['BEAM_ANDROID_TOOLS_BIN'])/'avdmanager'
    name='beam-consumer-'+uuid.uuid4().hex
    image='system-images;android-'+str(api)+';'+('google_apis_ps16k' if page_size==16384 else 'google_apis')+';'+('arm64-v8a' if architecture=='arm64' else 'x86_64')
    if page_size==16384 and api!=35:raise ValueError('Select the pinned API 35 16 KB image')
    run(manager,'create','avd','--force','--name',name,'--package',image,input='no\n',text=True)
    # Choose an unused console/adb pair. Verify the AVD identity after startup
    # as another process could acquire the ports between probing and launch.
    port=None
    for candidate in range(5554,5682,2):
        with contextlib.ExitStack() as stack:
            try:
                for number in (candidate,candidate+1):
                    listener=stack.enter_context(socket.socket())
                    listener.bind(('127.0.0.1',number))
            except OSError:continue
            port=candidate
            break
    if port is None:raise RuntimeError('no unused Android emulator port pair')
    process=subprocess.Popen([str(tools/'emulator/emulator'),'@'+name,'-port',str(port),'-no-window','-no-audio','-no-boot-anim','-no-snapshot','-gpu','swiftshader'])
    command=[str(adb),'-s','emulator-'+str(port)]
    owns_device=False
    try:
        run(*command,'wait-for-device',timeout=240)
        identity=subprocess.check_output(command+['emu','avd','name'],text=True,timeout=30).splitlines()
        if not identity or identity[0]!=name:raise RuntimeError('Android serial belongs to another AVD')
        owns_device=True
        deadline=time.monotonic()+240
        while time.monotonic()<deadline:
            if subprocess.check_output(command+['shell','getprop','sys.boot_completed'],text=True).strip()=='1':break
            if process.poll() is not None:raise RuntimeError('Android emulator exited during startup')
            time.sleep(2)
        else:raise TimeoutError('Android emulator boot timeout')
        observed=subprocess.check_output(command+['shell','getconf','PAGESIZE'],text=True).strip()
        if observed!=str(page_size):raise ValueError('unexpected Android page size: '+observed)
        yield command
    finally:
        if owns_device:subprocess.run(command+['emu','kill'],timeout=30,check=False)
        process.terminate()
        try:process.wait(timeout=20)
        except subprocess.TimeoutExpired:process.kill();process.wait()
        run(manager,'delete','avd','--name',name)


@contextlib.contextmanager
def simulator_session():
    types=json.loads(subprocess.check_output(['xcrun','simctl','list','devicetypes','--json'],text=True))['devicetypes']
    runtimes=json.loads(subprocess.check_output(['xcrun','simctl','list','runtimes','--json'],text=True))['runtimes']
    runtime=os.environ.get('BEAM_IOS_SIMULATOR_RUNTIME')
    device=os.environ.get('BEAM_IOS_SIMULATOR_DEVICE_TYPE')
    if not any(r['identifier']==runtime and r.get('isAvailable') for r in runtimes):
        raise ValueError('Set BEAM_IOS_SIMULATOR_RUNTIME to an installed, available iOS runtime identifier')
    if not any(t['identifier']==device and t.get('productFamily') in ('iPhone','iPad') for t in types):
        raise ValueError('Set BEAM_IOS_SIMULATOR_DEVICE_TYPE to an installed iPhone/iPad device type identifier')
    udid=subprocess.check_output(['xcrun','simctl','create','beam-consumer-'+uuid.uuid4().hex,device,runtime],text=True).strip()
    try:
        run('xcrun','simctl','boot',udid)
        run('xcrun','simctl','bootstatus',udid,'-b',timeout=300)
        yield udid
    finally:
        subprocess.run(['xcrun','simctl','shutdown',udid],check=False,timeout=60)
        run('xcrun','simctl','delete',udid)


def cases(spec,bundle,root,execute,move,copy):
    results=[]
    def argument(value):
        if not isinstance(value,str) or '\0' in value:raise ValueError('invalid consumer argument')
        return root+value[len('$BUNDLE'):] if value=='$BUNDLE' or value.startswith('$BUNDLE/') else value
    for case in spec['cases']:
        name=case['name']
        operation=case.get('operation','run')
        if operation in ('move','copy'):
            for path in (case['source'],case['destination']):confined(bundle,path)
            (move if operation=='move' else copy)(root+'/'+case['source'],root+'/'+case['destination'])
        elif operation=='run':
            argv=[argument(a) for a in case['argv']]
            if not argv or not argv[0].startswith(root+'/'):raise ValueError('consumer command must be inside bundle')
            result=execute(argv,case.get('timeout',300))
            if result.returncode!=case.get('status',0):
                raise RuntimeError(f'{name}: exit {result.returncode}; stdout={result.stdout!r}; stderr={result.stderr!r}')
            for stream in ('stdout','stderr'):
                if stream in case and getattr(result,stream)!=case[stream]:raise ValueError(f'{name}: unexpected {stream}')
                key=stream+'_contains'
                if key in case and case[key] not in getattr(result,stream):raise ValueError(f'{name}: missing {stream} assertion')
        else:raise ValueError('unknown consumer operation')
        results.append({'name':name,'status':'passed'})
    return results


def execute_bundle(bundle,work,page_size=4096):
    spec=load(bundle)
    for name,record in spec['files'].items():
        if record.get('executable'):confined(bundle,name).chmod(0o755)
    if spec['system']=='Android':
        with android_session(work,spec['architecture'],page_size) as adb:
            remote='/data/local/tmp/beam-consumer-'+uuid.uuid4().hex
            run(*adb,'push',bundle,remote)
            try:
                for name,record in spec['files'].items():
                    if record.get('executable'):run(*adb,'shell',shlex.join(['chmod','755',remote+'/'+name]))
                def execute(argv,timeout):
                    return subprocess.run([*adb,'shell',shlex.join(argv)],capture_output=True,text=True,timeout=timeout)
                return cases(spec,bundle,remote,execute,
                    lambda a,b:run(*adb,'shell',shlex.join(['mv',a,b])),
                    lambda a,b:run(*adb,'shell',shlex.join(['cp',a,b])))
            finally:run(*adb,'shell',shlex.join(['rm','-rf',remote]))
    if spec.get('variant')!='simulator':raise ValueError('physical iOS device execution is outside this interface')
    with simulator_session() as udid:
        def execute(argv,timeout):
            return subprocess.run(['xcrun','simctl','spawn',udid,*argv],capture_output=True,text=True,timeout=timeout)
        import shutil
        return cases(spec,bundle,str(bundle.resolve()),execute,lambda a,b:Path(a).rename(b),shutil.copy2)


def main():
    p=argparse.ArgumentParser();p.add_argument('--interface-version',action='store_true')
    p.add_argument('--bundle',type=Path);p.add_argument('--work',type=Path);p.add_argument('--report',type=Path)
    p.add_argument('--page-size',type=int,choices=[4096,16384],default=4096);a=p.parse_args()
    if a.interface_version:print('beam-consumer-v1');return
    if not all((a.bundle,a.work,a.report)):p.error('bundle, work and report are required')
    a.work.mkdir(parents=True,exist_ok=True)
    report={'interface':'beam-consumer-v1','page_size':a.page_size,'physical_device_execution':'untested','tests':[],'eligible':False}
    try:
        report['bundle_sha256']=hashlib.sha256((a.bundle/'consumer.json').read_bytes()).hexdigest()
        report['tests']=execute_bundle(a.bundle.resolve(),a.work,a.page_size)
        report['eligible']=True
    except Exception as error:report['tests'].append({'name':'prepared-consumer','status':'failed','error':str(error)})
    a.report.parent.mkdir(parents=True,exist_ok=True)
    a.report.write_text(json.dumps(report,indent=2)+'\n')
    if not report['eligible']:raise SystemExit(1)


if __name__=='__main__':main()
