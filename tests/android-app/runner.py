#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Build and execute an installed-only, signed Android product fixture offline."""
import argparse
import contextlib
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import xml.etree.ElementTree as ET
import zipfile

APP='org.erlangsters.acceptance.android'
ACTIVITY=APP+'/.AcceptanceActivity'


def sha(path):
    with path.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('inputs','gradle','gradle-home','android-sdk','jdk','bundletool','keystore','work','result'):
        p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--emulator',action='append',default=[])
    p.add_argument('--device',action='append',default=[])
    p.add_argument('--build-only',action='store_true')
    p.add_argument('--keep-work',action='store_true',help='Retain a failed payload for explicit diagnosis')
    p.add_argument('--forbid',type=Path,action='append',default=[])
    a=p.parse_args();a.forbid=[x.resolve(strict=True) for x in a.forbid]
    if not a.emulator and not a.device and not a.build_only:p.error('Select explicit ADB serials or --build-only')
    if a.build_only and (a.emulator or a.device):p.error('Build-only cannot claim device execution')
    if not os.environ.get('BEAM_KEYSTORE_PASSWORD'):p.error('Set BEAM_KEYSTORE_PASSWORD for the explicitly selected local signing key')
    source=Path(__file__).resolve().parent;original=a.inputs.resolve(strict=True)
    inputs_record=json.loads((original/'inputs.json').read_text());targets=inputs_record['targets']
    flavor=targets['arm64-v8a']['flavor']
    a.work.mkdir(parents=True,exist_ok=True);a.result.parent.mkdir(parents=True,exist_ok=True)
    env=os.environ.copy();env.update(JAVA_HOME=str(a.jdk.resolve()),ANDROID_HOME=str(a.android_sdk.resolve()),ANDROID_SDK_ROOT=str(a.android_sdk.resolve()),
        ANDROID_NDK_ROOT=str(a.android_sdk.resolve()/'ndk/28.2.13676358'),GRADLE_USER_HOME=str(a.gradle_home.resolve()))
    adb=a.android_sdk.resolve()/'platform-tools/adb';java=a.jdk.resolve()/'bin/java'
    tools=a.android_sdk.resolve()/'build-tools/36.0.0'
    evidence={'format':1,'status':'running','flavor':flavor,'targets':targets,'commands':[],
              'variants':{},'executions':[],'tests':{},'deployment_minimum':26,'compile_target_api':36,
              'source_sha256':{str(x.relative_to(source)):sha(x) for x in source.rglob('*') if x.is_file() and
                  not any(t in x.relative_to(source).parts for t in ('build','.gradle','.cxx','__pycache__'))}}
    installed=set();log=a.result.with_suffix('.log').open('w')
    def run(*command,check=True,timeout=600,cwd=None,isolated=False):
        command=list(map(str,command))
        if isolated:
            command=[sys.executable,str(project/'isolate.py'),*sum((['--deny',str(x)] for x in [source,original,*a.forbid]),[]),'--',*command]
        evidence['commands'].append(command)
        proc=subprocess.Popen(command,cwd=cwd,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,start_new_session=True)
        try:output,_=proc.communicate(timeout=timeout)
        except BaseException:
            os.killpg(proc.pid,signal.SIGKILL);output,_=proc.communicate();log.write(output[-16000:]);log.flush();raise
        log.write('$ '+repr(command)+'\n'+output+'\n');log.flush()
        if check and proc.returncode:raise RuntimeError(f'Command failed ({proc.returncode}): {command}\n{output[-6000:]}')
        return output
    def device(serial,*command,**kw):return run(adb,'-s',serial,*command,**kw)
    def launch(serial):device(serial,'shell','am','start','-W','-n',ACTIVITY)
    def reports(serial):
        text=device(serial,'logcat','-d','-v','raw','-s','BEAMAcceptance:I','*:S',timeout=20)
        out=[]
        for line in text.splitlines():
            try:out.append(json.loads(line))
            except json.JSONDecodeError:pass
        return out
    def poll(serial,predicate,*,failure=False,after=0):
        deadline=time.monotonic()+90;last=None
        while time.monotonic()<deadline:
            records=reports(serial)[after:]
            for last in reversed(records):
                if predicate(last):return last
                if last.get('state')=='failed' and not failure:raise RuntimeError('Application failed: '+repr(last))
            time.sleep(.5)
        raise RuntimeError('Android application result timeout: '+repr(last))
    def tap(serial,text):
        # Exercise real Activity controls through their displayed accessibility text.
        device(serial,'shell','uiautomator','dump','/data/local/tmp/beam-acceptance-ui.xml',timeout=25)
        xml=device(serial,'shell','cat','/data/local/tmp/beam-acceptance-ui.xml',timeout=10)
        nodes=[n for n in ET.fromstring(xml).iter('node') if n.get('text')==text]
        if len(nodes)!=1:raise RuntimeError('UI control missing or ambiguous: '+text)
        coords=list(map(int,re.findall(r'\d+',nodes[0].get('bounds',''))))
        if len(coords)!=4:raise RuntimeError('Invalid UI bounds')
        device(serial,'shell','input','tap',str((coords[0]+coords[2])//2),str((coords[1]+coords[3])//2))
    def certificate(apk):
        output=run(tools/'apksigner','verify','--verbose','--print-certs',apk)
        values=re.findall(r'Signer #1 certificate SHA-256 digest: ([a-fA-F0-9]{64})',output)
        if len(values)!=1:raise RuntimeError('APK signing certificate not unambiguous')
        return values[0].lower()
    try:
        gradle_version=run(a.gradle.resolve(),'--version',timeout=60)
        java_version=run(java,'-version',timeout=30)
        bundle_version=run(java,'-jar',a.bundletool.resolve(),'version',timeout=30).strip()
        if not re.search(r'^Gradle 8\.13$',gradle_version,re.M):raise RuntimeError('Select pinned Gradle 8.13')
        if not re.search(r'version "17[.\"]',java_version):raise RuntimeError('Select JDK 17')
        if bundle_version!='1.18.2':raise RuntimeError('Select pinned bundletool 1.18.2')
        evidence['tool_versions']={'gradle':gradle_version.strip(),'java':java_version.strip(),'bundletool':bundle_version}
        temporary_context=contextlib.nullcontext(tempfile.mkdtemp(prefix='android-app-',dir=a.work)) if a.keep_work else tempfile.TemporaryDirectory(prefix='android-app-',dir=a.work)
        with temporary_context as temporary:
            work=Path(temporary);project=work/'copied fixture';inputs=work/'relocated inputs ü'
            if a.keep_work:evidence['retained_work']=str(work)
            shutil.copytree(source,project,ignore=shutil.ignore_patterns('build','.gradle','.cxx','__pycache__'))
            shutil.copy2(source.parent/'support/native_counts.h',project/'app/src/main/cpp/native_counts.h')
            shutil.copytree(original,inputs,symlinks=True)
            spec=importlib.util.spec_from_file_location('android_delivery',inputs/'sdk/arm64-v8a/share/beam/tools/beam-android.py')
            delivery=importlib.util.module_from_spec(spec);spec.loader.exec_module(delivery)
            destinations=[]
            for kind,serials in [('emulator',a.emulator),('physical',a.device)]:
                for serial in serials:
                    if device(serial,'get-state').strip()!='device':raise RuntimeError('Selected ADB device is not ready: '+serial)
                    facts={'serial':serial,'device_kind':kind,
                        'api':int(device(serial,'shell','getprop','ro.build.version.sdk').strip()),
                        'os_version':device(serial,'shell','getprop','ro.build.version.release').strip(),
                        'abi':device(serial,'shell','getprop','ro.product.cpu.abi').strip(),
                        'page_size':int(device(serial,'shell','getconf','PAGE_SIZE').strip()),
                        'build_fingerprint':device(serial,'shell','getprop','ro.build.fingerprint').strip()}
                    emulator=device(serial,'shell','getprop','ro.kernel.qemu').strip()=='1'
                    if emulator != (kind=='emulator') or facts['abi'] not in targets:raise RuntimeError('Device selection identity mismatch')
                    if facts['api']<26 or facts['page_size'] not in (4096,16384):raise RuntimeError('Unsupported observed Android environment')
                    # API 35's experimental image supports global linker policy.
                    # Only this runner's emulator is changed; physical devices use
                    # manifest opt-out plus verified ELF/ZIP alignment.
                    if kind=='emulator' and facts['page_size']==16384:
                        device(serial,'root',timeout=30)
                        device(serial,'wait-for-device',timeout=60)
                        if device(serial,'shell','id','-u').strip()!='0':
                            raise RuntimeError('Selected 16 KB emulator must permit explicit linker policy setup')
                        device(serial,'shell','setprop','bionic.linker.16kb.app_compat.enabled','false')
                        device(serial,'shell','setprop','pm.16kb.app_compat.disabled','true')
                        if device(serial,'shell','getprop','bionic.linker.16kb.app_compat.enabled').strip()!='false' or device(serial,'shell','getprop','pm.16kb.app_compat.disabled').strip()!='true':
                            raise RuntimeError('Cannot prove page-size compatibility fallback is disabled')
                    facts['page_size_compatibility']=False;destinations.append(facts)
            password_file=work/'signing-password';password_file.write_text(env['BEAM_KEYSTORE_PASSWORD']);password_file.chmod(0o600)
            base_command=[a.gradle.resolve(),'--offline','--no-daemon','--console=plain',
                          '-PbeamInputs='+str(inputs),'-PbeamFlavor='+flavor,'-PbeamKeystore='+str(a.keystore.resolve())]
            normal_assets=work/'verified assets';shutil.copytree(inputs/'assets',normal_assets)
            variants=[('all','file,network,zlib'),('runtime-only',''),('file','file'),('network','network'),('zlib','zlib'),
                      ('missing','file,network,zlib'),('corrupt','file,network,zlib'),('invalid-native','file,network,zlib'),('recovery','file,network,zlib')]
            for name,components in variants:
                shutil.rmtree(inputs/'assets');shutil.copytree(normal_assets,inputs/'assets')
                if name in ('missing','corrupt'):
                    for abi in targets:
                        candidates=list((inputs/'assets/beam'/abi/'packages').glob('*/ebin/android_acceptance.beam'))
                        if len(candidates)!=1:raise RuntimeError('Selected immutable module is not unique')
                        if name=='missing':candidates[0].unlink()
                        else:
                            data=bytearray(candidates[0].read_bytes());data[-1]^=1;candidates[0].write_bytes(data)
                native_variant='invalid' if name=='invalid-native' else 'normal'
                run(*base_command,'-PbeamComponents='+components,'-PbeamNativeVariant='+native_variant,'assembleRelease',
                    *(['bundleRelease'] if name=='all' else []),isolated=True,cwd=project,timeout=1800)
                apk=project/'app/build/outputs/apk/release/app-release.apk'
                native=delivery.apk_set([apk]);cert=certificate(apk)
                expected={'libbeam.so','libc++_shared.so','libbeam_acceptance.so','libshared_probe.so'} | {'libbeam-'+c+'.so' for c in components.split(',') if c}
                if any(set(records)!=expected for records in native['native'].values()):raise RuntimeError('Unexpected runtime/component/C++ closure')
                item={'build':'passed','signing_certificate_sha256':cert,'native':native,'execution':'untested','devices':{}}
                evidence['variants'][name]=item
                for facts in destinations:
                    serial=facts['serial'];device(serial,'uninstall',APP,check=False)
                    device(serial,'install','-r',apk);installed.add(serial)
                    start=len(reports(serial));launch(serial)
                    if name in ('missing','corrupt','invalid-native'):
                        rejected=poll(serial,lambda r:r.get('state')=='failed',failure=True,after=start)
                        if rejected.get('generation',0)!=0:raise RuntimeError('Invalid selection reached a live VM')
                        if name=='missing' and 'missing asset' not in rejected.get('error',''):raise RuntimeError('Missing resource failed for an unrelated reason')
                        if name=='corrupt' and 'verify bytecode' not in rejected.get('error',''):raise RuntimeError('Corrupt resource failed for an unrelated reason')
                        if name=='invalid-native' and 'Missing native image' not in rejected.get('error',''):raise RuntimeError('Invalid native selection failed for an unrelated reason')
                        item['devices'][serial]={'expected_rejection':rejected};continue
                    first=poll(serial,lambda r:r.get('state')=='passed' and r.get('cycles',0)>=3 and r.get('events',0)>=3,after=start)
                    if first['sdk_build_id']!=targets[facts['abi']]['sdk-build-id'] or first['flavor']!=flavor or first['page_size']!=facts['page_size'] or first['debuggable'] or first['debugger_attached'] or first['ui_heartbeats']<3 or first.get('cleanups',0)<2 or '!/lib/' not in first['runtime_image']:
                        raise RuntimeError('Android runtime, signing or responsive UI facts disagree with selection')
                    item['devices'][serial]={'initial':first}
                    if name=='all':
                        start=len(reports(serial));tap(serial,'Java GC and exception')
                        stress=poll(serial,lambda r:r.get('state')=='passed' and r.get('generation',0)>first['generation'] and r.get('java_exceptions',0)>0,after=start)
                        start=len(reports(serial));tap(serial,'Recreate Activity');tap(serial,'Snapshot')
                        recreated=poll(serial,lambda r:r.get('activity_creations',0)>stress['activity_creations'],after=start)
                        if recreated['generation']!=stress['generation'] or recreated['process_id']!=stress['process_id']:raise RuntimeError('Activity recreation replaced the system/VM')
                        start=len(reports(serial));device(serial,'shell','input','keyevent','KEYCODE_HOME')
                        inactive=poll(serial,lambda r:r.get('state')=='inactive',after=start);launch(serial)
                        recovered=poll(serial,lambda r:r.get('state')=='passed' and r.get('generation',0)>recreated['generation'],after=start)
                        start=len(reports(serial));device(serial,'shell','am','force-stop',APP);launch(serial)
                        reborn=poll(serial,lambda r:r.get('state')=='passed' and r.get('cycles',0)>=3,after=start)
                        if reborn['process_id']==recovered['process_id'] or reborn['acknowledged']<=recovered['acknowledged']:raise RuntimeError('Acknowledged state did not survive process death')
                        item['devices'][serial].update(stress=stress,recreated=recreated,inactive=inactive,recovered=recovered,reborn=reborn)
                        bundle=project/'app/build/outputs/bundle/release/app-release.aab';splitset=work/(serial.replace(':','_')+'.apks')
                        run(java,'-jar',a.bundletool.resolve(),'build-apks','--bundle='+str(bundle),'--output='+str(splitset),
                            '--connected-device','--device-id='+serial,'--adb='+str(adb),'--ks='+str(a.keystore.resolve()),
                            '--ks-key-alias=qualification','--ks-pass=file:'+str(password_file),'--key-pass=file:'+str(password_file))
                        splitdir=work/('splits-'+serial.replace(':','_'));splitdir.mkdir()
                        with zipfile.ZipFile(splitset) as z:
                            for entry in z.infolist():
                                if entry.filename.endswith('.apk'):
                                    target=splitdir/Path(entry.filename).name
                                    if target.exists():raise RuntimeError('Ambiguous split archive basename')
                                    target.write_bytes(z.read(entry))
                        splits=sorted(splitdir.glob('*.apk'));delivery.apk_set(splits)
                        for split in splits:
                            if certificate(split)!=cert:raise RuntimeError('Split signing identity differs')
                        device(serial,'uninstall',APP);device(serial,'install-multiple','-r',*splits)
                        start=len(reports(serial));launch(serial)
                        split_result=poll(serial,lambda r:r.get('state')=='passed' and r.get('cycles',0)>=3,after=start)
                        if 'split' not in split_result['runtime_image']:raise RuntimeError('AAB execution did not load from a split APK')
                        item['devices'][serial]['split']=split_result
                        evidence['executions'].append({**facts,'sdk_build_id':first['sdk_build_id'],'flavor':flavor,'status':'passed',
                            'configuration':'Release','debuggable':False,'debugger_attached':False,'signing_certificate_sha256':cert})
                if destinations:item['execution']='passed'
            for name in ('signed-release','responsiveness','native-librarys','static-shared-nifs','lifetimes','background-recovery','activity-recreation','process-death','java-gc-exceptions','resource-rejection','relocated-isolated-inputs','native-closure','split-install'):
                evidence['tests']['android-app-'+name]='passed' if destinations else 'untested'
            evidence['status']='passed' if destinations else 'build-only'
    except BaseException as failure:
        evidence['status']='failed';evidence['error']=str(failure)
        raise
    finally:
        cleanup_errors=[]
        for serial in installed:
            for command in [('shell','am','force-stop',APP),('uninstall',APP),
                            ('shell','rm','-f','/data/local/tmp/beam-acceptance-ui.xml')]:
                try:device(serial,*command,timeout=30)
                except Exception as error:cleanup_errors.append({'serial':serial,'command':command,'error':str(error)})
        evidence['cleanup']='retained-for-diagnosis' if a.keep_work else 'completed'
        if cleanup_errors:
            evidence.update(cleanup='incomplete',cleanup_errors=cleanup_errors,status='failed')
        a.result.write_text(json.dumps(evidence,indent=2)+'\n');log.close()
        if cleanup_errors:raise RuntimeError('Android cleanup failed; see the result report')


if __name__=='__main__':main()
