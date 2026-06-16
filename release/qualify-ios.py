#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Qualify a transported iOS SDK using the product-owned signed application."""
import argparse
import contextlib
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import release
sys.path.insert(0,str(release.ROOT/'ci'))
import executor_v1


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('record','package','work','report'):p.add_argument('--'+key,type=Path,required=True)
    p.add_argument('--host-sdk',type=Path)
    a=p.parse_args();record=release.read(a.record);release.validate_candidate_version(record)
    if record['platform']['family']!='ios':p.error('iOS candidate required')
    device=record['platform']['executor']=='ios-device'
    results=[];evidence={};host=release.qualification_host()
    a.work.mkdir(parents=True,exist_ok=True)
    try:
        if release.clean_revision(release.ROOT)!=record['source']:
            raise ValueError('Product application fixture source differs from candidate source')
        if device and not os.environ.get('BEAM_IOS_DEVICE'):
            raise RuntimeError('Physical device execution requires BEAM_IOS_DEVICE and configured signing access')
        if release.clean_revision(a.package)!=record['inputs']['package']['commit']:
            raise ValueError('Package source does not match the candidate input pin')
        archive=a.record.parent/record['archive']['name'];release.verify_asset(archive,record['archive'])
        with tempfile.TemporaryDirectory(prefix='ios-qualification-',dir=a.work) as temporary:
            root=Path(temporary);sdk=release.extract(archive,root/'target-sdk')
            host_sdk=a.host_sdk
            if host_sdk is None:
                host_sdk=root/'host-sdk';build=root/'host-build'
                for command in (
                    ['cmake','--preset','macos-arm64-appleclang-emu','-B',build,
                     '-DCMAKE_INSTALL_PREFIX='+str(host_sdk),'-DBEAM_PACKAGE_SOURCE_DIR='+str(a.package.resolve()),
                     '-DPython3_EXECUTABLE='+sys.executable],
                    ['cmake','--build',build,'--parallel','2'],['cmake','--install',build]):
                    subprocess.run(list(map(str,command)),cwd=release.ROOT,check=True)
            destination=contextlib.nullcontext(os.environ['BEAM_IOS_DEVICE']) if device else executor_v1.simulator_session()
            with destination as selected:
                report=a.report.with_name(a.report.stem+'-application.json')
                command=[sys.executable,release.ROOT/'tests/ios-app/runner.py','--host-sdk',host_sdk,
                         '--target-sdk',sdk,'--work',root/'application','--result',report,
                         '--device' if device else '--simulator',selected]
                if os.environ.get('BEAM_IOS_SIGNING_TEAM'):command+=['--signing-team',os.environ['BEAM_IOS_SIGNING_TEAM']]
                subprocess.run(list(map(str,command)),check=True)
                evidence=release.read(report)
                if evidence['status']!='passed' or evidence['target']['sdk-build-id']!=record['sdk']['build-id']:
                    raise ValueError('Application evidence is incomplete or selects another SDK')
                if evidence['configuration']!='Release' or evidence['debugger_attached']:
                    raise ValueError('Release execution without a debugger is mandatory')
                if any(v['signing']!='verified' for v in evidence['variants'].values()):
                    raise ValueError('Unsigned application evidence is not qualification')
                results=[{'name':name,'status':'passed'} for name in release.IOS_APP_TESTS]
    except Exception as error:
        results=[{'name':name,'status':'failed','error':str(error)} for name in release.IOS_APP_TESTS]
    result={'schema':1,'kind':'ios-app','candidate':release.digest(a.record),
            'tests':results,'eligible':all(x['status']=='passed' for x in results),
            'host':host,'application':evidence,'physical_device_execution':'passed' if device and evidence.get('status')=='passed' else 'untested'}
    release.write(a.report,result)
    if not result['eligible']:raise SystemExit(1)


if __name__=='__main__':main()
