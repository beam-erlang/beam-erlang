"""Canonical preset build/install/test front door, shared by CI and candidates."""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import catalog
sys.path.insert(0,str(catalog.ROOT/'release'))
import release


def run(*args): subprocess.run(list(map(str,args)),check=True,cwd=catalog.ROOT)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--target',required=True);p.add_argument('--work',type=Path,required=True)
    p.add_argument('--candidate',action='store_true');p.add_argument('--inside',action='store_true');p.add_argument('--preset')
    a=p.parse_args();t=catalog.target(a.target);work=a.work.resolve();work.mkdir(parents=True,exist_ok=True)
    if t['container'] and not a.inside:
        cmd=['docker','run','--rm','--init','--volume',f'{catalog.ROOT}:/source','--volume',f'{work}:/work','--workdir','/source']
        for key in ['GITHUB_SHA','GITHUB_REPOSITORY','GITHUB_RUN_ID','GITHUB_RUN_ATTEMPT','GITHUB_REF_NAME','ImageOS','ImageVersion']:
            if key in os.environ: cmd+=['--env',key]
        cmd += [t['container'],'bash' if 'debian' in t['container'] else 'sh','ci/linux-container.sh','python3','ci/build.py','--target',t['id'],'--work','/work','--inside']
        if a.candidate: cmd+=['--candidate']
        if a.preset: cmd+=['--preset',a.preset]
        run(*cmd);return
    package=work/'inputs/package'
    if not package.exists(): run(sys.executable,'release/release.py','acquire','--kind','package','--destination',work/'inputs')
    build=work/'build';prefix=work/'prefix';out=work/'payload';out.mkdir(exist_ok=True)
    run('cmake','--preset',a.preset or t['preset'],'-B',build,'-DCMAKE_INSTALL_PREFIX='+str(prefix),'-DBEAM_PACKAGE_SOURCE_DIR='+str(package))
    run('cmake','--build',build,'--parallel',os.environ.get('CMAKE_BUILD_PARALLEL_LEVEL','2'))
    run('cmake','--install',build)
    if a.candidate:
        run(sys.executable,'release/finalize.py','--target',a.target,'--prefix',prefix,'--build',build)
    # Always preserve production evidence and a real artifact after test failure.
    report=out/(t['id']+'-beam.json')
    result=subprocess.run([sys.executable,'release/release.py','ctest','--kind','beam','--build',str(build),'--report',str(report)],cwd=catalog.ROOT)
    cmd=[sys.executable,'release/release.py','pack','--target',t['id'],'--flavor',t['flavor'],'--build',str(build),'--prefix',str(prefix),'--package',str(package),'--output',str(out)]
    if not a.candidate: cmd+=['--development']
    run(*cmd)
    run(sys.executable,'release/bind-report.py',report,out/('candidate-'+t['id']+'.json'))
    if result.returncode: raise SystemExit(result.returncode)

if __name__=='__main__': main()
