import argparse
from pathlib import Path
import sys
import catalog
sys.path.insert(0,str(catalog.ROOT/'release'))
import release
p=argparse.ArgumentParser();p.add_argument('--target',required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
r=catalog.target(a.target)
for kind in release.report_kinds(a.target):
    if kind == 'beam':
        continue  # Producer evidence is independent of executor availability.
    names = release.VM_TESTS if kind == 'beam-vm' else release.OTP_TESTS if kind == 'otp' else release.PACKAGE_TESTS if kind == 'package' else {'target-execution'}
    release.write(a.output/(a.target+'-'+kind+'.json'),{'schema':1,'kind':kind,'eligible':False,
        'tests':[{'name':name,'status':'unexecuted','reason':r['execution_reason']} for name in sorted(names)]})
print(r['execution_reason'])
