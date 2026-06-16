"""Drop provisioning privileges before running a literal command vector."""
import os
from pathlib import Path
import subprocess
import sys


def main():
    uid = int(os.environ['BEAM_CONTAINER_UID'])
    gid = int(os.environ['BEAM_CONTAINER_GID'])
    if uid <= 0 or gid < 0 or not sys.argv[1:]:
        raise ValueError('Container execution requires a nonroot identity and command')
    home = Path('/tmp') / ('beam-home-' + str(uid))
    home.mkdir(mode=0o700, exist_ok=True)
    os.chown(home, uid, gid)
    os.setgroups([])
    os.setgid(gid)
    os.setuid(uid)
    os.environ['HOME'] = str(home)
    subprocess.run(['git', 'config', '--global', '--add', 'safe.directory', '/source'], check=True)
    os.execvp(sys.argv[1], sys.argv[1:])


if __name__ == '__main__':
    main()
