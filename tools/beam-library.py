#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Generate a passive exact-SDK descriptor from explicitly supplied bytecode."""
import argparse
import json
from pathlib import Path
import re


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('name', 'build-id', 'accessor', 'output'):
        parser.add_argument('--' + name, required=True)
    parser.add_argument('--module', nargs=2, action='append', default=[])
    parser.add_argument('--nif', nargs=2, action='append', default=[])
    parser.add_argument('--shared', action='store_true')
    args = parser.parse_args()
    if not re.fullmatch('[0-9a-f]{64}', args.build_id):
        parser.error('invalid SDK identity')
    for group in (args.module, args.nif):
        names = [name for name, _ in group]
        if len(set(names)) != len(names) or any(not re.fullmatch('[a-z][a-zA-Z0-9_@]*', name) for name in names):
            parser.error('invalid or duplicate module names')
    for symbol in [args.accessor] + [symbol for _, symbol in args.nif]:
        if not re.fullmatch('[a-zA-Z_][a-zA-Z0-9_]*', symbol):
            parser.error('invalid C identifier')
    quote = json.dumps
    lines = ['/* Generated passive BEAM library. */', '#include <beam/library.h>']
    modules = []
    for index, (name, filename) in enumerate(args.module):
        data = Path(filename).read_bytes()
        if len(data) < 12 or data[:4] != b'FOR1' or data[8:12] != b'BEAM' or int.from_bytes(data[4:8], 'big') != len(data) - 8:
            parser.error('invalid BEAM container: ' + filename)
        lines.append(f'static const unsigned char module_{index}[] = {{')
        lines.extend(','.join(str(byte) for byte in data[start:start+24]) + ',' for start in range(0, len(data), 24))
        lines.append('};')
        # Let acquisition supply canonical image provenance for shared delivery
        # and descriptor-name provenance for linked delivery.
        modules.append(f'{{{quote(name)}, {{module_{index}, sizeof(module_{index})}}, NULL}}')
    if modules:
        lines.append('static const beam_module_spec modules[] = {' + ','.join(modules) + '};')
    nifs = []
    for name, symbol in args.nif:
        lines.append(f'extern struct enif_entry_t *{symbol}(void);')
        nifs.append(f'{{sizeof(beam_builtin_nif), {quote(name)}, {quote(args.build_id)}, {symbol}, 1}}')
    if nifs:
        lines.append('static const beam_builtin_nif nifs[] = {' + ','.join(nifs) + '};')
    symbol = 'beam_library_descriptor_v1' if args.shared else 'descriptor'
    if args.shared:
        lines += ['#ifdef _WIN32', '__declspec(dllexport)', '#else', '__attribute__((visibility("default")))', '#endif']
    else:
        lines.append('static')
    lines.append(f'const beam_library_descriptor {symbol} = {{sizeof(beam_library_descriptor), BEAM_LIBRARY_FORMAT_VERSION, '
                 f'{quote(args.build_id)}, {quote(args.name)}, {len(modules)}, {"modules" if modules else "NULL"}, '
                 f'{len(nifs)}, {"nifs" if nifs else "NULL"}' + '};')
    lines.append(f'const beam_library_descriptor *{args.accessor}(void) {{ return &{symbol}; }}')
    Path(args.output).write_text('\n'.join(lines) + '\n')


if __name__ == '__main__':
    main()
