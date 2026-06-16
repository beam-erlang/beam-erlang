"""Qualify transported target SDKs with their product-owned acceptance."""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import catalog
from workspace import run as owned_run
sys.path.insert(0,str(catalog.ROOT/'release'))
import release


def main():
    p=argparse.ArgumentParser();p.add_argument('--target',required=True);p.add_argument('--work',type=Path,required=True);p.add_argument('--inside',action='store_true');p.add_argument('--extended',action='store_true');a=p.parse_args()
    row=catalog.target(a.target);work=a.work.resolve()
    if row['container'] and not a.inside:
        from linux_container import run_container
        command=['python3','ci/test_artifact.py','--target',a.target,'--work','/work','--inside']
        if a.extended: command+=['--extended']
        run_container(catalog.ROOT,row,work,command)
        return
    record=work/'payload'/('candidate-'+a.target+'.json');reports=work/'reports';reports.mkdir(exist_ok=True)
    from workspace import workspace
    with workspace(work) as scratch:
        status=0
        def run(*command):
            nonlocal status
            result=owned_run([str(x) for x in command],check=False,cwd=catalog.ROOT)
            status=status or result.returncode
        if row['qualification']!='desktop':
            command=[sys.executable,'tests/target/execute.py','--record',record,'--work',scratch/'acquired','--report',reports/(a.target+'-acquired.json')]
            if a.extended:command+=['--extended']
            run(*command)
            if row['family']=='android':
                run(sys.executable,'release/release.py','acquire','--kind','package','--destination',scratch/'inputs')
                current=release.read(record)
                peers=[]
                for path in (work/'peer-payloads').rglob('candidate-*.json'):
                    candidate=release.read(path);other=candidate['platform']
                    if (other['family']=='android' and other['arch']!=row['arch'] and
                            other['host']==row['host'] and other['flavor']==row['flavor']):peers.append(path)
                if len(peers)==1:
                    run(sys.executable,'release/qualify-android.py','--record',record,'--peer-record',peers[0],
                        '--package',scratch/'inputs/package','--work',scratch/'android-app',
                        '--report',reports/(a.target+'-android-app.json'))
                else:
                    release.write(reports/(a.target+'-android-app.json'),{'schema':1,'kind':'android-app',
                        'candidate':release.digest(record),'eligible':False,'physical_device_execution':'untested',
                        'tests':[{'name':name,'status':'unexecuted','reason':'Exactly one transported peer ABI candidate is required'}
                                 for name in sorted(release.ANDROID_APP_TESTS)]})
                    status=status or 1
            if row['family']=='ios':
                run(sys.executable,'release/release.py','acquire','--kind','package','--destination',scratch/'inputs')
                run(sys.executable,'release/qualify-ios.py','--record',record,'--package',scratch/'inputs/package',
                    '--work',scratch/'ios-app','--report',reports/(a.target+'-ios-app.json'))
        else:
            run(sys.executable,'release/release.py','acquired','--record',record,'--work',scratch/'acquired','--report',reports/(a.target+'-acquired.json'))
            run(sys.executable,'release/release.py','acquire','--kind','package','--destination',scratch/'inputs')
            run(sys.executable,'release/qualify-package.py','--record',record,'--source',scratch/'inputs/package','--work',scratch/'package','--report',reports/(a.target+'-package.json'))
            if row['family']=='integration':
                run(sys.executable,'release/release.py','acquire','--kind','qualification','--destination',scratch/'inputs')
                run(sys.executable,'release/qualify-otp.py','--record',record,'--inputs-dir',scratch/'inputs','--work',scratch/'otp','--report',reports/(a.target+'-otp.json'),'--vm-report',reports/(a.target+'-beam-vm.json'))
        raise SystemExit(status)

if __name__=='__main__':main()
