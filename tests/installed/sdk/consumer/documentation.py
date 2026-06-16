"""Installed manual navigation and the canonical public tutorial sources."""
from pathlib import Path
import re
from urllib.parse import unquote

from support.process import check


def check_documentation(prefix):
    root = prefix / 'share/doc/beam'
    check((root / 'README.md').is_file(), 'installed SDK lacks its manual index')
    for name in ('README.md', 'CMakeLists.txt', 'EmbedBytes.cmake', 'main.c', 'answer.erl'):
        check((root / 'examples/first-call' / name).is_file(), f'installed tutorial lacks {name}')
    for page in root.rglob('*.md'):
        text = page.read_text(encoding='utf-8')
        source = Path(__file__).resolve().parents[4]
        check(str(source) not in text and str(prefix.resolve()) not in text,
              f'installed manual retains a development prefix: {page.relative_to(root)}')
        for link in re.findall(r'\]\(([^)\n]+)\)', text):
            if re.match(r'[A-Za-z][A-Za-z0-9+.-]*:', link) or link.startswith(('#', '//')):
                continue
            target = (page.parent / unquote(link.split('#')[0])).resolve()
            check(target.is_relative_to(prefix.resolve()) and target.exists(),
                  f'broken installed manual link: {page.relative_to(root)}: {link}')
