#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Run a copied Android consumer with original input trees inaccessible."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--deny',type=Path,action='append',required=True)
    p.add_argument('--inside',action='store_true')
    p.add_argument('command',nargs=argparse.REMAINDER)
    a=p.parse_args();command=a.command
    if command and command[0]=='--':command=command[1:]
    if not command:p.error('A consumer command is required')
    paths=sorted({str(x.resolve(strict=True)) for x in a.deny})
    if sys.platform=='darwin':
        profile='(version 1)(allow default)'+''.join('(deny file-read* file-write* process-exec file-map-executable (subpath '+json.dumps(x)+'))' for x in paths)
        os.execv('/usr/bin/sandbox-exec',['sandbox-exec','-p',profile,*command])
    elif sys.platform.startswith('linux'):
        if not a.inside:
            args=['unshare','--user','--map-root-user','--mount','--fork',sys.executable,__file__,'--inside']
            for path in paths:args+=['--deny',path]
            os.execvp('unshare',[*args,'--',*command])
        subprocess.run(['mount','--make-rprivate','/'],check=True)
        for path in paths:
            if not Path(path).is_dir():raise ValueError('Isolation requires directory roots')
            subprocess.run(['mount','-t','tmpfs','-o','size=4k,ro','beam-android-denied',path],check=True)
        os.execvpe(command[0],command,os.environ)
    else:raise RuntimeError('Android consumer isolation is qualified on macOS and Linux only')


if __name__=='__main__':main()
