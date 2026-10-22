"""Reuse all package suites with the archive's installed payload."""
import argparse
from pathlib import Path
import subprocess
import release
p = argparse.ArgumentParser()
for name in ['record','source','work','report']: p.add_argument('--'+name, required=True)
a = p.parse_args()
r = release.read(a.record)
release.validate_candidate_version(r)
if release.clean_revision(a.source) != r['inputs']['package']['commit']:
    raise ValueError('package qualification source mismatch')
archive = Path(a.record).parent/r['archive']['name']
release.verify_asset(archive, r['archive'])
work = Path(a.work).resolve()
prefix = release.extract(archive, work/'SDK ü')
subprocess.run(['cmake','-S',a.source,'-B',str(work/'build'),
                '-DCMAKE_PREFIX_PATH='+str(prefix), '-DBEAM_PACKAGE_ACQUIRED_PAYLOAD=ON'], check=True)
# A selected older source pin must fail instead of rebuilding a replacement tool.
cache = (work/'build/CMakeCache.txt').read_text()
if 'BEAM_PACKAGE_ACQUIRED_PAYLOAD:BOOL=ON' not in cache:
    raise ValueError('package pin lacks acquired-payload qualification support')
subprocess.run(['python3',str(release.ROOT/'release/release.py'),'ctest','--kind','package',
                '--build',str(work/'build'),'--record',a.record,'--report',a.report], check=True)
