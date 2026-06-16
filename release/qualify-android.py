#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Bind independent Android application qualification to transported SDK candidates."""
import argparse
import contextlib
import os
from pathlib import Path
import secrets
import subprocess
import sys
import tempfile
import release
sys.path.insert(0,str(release.ROOT/'ci'))
import executor_v1


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('record','peer-record','package','work','report'):
        p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--host-sdk',type=Path)
    a=p.parse_args();record=release.read(a.record);peer=release.read(a.peer_record)
    result={'schema':1,'kind':'android-app','candidate':release.digest(a.record),'eligible':False,
            'host':release.qualification_host(),'executions':[],'physical_device_execution':'untested',
            'tests':[{'name':n,'status':'untested'} for n in sorted(release.ANDROID_APP_TESTS)]}
    a.work.mkdir(parents=True,exist_ok=True)
    try:
        for selected in (record,peer):release.validate_candidate_version(selected)
        row=record['platform']
        if row['family']!='android' or peer['platform']['family']!='android':raise ValueError('Android candidate pair required')
        if {row['arch'],peer['platform']['arch']}!={'arm64','amd64'}:raise ValueError('AAR qualification needs both Android ABIs')
        for key in ('source','version','flavor'):
            if record[key]!=peer[key]:raise ValueError('Peer SDK candidate disagrees on '+key)
        if release.clean_revision(release.ROOT)!=record['source']:raise ValueError('Fixture source differs from candidate')
        if release.clean_revision(a.package)!=record['inputs']['package']['commit']:raise ValueError('Package source differs from candidate input pin')
        selected={name:Path(os.environ[name]).resolve(strict=True) for name in
            ('BEAM_ANDROID_GRADLE','BEAM_ANDROID_GRADLE_HOME','BEAM_ANDROID_BUNDLETOOL','JAVA_HOME','ANDROID_HOME')}
        # Provisioning owns network acquisition. Every Gradle invocation below is offline.
        with tempfile.TemporaryDirectory(prefix='android-qualification-',dir=a.work) as temporary:
            root=Path(temporary);sdks=[]
            for path,candidate in ((a.record,record),(a.peer_record,peer)):
                archive=path.parent/candidate['archive']['name'];release.verify_asset(archive,candidate['archive'])
                sdks.append(release.extract(archive,root/('sdk-'+candidate['platform']['arch'])))
            os.environ['ANDROID_NDK_ROOT']=str(selected['ANDROID_HOME']/'ndk/28.2.13676358')
            input_report=a.report.with_name(a.report.stem+'-inputs.json')
            subprocess.run([sys.executable,str(release.ROOT/'tests/target/android_delivery.py'),
                '--sdk',str(sdks[0]),'--sdk',str(sdks[1]),'--work',str(root/'input-checks'),
                '--report',str(input_report)],check=True)
            result['input_validation']=release.read(input_report)
            host=a.host_sdk
            if host is None:
                host=root/'host-sdk';build=root/'host-build'
                for command in (
                    ['cmake','-S',release.ROOT,'-B',build,'-G','Ninja','-DCMAKE_BUILD_TYPE=Release',
                     '-DERTS_BUILD_FLAVOR=emu','-DCMAKE_INSTALL_PREFIX='+str(host),
                     '-DBEAM_PACKAGE_SOURCE_DIR='+str(a.package.resolve()),'-DPython3_EXECUTABLE='+sys.executable],
                    ['cmake','--build',build,'--parallel','2'],['cmake','--install',build]):
                    subprocess.run(list(map(str,command)),check=True)
            inputs=root/'inputs'
            subprocess.run([sys.executable,str(release.ROOT/'tests/android-app/prepare.py'),
                '--host-sdk',str(host),'--sdk',str(sdks[0]),'--sdk',str(sdks[1]),'--output',str(inputs)],check=True)
            key=root/'qualification.p12';password_file=root/'password';password=secrets.token_urlsafe(32)
            password_file.write_text(password);password_file.chmod(0o600)
            subprocess.run([str(selected['JAVA_HOME']/'bin/keytool'),'-genkeypair','-alias','qualification',
                '-keyalg','RSA','-keysize','3072','-validity','30','-dname','CN=BEAM Android local qualification',
                '-keystore',str(key),'-storepass:file',str(password_file),'-keypass:file',str(password_file)],check=True)
            env=dict(os.environ,BEAM_KEYSTORE_PASSWORD=password)
            with contextlib.ExitStack() as sessions:
                destinations=[]
                matrix=[(35,4096),(35,16384),(36,4096)]
                if row['arch']=='amd64':matrix.append((26,4096))
                for api,pages in matrix:
                    command=sessions.enter_context(executor_v1.android_session(root,row['arch'],pages,api=api))
                    destinations+=['--emulator',command[2]]
                physical=os.environ.get('BEAM_ANDROID_DEVICE') if row['arch']=='arm64' else None
                if physical:destinations+=['--device',physical]
                app_report=a.report.with_name(a.report.stem+'-application.json')
                command=[sys.executable,release.ROOT/'tests/android-app/runner.py','--inputs',inputs,
                    '--gradle',selected['BEAM_ANDROID_GRADLE'],'--gradle-home',selected['BEAM_ANDROID_GRADLE_HOME'],
                    '--android-sdk',selected['ANDROID_HOME'],'--jdk',selected['JAVA_HOME'],
                    '--bundletool',selected['BEAM_ANDROID_BUNDLETOOL'],'--keystore',key,
                    '--work',root/'application','--result',app_report,'--forbid',release.ROOT,
                    '--forbid',a.package.resolve(),*destinations]
                execution=subprocess.run(list(map(str,command)),env=env)
                evidence=release.read(app_report);result['application']=evidence
                result['executions']=evidence.get('executions',[])
                result['tests']=[{'name':name,'status':evidence.get('tests',{}).get(name,'failed')}
                                 for name in sorted(release.ANDROID_APP_TESTS)]
                if execution.returncode or evidence.get('status')!='passed':raise ValueError('Android application acceptance failed')
                if physical:result['physical_device_execution']='passed'
                release.validate_android_app_report(result,record,row)
                result['eligible']=all(t['status']=='passed' for t in result['tests'])
    except Exception as error:
        result['error']=str(error)
    finally:release.write(a.report,result)
    if not result['eligible']:raise SystemExit(1)


if __name__=='__main__':main()
