"""Compare each registered product line with upstream releases, without merging."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess


def output(*args):return subprocess.check_output(args,text=True).strip()


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    root=Path(__file__).resolve().parents[1];policy=json.loads((root/'release/policy.json').read_text())
    raw=output('git','ls-remote','https://github.com/erlang/otp.git','refs/heads/maint*','refs/tags/OTP-*')
    refs={ref:commit for line in raw.splitlines() for commit,ref in [line.split()]}
    records=[];failed=False
    for branch,line in policy['lines'].items():
        record={'product_branch':branch,'erts_major':line['erts_major'],'upstream_line':line['upstream_line']}
        try:
            # Fetch committed metadata only; never switch, reset, merge or write a pin.
            subprocess.run(['git','-C',str(root),'fetch','--no-tags','origin',branch],check=True,capture_output=True)
            record['source']=output('git','-C',str(root),'rev-parse','FETCH_HEAD')
            pin=output('git','-C',str(root),'show','FETCH_HEAD:UPSTREAM_COMMIT')
            if not re.fullmatch('[a-f0-9]{40}',pin):raise ValueError('invalid recorded upstream provenance')
            record['upstream_commit']=pin
            major=line['upstream_line'].removeprefix('maint-')
            tags={ref.removeprefix('refs/tags/'):refs.get(ref+'^{}',commit) for ref,commit in refs.items()
                  if re.fullmatch(r'refs/tags/OTP-'+re.escape(major)+r'(?:\.[0-9]+)+',ref)}
            ordered=sorted(tags,key=lambda t:tuple(map(int,t.removeprefix('OTP-').split('.'))))
            record['latest_release']={'tag':ordered[-1],'commit':tags[ordered[-1]]} if ordered else None
            record['matching_releases']=[tag for tag in ordered if tags[tag]==pin]
            record['upstream_tip']=refs.get('refs/heads/'+line['upstream_line'])
            record['status']='current-release' if ordered and tags[ordered[-1]]==pin else 'review-upstream-changes'
            record['tip_differs']=record['upstream_tip']!=pin
        except (ValueError,subprocess.CalledProcessError) as error:
            record.update(status='audit-failed',error=str(error));failed=True
        records.append(record)
    a.output.write_text(json.dumps({'schema':1,'lines':records,'action':'review only; no automatic synchronization'},indent=2)+'\n')
    summary='| Product line | ERTS | Audit |\n| --- | --- | --- |\n'+''.join(f"| {r['product_branch']} | {r['erts_major']} | {r['status']} |\n" for r in records)
    print(summary)
    if os.environ.get('GITHUB_STEP_SUMMARY'):
        with open(os.environ['GITHUB_STEP_SUMMARY'],'a') as f:f.write(summary)
    if failed:raise SystemExit(1)

if __name__=='__main__':main()
