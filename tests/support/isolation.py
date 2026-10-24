"""Filesystem denial for Darwin acceptance children; never an execution fallback."""

from pathlib import Path
import sys


class Seatbelt:
    """Keep copied acceptance inputs outside the original, denied prefixes."""

    def __init__(self, work, forbidden, executable=Path('/usr/bin/sandbox-exec')):
        if sys.platform != 'darwin':
            raise RuntimeError('Seatbelt requires Darwin')
        self.work = Path(work).resolve(strict=True)
        self.executable = Path(executable).resolve(strict=True)
        self.forbidden = sorted({Path(path).resolve(strict=True) for path in forbidden})
        if not self.forbidden:
            raise ValueError('Isolation requires explicit forbidden inputs')
        for path in self.forbidden:
            if self.work == path or path in self.work.parents:
                raise ValueError(f'Acceptance work is inside a forbidden prefix: {path}')
        self.profile = self.work / 'filesystem-denial.sb'
        rules = ['(version 1)', '(allow default)']
        for index in range(len(self.forbidden)):
            rules.append('(deny file-read* file-write* process-exec file-map-executable '
                         f'(subpath (param "DENY{index}")))')
        self.profile.write_text('\n'.join(rules) + '\n')

    def command(self, arguments):
        command = [str(self.executable), '-f', str(self.profile)]
        for index, path in enumerate(self.forbidden):
            command += ['-D', f'DENY{index}={path}']
        return command + [str(argument) for argument in arguments]
