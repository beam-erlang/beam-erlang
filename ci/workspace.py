"""Own disposable producer/acquisition trees independently of transported inputs."""
from contextlib import contextmanager
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import tempfile


def run(command, *, check=True, **kwargs):
    """Stop all POSIX descendants before a runner can clean their inputs."""
    child = subprocess.Popen(command, start_new_session=os.name!='nt', **kwargs)
    try:
        result = subprocess.CompletedProcess(command, child.wait())
    except BaseException:
        if os.name!='nt':
            try: os.killpg(child.pid, signal.SIGTERM)
            except ProcessLookupError: pass
        else:
            child.terminate()
        try:
            child.wait(timeout=5)
        except subprocess.TimeoutExpired:
            if os.name!='nt':
                try: os.killpg(child.pid, signal.SIGKILL)
                except ProcessLookupError: pass
            else: child.kill()
            child.wait()
        if os.name!='nt':
            # A descendant can ignore SIGTERM after its group leader exits.
            try: os.killpg(child.pid, signal.SIGKILL)
            except ProcessLookupError: pass
        raise
    if result.returncode and os.name!='nt':
        try: os.killpg(child.pid, signal.SIGKILL)
        except ProcessLookupError: pass
    if check:
        result.check_returncode()
    return result


def diagnostics(source, destination):
    destination.mkdir(parents=True, exist_ok=True)
    retained, used = [], 0
    for directory, dirs, files in os.walk(source, followlinks=False):
        dirs[:] = sorted(d for d in dirs if not (Path(directory)/d).is_symlink())
        for name in sorted(files):
            path = Path(directory)/name
            if path.is_symlink() or path.suffix not in {'.log','.json','.txt','.yaml'}:
                continue
            relative = path.relative_to(source)
            if not ('Testing' in relative.parts or 'logs' in relative.parts or
                    len(relative.parts) == 1 or name == 'CMakeConfigureLog.yaml'):
                continue
            try:
                size = path.stat().st_size
            except OSError:
                continue
            count = min(size, 1024*1024)
            if used+count > 8*1024*1024 or len(retained) >= 256:
                continue
            if size > count and path.suffix in {'.json','.yaml'}:
                continue
            target = destination/relative
            target.parent.mkdir(parents=True, exist_ok=True)
            try:
                with path.open('rb') as stream:
                    if size > count:
                        stream.seek(size-count)
                    target.write_bytes(stream.read(count))
            except OSError:
                continue
            retained.append({'path':str(relative),'original_bytes':size,'retained_bytes':count})
            used += count
    return retained


@contextmanager
def workspace(work):
    work = Path(work)
    parent = work/'scratch'
    parent.mkdir(exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix='run-', dir=parent))
    previous = signal.getsignal(signal.SIGTERM)
    def terminate(signum, frame):
        raise SystemExit(128+signum)
    signal.signal(signal.SIGTERM, terminate)
    outcome = 'completed'
    try:
        yield root
    except BaseException as error:
        outcome = str(error)[:4096]
        raise
    finally:
        signal.signal(signal.SIGTERM, previous)
        retained = diagnostics(root, work/'diagnostics'/root.name)
        keep = os.environ.get('BEAM_TEST_KEEP_WORK') == '1'
        size = 0
        for directory, _, files in os.walk(root, followlinks=False):
            for filename in files:
                path=Path(directory)/filename
                try:
                    if not path.is_symlink(): size+=path.stat().st_size
                except OSError:
                    pass
        if not keep:
            def restore_permissions(function, path, error):
                try:
                    if Path(path).is_symlink():
                        os.unlink(path)
                    elif Path(path).is_dir():
                        os.chmod(path,0o700)
                        shutil.rmtree(path,onerror=restore_permissions)
                    else:
                        os.chmod(path,0o600)
                        function(path)
                except FileNotFoundError:
                    pass
            shutil.rmtree(root,onerror=restore_permissions)
        (work/'diagnostics'/root.name/'cleanup.json').write_text(json.dumps({
            'outcome':outcome,'retained':retained,'payloads_removed':not keep,
            'debug_path':str(root) if keep else None,'debug_bytes':size if keep else 0},indent=2)+'\n')
