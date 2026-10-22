"""Validate build/workflow contracts without provisioning any target."""
import json
from pathlib import Path
import re
import subprocess
import sys
import catalog
ROOT=catalog.ROOT
catalog.validate()
subprocess.run(['cmake','--list-presets=all'],cwd=ROOT,check=True)
expected={'checks','ci-linux','ci-windows','ci-macos','ci-android','ci-ios','integration','qualification','release-candidate','release-publish','containers-candidate','containers-publish','upstream-audit','_build-target','_test-artifact','_build-container'}
if {p.stem for p in (ROOT/'.github/workflows').glob('*.yml')}!=expected:raise ValueError('workflow inventory differs')
for path in (ROOT/'.github/workflows').glob('*.yml'):
    text=path.read_text()
    for ref in re.findall(r'uses:\s*[\'"]?(\./\.github/workflows/[^\s\'"\n]+)',text):
        if not (ROOT/ref).is_file():raise ValueError('missing reusable workflow: '+ref)
    for ref in re.findall(r'(?:python3?|bash|sh) ((?:ci|release|containers)/[\w/-]+\.(?:py|sh))',text):
        if not (ROOT/ref).is_file():raise ValueError('missing workflow command: '+ref)
for folder in ['ci','release','containers']:
    for path in (ROOT/folder).glob('*.py'):compile(path.read_text(),str(path),'exec')
# Runnable documentation belongs to the product; external owning-document links
# are intentionally not required in an independent source checkout.
for name in ['ci/README.md','release/README.md','tests/README.md']:
    path=ROOT/name
    if not path.exists():raise ValueError('missing owning manual '+name)
    for link in re.findall(r'\]\(([^)]+)\)',path.read_text()):
        if ':' in link or link.startswith('#'):continue
        target=(path.parent/link.split('#')[0]).resolve()
        if target.is_relative_to(ROOT) and not target.exists():raise ValueError('broken local documentation link: '+link)
print('Workflow inventory, commands, presets and local documentation links verified.')
