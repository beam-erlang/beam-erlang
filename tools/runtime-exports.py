#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Intersect the canonical public inventory with a built Mach-O archive."""
import argparse
from pathlib import Path
import re
import subprocess

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--inventory',type=Path,required=True)
p.add_argument('--archive',type=Path,required=True)
p.add_argument('--nm',required=True)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args()
public=set(re.findall(r'^\s+([A-Za-z_][A-Za-z0-9_]*);',a.inventory.read_text(),re.M))
result=subprocess.run([a.nm,'-gU',str(a.archive)],check=True,capture_output=True,text=True)
defined={line.split()[-1] for line in result.stdout.splitlines() if line.split()}
exports=sorted('_'+name for name in public if '_'+name in defined)
if not exports or '_beam_vm_init' not in exports:
    raise SystemExit('public runtime export inventory is unavailable')
a.output.write_text(''.join(name+'\n' for name in exports))
