"""Keep failed, disabled and missing execution distinct in workflow summaries."""
import argparse
import json
import os
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--needs',required=True);p.add_argument('--reports',type=Path);a=p.parse_args()
needs=json.loads(a.needs);lines=['| Job | Result |','| --- | --- |'];failed=False
for name,job in needs.items():
    state=job['result'];lines.append(f'| {name} | {state} |');failed|=state not in ('success','skipped')
if a.reports:
    for file in sorted(a.reports.rglob('*.json')):
        data=json.loads(file.read_text())
        if data.get('physical_device_execution')=='untested':
            lines.append(f'| {file.stem}: physical-device execution | untested; excluded from emulator/simulator scope |')
        if 'tests' in data:
            for test in data['tests']:
                if test['status']!='passed':lines.append(f"| {file.stem}: {test['name']} | {test['status']} |");failed|=test['status'] in ('failed','untested','unexecuted')
text='\n'.join(lines)+'\n';print(text)
if os.environ.get('GITHUB_STEP_SUMMARY'):
    with open(os.environ['GITHUB_STEP_SUMMARY'],'a') as f:f.write(text)
if failed:raise SystemExit(1)
