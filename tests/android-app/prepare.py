#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Prepare a self-contained Android app input tree from explicitly installed SDKs."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import tomllib


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--host-sdk',type=Path,required=True)
    p.add_argument('--sdk',type=Path,action='append',required=True)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--cmake',default='cmake')
    a=p.parse_args();source=Path(__file__).resolve().parent
    if len(a.sdk)!=2: p.error('Select both Android ABI SDKs for one flavor')
    target=[]
    for sdk in a.sdk:
        sdk=sdk.resolve(strict=True);facts=tomllib.loads((sdk/'lib/beam/target.toml').read_text());target.append((sdk,facts))
    if {facts['android-abi'] for sdk,facts in target}!={'arm64-v8a','x86_64'} or len({facts['flavor'] for sdk,facts in target})!=1:
        p.error('SDK selections must contain both ABIs of one flavor')
    output=a.output.absolute()
    if output.exists():p.error('Output already exists; choose a new preparation directory')
    output.parent.mkdir(parents=True,exist_ok=True)
    host=a.host_sdk.resolve(strict=True)
    def run(*args):subprocess.run(list(map(str,args)),check=True)
    with tempfile.TemporaryDirectory(prefix='.android-inputs-',dir=output.parent) as temp:
        stage=Path(temp)/'inputs';stage.mkdir();records={}
        for sdk,facts in target:
            abi=facts['android-abi'];copied=stage/'sdk'/abi;shutil.copytree(sdk,copied,symlinks=True)
            package=Path(temp)/('package-'+abi);shutil.copytree(source/'package',package,ignore=shutil.ignore_patterns('build','.beam','beam.lock'))
            for command in ('build','install'):run(host/'bin/beam-pkg',command,'--target-sdk',copied,package)
            export=stage/'exports'/abi
            run(host/'bin/beam-pkg','export','--target-sdk',copied,'--output',export,package)
            assets=Path(temp)/('assets-'+abi)
            run(sys.executable,copied/'share/beam/tools/beam-android.py','assets','--sdk',copied,'--export',export,'--output',assets,'--cmake',a.cmake,
                '--inventory','library','--inventory','services','--component','file','--component','network','--component','zlib')
            (stage/'assets/beam').mkdir(parents=True,exist_ok=True);shutil.move(assets/'beam'/abi,stage/'assets/beam'/abi)
            records[abi]=facts
        run(sys.executable,stage/'sdk/arm64-v8a/share/beam/tools/beam-android.py','aar',
            '--sdk',stage/'sdk/arm64-v8a','--sdk',stage/'sdk/x86_64','--output',stage/'aars','--cmake',a.cmake)
        (stage/'inputs.json').write_text(json.dumps({'format':1,'targets':records},indent=2)+'\n')
        os.rename(stage,output)
    print(output)


if __name__=='__main__':main()
