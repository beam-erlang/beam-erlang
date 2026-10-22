"""Verify a cross-built SDK's installation inventory without executing its code."""
import argparse
import hashlib
import json
import subprocess
import tempfile
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--build',type=Path,required=True);a=p.parse_args()
with tempfile.TemporaryDirectory() as tmp:
    prefix=Path(tmp)/'SDK with spaces'
    subprocess.run(['cmake','--install',str(a.build),'--prefix',str(prefix)],check=True)
    inventory=json.loads((prefix/'lib/beam/runtime-artifacts.json').read_text())
    for entry in inventory['artifacts'].values():
        path=prefix/entry['path']
        if hashlib.sha256(path.read_bytes()).hexdigest()!=entry['sha256']:
            raise ValueError('installed artifact differs: '+entry['path'])
    for name in ['runtime','runtime.static','header.beam_native']:
        if name not in inventory['artifacts']: raise ValueError('missing SDK asset '+name)
    if not (a.build/'target-acceptance/answer_beam.h').is_file():
        raise ValueError('missing source-built embedding fixture')
