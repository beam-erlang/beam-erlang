"""Run target Linux commands with read-only sources and an unprivileged owner."""
import os
from pathlib import Path
import subprocess
import signal
import uuid

PRODUCER_ENV = ('GITHUB_SHA', 'GITHUB_REPOSITORY', 'GITHUB_RUN_ID',
                'GITHUB_RUN_ATTEMPT', 'GITHUB_REF_NAME', 'GITHUB_WORKFLOW_REF',
                'ImageOS', 'ImageVersion', 'CMAKE_BUILD_PARALLEL_LEVEL',
                'BEAM_TEST_KEEP_WORK')


def run_container(root, row, work, arguments):
    root, work = Path(root).resolve(), Path(work).resolve()
    uid, gid = os.getuid(), os.getgid()
    if uid == 0:
        raise ValueError('Linux acceptance requires an unprivileged invoking user')
    work.mkdir(parents=True, exist_ok=True)
    name = 'beam-ci-' + uuid.uuid4().hex
    command = ['docker', 'run', '--rm', '--init', '--name', name,
               '--platform', 'linux/' + row['arch'],
               '--mount', f'type=bind,source={root},target=/source,readonly',
               '--mount', f'type=bind,source={work},target=/work',
               '--workdir', '/source', '--env', f'BEAM_CONTAINER_UID={uid}',
               '--env', f'BEAM_CONTAINER_GID={gid}']
    for key in PRODUCER_ENV:
        if key in os.environ:
            command += ['--env', key]
    shell = 'bash' if 'debian' in row['container'] else 'sh'
    command += [row['container'], shell, 'ci/linux-container.sh', *map(str, arguments)]
    previous = signal.getsignal(signal.SIGTERM)
    def terminate(signum, frame):
        raise SystemExit(128+signum)
    signal.signal(signal.SIGTERM,terminate)
    try:
        subprocess.run(command, check=True)
    finally:
        # The task's unique name cannot identify another run's inputs/processes.
        try:
            subprocess.run(['docker', 'rm', '--force', name], check=False,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        finally:
            signal.signal(signal.SIGTERM,previous)
