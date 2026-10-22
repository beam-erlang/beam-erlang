"""Qualify transported target SDKs with their product-owned acceptance."""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import catalog
sys.path.insert(0,str(catalog.ROOT/'release'))
import release


def main():
    p=argparse.ArgumentParser();p.add_argument('--target',required=True);p.add_argument('--work',type=Path,required=True);p.add_argument('--inside',action='store_true');p.add_argument('--extended',action='store_true');a=p.parse_args()
    row=catalog.target(a.target);work=a.work.resolve()
    if row['container'] and not a.inside:
        command=['docker','run','--rm','--init','-v',f'{catalog.ROOT}:/source','-v',f'{work}:/work','-w','/source',row['container'],'sh','ci/linux-container.sh','python3','ci/test_artifact.py','--target',a.target,'--work','/work','--inside']
        subprocess.run(command,check=True);return
    record=work/'payload'/('candidate-'+a.target+'.json');reports=work/'reports';reports.mkdir(exist_ok=True)
    status=0
    def run(*command):
        nonlocal status
        result=subprocess.run([str(x) for x in command],cwd=catalog.ROOT)
        status=status or result.returncode
    if row['qualification']!='desktop':
        command=[sys.executable,'tests/target/execute.py','--record',record,'--work',work/'acquired','--report',reports/(a.target+'-acquired.json')]
        if a.extended:command+=['--extended']
        run(*command)
    else:
        run(sys.executable,'release/release.py','acquired','--record',record,'--work',work/'acquired','--report',reports/(a.target+'-acquired.json'))
        run(sys.executable,'release/release.py','acquire','--kind','package','--destination',work/'inputs')
        run(sys.executable,'release/qualify-package.py','--record',record,'--source',work/'inputs/package','--work',work/'package','--report',reports/(a.target+'-package.json'))
        if row['family']=='integration':
            run(sys.executable,'release/release.py','acquire','--kind','qualification','--destination',work/'inputs')
            run(sys.executable,'release/qualify-otp.py','--record',record,'--inputs-dir',work/'inputs','--work',work/'otp','--report',reports/(a.target+'-otp.json'))
    raise SystemExit(status)

if __name__=='__main__':main()
