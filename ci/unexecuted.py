import argparse
from pathlib import Path
import sys
import catalog
sys.path.insert(0,str(catalog.ROOT/'release'))
import release
p=argparse.ArgumentParser();p.add_argument('--target',required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
r=catalog.target(a.target)
release.write(a.output/(a.target+'-acquired.json'),{'schema':1,'kind':'acquired','eligible':False,'tests':[{'name':'target-execution','status':'unexecuted','reason':r['execution_reason']} ]})
print(r['execution_reason'])
