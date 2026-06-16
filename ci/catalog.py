"""CI orchestration catalog. Compiler settings remain in CMake presets."""
import argparse
import json
import os
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]


def targets():
    return json.loads((ROOT/'ci/targets.json').read_text())['targets']


def target(name):
    return next(t for t in targets() if t['id'] == name)


def validate():
    data = json.loads((ROOT/'CMakePresets.json').read_text())
    presets = {p['name']: p for p in data['configurePresets']}
    bases = json.loads((ROOT/'containers/bases.json').read_text())
    ids = [r['id'] for r in targets()]
    if len(ids) != len(set(ids)):
        raise ValueError('duplicate target identities')
    for r in targets():
        if r['container']:
            tag,digest=r['container'].split('@',1)
            if bases.get(tag)!=digest:raise ValueError('container base pin differs from lock')
        toolchain=presets[r['preset']].get('cacheVariables',{}).get('CMAKE_TOOLCHAIN_FILE')
        if toolchain and not Path(toolchain.replace('${sourceDir}',str(ROOT))).is_file():
            raise ValueError('missing target toolchain')
        if r['preset'] not in presets:
            raise ValueError('missing preset: '+r['id'])
        if presets[r['preset']]['cacheVariables']['ERTS_BUILD_FLAVOR'] != r['flavor']:
            raise ValueError('catalog/preset flavor mismatch')
        for kind in ('buildPresets','testPresets'):
            if r['preset'] not in {p['name'] for p in data[kind]}:
                raise ValueError('missing '+kind+': '+r['id'])
    policy = json.loads((ROOT/'release/policy.json').read_text())
    if set(policy['targets']) != {r['id'] for r in targets() if r['release']}:
        raise ValueError('release target catalog differs from policy')
    if set(policy['integration_targets']) != {r['id'] for r in targets() if not r['release']}:
        raise ValueError('nonpublic qualification routes differ from policy')
    for branch,line in policy['lines'].items():
        if 'stable' in line['channels'] and branch != 'maint-'+line['erts_major']:
            raise ValueError('stable publication requires its ERTS maintenance branch')


def matrix(family=None, release=False):
    rows = [r.copy() for r in targets() if (not family or r['family']==family)
            and (not release or r['id'] in json.loads((ROOT/'release/policy.json').read_text())['targets'] + json.loads((ROOT/'release/policy.json').read_text())['integration_targets'])]
    for r in rows:
        r['test_runner'] = r['runner']
        if r['executor']=='android-amd64':
            r['test_runner']='ubuntu-24.04'
        elif r['executor']=='ios-device':
            r['test_runner']=os.environ.get('BEAM_IOS_DEVICE_RUNNER') or 'beam-ios-device'
            r['execution_enabled']=bool(os.environ.get('BEAM_IOS_DEVICE_RUNNER'))
            if not r['execution_enabled']: r['execution_reason']='Physical iOS device runner and signing access are not configured'
        elif r['executor']=='android-arm64':
            r['test_runner']=os.environ.get('BEAM_ANDROID_ARM64_RUNNER') or 'beam-android-arm64'
            r['execution_enabled']=bool(os.environ.get('BEAM_ANDROID_ARM64_RUNNER'))
    return {'include':rows}


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('command',choices=['check','matrix','get'])
    p.add_argument('--family');p.add_argument('--release',action='store_true');p.add_argument('--target')
    a=p.parse_args();validate()
    if a.command=='check': print('Target catalog, presets and release lines agree.')
    else: print(json.dumps(target(a.target) if a.command=='get' else matrix(a.family,a.release)))

if __name__=='__main__': main()
