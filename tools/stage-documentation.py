"""Stage public manuals with links relative to the installed SDK."""
import argparse
import os
from pathlib import Path
import re
import shutil
from urllib.parse import unquote


LINK = re.compile(r'\[([^\]\n]+)\]\(([^)\n]+)\)')
EXAMPLE_FILES = ('README.md', 'CMakeLists.txt', 'EmbedBytes.cmake', 'answer.erl', 'main.c')
DOC_ROOT = Path('share/doc/beam')


def stage(source, output, package=None):
    source = source.resolve()
    manuals = source / 'docs'
    example = source / 'tests/installed/sdk/consumer/first-call'
    copies = {p.resolve(): DOC_ROOT / p.relative_to(manuals)
              for p in manuals.rglob('*') if p.is_file()}
    copies.update({(example / name).resolve(): DOC_ROOT / 'examples/first-call' / name
                   for name in EXAMPLE_FILES})
    if package:
        for name in ('package-format.md', 'commands.md'):
            copies[(package / 'docs' / name).resolve(strict=True)] = DOC_ROOT / 'package' / name
    targets = dict(copies)
    targets[example.resolve()] = DOC_ROOT / 'examples/first-call/README.md'
    for header in (source / 'include/beam').rglob('*.h'):
        targets[header.resolve()] = Path('include/beam') / header.relative_to(source / 'include/beam')
    for header in ('erl_nif', 'erl_driver', 'erl_drv_nif', 'erl_nif_api_funcs'):
        targets[(source / f'src/erts/emulator/beam/{header}.h').resolve()] = Path(f'include/beam/nif/{header}.h')

    def rewrite(origin, destination, match):
        label, url = match.groups()
        if re.match(r'[A-Za-z][A-Za-z0-9+.-]*:', url) or url.startswith(('#', '//')):
            return match.group()
        path, separator, fragment = url.partition('#')
        resolved = (origin.parent / unquote(path)).resolve()
        target = targets.get(resolved)
        if resolved == source / 'README.md' and fragment == 'building':
            target, separator, fragment = DOC_ROOT / 'getting-started/building.md', '', ''
        if package and resolved == (package.parent / 'erlang-otp/README.md').resolve():
            target, separator, fragment = DOC_ROOT / 'concepts.md', '#', 'beamerlang-and-erlangotp'
        if target is not None:
            relative = Path(os.path.relpath(target, destination.parent)).as_posix()
            return f'[{label}]({relative}{separator}{fragment})'
        # Implementation and maintainer documents belong to the source distribution.
        if resolved.is_relative_to(source):
            reference = resolved.relative_to(source).as_posix()
        elif package and resolved.is_relative_to(package):
            reference = 'beam-erlang-package/' + resolved.relative_to(package).as_posix()
        else:
            reference = path.lstrip('./')
        return f'{label} (source distribution: `{reference}{separator}{fragment}`)'

    if output.exists():
        shutil.rmtree(output)
    output.mkdir(parents=True)
    for origin, destination in sorted(copies.items()):
        target = output / destination.relative_to(DOC_ROOT)
        target.parent.mkdir(parents=True, exist_ok=True)
        if origin.suffix == '.md':
            text = origin.read_text(encoding='utf-8')
            target.write_text(LINK.sub(lambda m: rewrite(origin, destination, m), text), encoding='utf-8')
        else:
            shutil.copy2(origin, target)
    (output / '.stamp').touch()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--package', type=Path)
    args = parser.parse_args()
    stage(args.source, args.output, args.package.resolve() if args.package else None)
