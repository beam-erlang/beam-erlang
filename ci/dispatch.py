"""Dispatch each maintained line's own extended-qualification workflow."""
import json
import os
import subprocess
from pathlib import Path
policy=json.loads((Path(__file__).resolve().parents[1]/'release/policy.json').read_text())
for branch in policy['lines']:
    subprocess.run(['gh','workflow','run','qualification.yml','--repo',policy['repository'],'--ref',branch],check=True)
