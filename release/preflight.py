#!/usr/bin/env python3
"""Require actual namespaces, private loopback and an unprivileged test account."""
import os
import shutil
import subprocess
import sys

if os.getuid() == 0 or os.geteuid() == 0:
    sys.exit('Qualification must run under an unprivileged account')
for command in ['bwrap', 'ip', 'cmake', 'ninja', 'cc', 'c++', 'perl', 'python3']:
    if not shutil.which(command):
        sys.exit('Missing qualification prerequisite: ' + command)
subprocess.run(['bwrap', '--unshare-all', '--die-with-parent', '--ro-bind', '/', '/',
                '--proc', '/proc', '--dev', '/dev', '--tmpfs', '/tmp',
                'python3', '-c', "import socket; s=socket.socket(); s.bind(('127.0.0.1',0)); s.listen(1); c=socket.create_connection(s.getsockname()); a,_=s.accept(); c.sendall(b'ok'); assert a.recv(2)==b'ok'"], check=True)
print('Unprivileged namespaces and private loopback verified')
