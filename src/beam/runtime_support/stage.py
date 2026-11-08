#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Stage an explicit owned runtime source for the fixed compiler projection."""
import argparse
import hashlib
import json
from pathlib import Path
import re


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--transform', type=Path, required=True)
    parser.add_argument('--canonical', action='store_true',
                        help='retain canonical imports for an owned compiler tool')
    args = parser.parse_args()
    data = args.source.read_bytes()
    source = data.decode('utf-8')
    declaration = re.compile(r'(?m)^-module\(([a-z][a-zA-Z0-9_]*)\)\.')
    # Later declarations can occur inside moduledoc examples. Only the actual
    # leading declaration is staged; documentation remains byte-preserved.
    first = declaration.search(source)
    if first is None or first.group(1) != args.source.stem:
        raise SystemExit(f'expected leading module declaration: {args.source}')
    original = first.group(1)
    if original == 'ms_transform':
        # Extend the owned tool without changing its seed-attested original.
        clause = ('copy({call,Anno,{remote,_Anno2,{atom,_Anno3,ets},{atom,_Anno4,fun2ms}},\n'
                  '      As0},Bound) ->\n'
                  '    {transform_call(ets,Anno,As0,Bound),Bound};')
        if source.count(clause) != 1:
            raise SystemExit('expected the pinned ETS fun2ms recognition clause')
        source = source.replace(clause, clause.replace('{atom,_Anno3,ets}',
                                '{atom,_Anno3,beam_ets}') + '\n' + clause, 1)
    expected = {'ets':'beam_ets', 'binary':'beam_binary', 'records':'beam_records', 'lists':'beam_lists', 'maps':'beam_maps',
                'string':'beam_string', 'unicode':'beam_unicode',
                'unicode_util':'beam_runtime_unicode_util',
                'queue':'beam_runtime_queue', 'sets':'beam_runtime_sets',
                'proplists':'beam_runtime_proplists',
                'orddict':'beam_runtime_orddict',
                'math':'beam_math', 'erl_ddll':'beam_ddll',
                'erl_debugger':'beam_debugger'}.get(original, original)
    if args.output.stem != expected:
        raise SystemExit(f'projection module mismatch: {expected} != {args.output.stem}')
    if args.canonical:
        if original != 'ms_transform':
            raise SystemExit('canonical staging is restricted to the owned match-spec tool')
        staged = source
    else:
        staged = declaration.sub(f'-module({expected}). -compile([{{parse_transform,beam_runtime_projection}},{{core_transform,beam_runtime_projection}}]).', source, count=1)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(staged.encode('utf-8'))
    receipt = {'source': str(args.source), 'module': original, 'runtime_module': expected,
               'source_sha256': hashlib.sha256(data).hexdigest(),
               'canonical_imports': args.canonical,
               'transform_sha256': hashlib.sha256(args.transform.read_bytes()).hexdigest(),
               'stage_sha256': hashlib.sha256(staged.encode()).hexdigest()}
    if original == 'ets':
        receipt['withheld_exports'] = {
            'omitted': [['table', 1], ['table', 2], ['from_dets', 2], ['to_dets', 2]],
            'pending': [['file2tab', 1], ['file2tab', 2], ['tab2file', 2], ['tab2file', 3],
                        ['tabfile_info', 1], ['i', 0], ['i', 1], ['i', 2], ['i', 3]]}
    args.output.with_suffix('.source.json').write_text(json.dumps(receipt,indent=2)+'\n')


if __name__ == '__main__':
    main()
