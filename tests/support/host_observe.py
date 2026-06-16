"""Read bounded native process observations without VM-reported substitutes."""

import json
from pathlib import Path

selected_observer = None

from .process import run


def state(observer, pid, *, cwd, env):
    output = run([observer, '--state', str(pid)], cwd=cwd, env=env, timeout=5)
    if output.stderr or len(output.stdout) > 4096:
        raise AssertionError('Invalid host process observation')
    value = json.loads(output.stdout)
    if (set(value) != {'alive', 'zombie'} or
            any(type(item) is not bool for item in value.values()) or
            value['zombie'] and not value['alive']):
        raise AssertionError('Invalid host process state')
    return None if not value['alive'] else 'Z' if value['zombie'] else 'running'

def images(pid, *, cwd, env):
    if selected_observer is None:
        raise AssertionError('Native host observer was not selected')
    result=run([selected_observer,'--images',str(pid)],cwd=cwd,env=env,timeout=5)
    if result.stderr or len(result.stdout)>262144:
        raise AssertionError('Invalid native image observation')
    value=json.loads(result.stdout)
    if (set(value)!={'executable','files'} or not isinstance(value['files'],list) or
        not value['files'] or any(not isinstance(p,str) or not Path(p).is_absolute()
            for p in [value['executable'],*value['files']])):
        raise AssertionError('Invalid native image inventory')
    return value
