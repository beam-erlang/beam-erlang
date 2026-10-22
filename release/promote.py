#!/usr/bin/env python3
"""Verify trusted candidate origin and promote existing bytes without rebuilding."""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile
import release


def gh(*args):
    return release.run('gh', *args)


def preview_tag(manifest):
    if release.validate_version_record(manifest)['channel'] != 'preview':
        raise ValueError('preview version required')
    return 'v' + manifest['version']


def trusted_run(info, manifest, policy, jobs):
    if (info['event'] != 'workflow_dispatch' or info['path'] != policy['candidate_workflow']
        or info['head_repository']['full_name'] != policy['repository']
        or info['head_branch'] != 'master' or info['head_sha'] != manifest['source']
        or str(info['id']) != manifest['run_id'] or info['conclusion'] != 'success'):
        raise ValueError('candidate does not originate from a successful trusted workflow on master')
    expected = {f'{job} ({flavor})' for job in ['build','acquired','otp'] for flavor in ['emu','jit']} | {'collect'}
    observed = {j['name']: j for j in jobs}
    if not expected <= observed.keys() or any(observed[name]['conclusion'] != 'success' for name in expected):
        raise ValueError('mandatory candidate jobs did not succeed')


def protected_environment(environment):
    reviewers = next((rule for rule in environment.get('protection_rules', [])
                      if rule.get('type') == 'required_reviewers'), {})
    branches = environment.get('deployment_branch_policy') or {}
    if not reviewers.get('reviewers') or not reviewers.get('prevent_self_review'):
        raise ValueError('preview-release environment requires independent reviewers')
    if not branches.get('protected_branches'):
        raise ValueError('preview-release environment must restrict deployment to protected branches')


def payloads(manifest):
    entries = {}
    for r in manifest['candidates'].values():
        for item in [r['archive'], r['acceptance'], r['environment'], *r['sources']]:
            if Path(item['name']).name != item['name']:
                raise ValueError('unsafe asset name')
            if item['name'] in entries and entries[item['name']] != item:
                raise ValueError('conflicting payloads')
            entries[item['name']] = item
    for item in manifest['reports']:
        if Path(item['name']).name != item['name'] or item['name'] in entries:
            raise ValueError('unsafe or conflicting report name')
        entries[item['name']] = item
    return entries


def prepare(args):
    policy = release.read(release.ROOT/'release/policy.json')
    folder = Path(args.directory).resolve()
    path = folder/'candidate-manifest.json'
    if release.digest(path) != args.digest:
        raise ValueError('selected manifest digest mismatch')
    manifest = release.read(path)
    release.eligible(manifest, policy)
    preview_tag(manifest)
    if manifest.get('eligible') is not True or manifest.get('blockers'):
        raise ValueError('selected candidate was not declared eligible')
    if manifest['run_id'] != args.run:
        raise ValueError('run selection mismatch')
    import json
    repository = json.loads(gh('api', f'repos/{policy["repository"]}'))
    if repository.get('visibility') != 'public' or repository.get('private') is not False:
        raise ValueError('public release hosting requires a reviewed public repository')
    protected_environment(json.loads(gh('api', f'repos/{policy["repository"]}/environments/preview-release')))
    branch = json.loads(gh('api', f'repos/{policy["repository"]}/branches/master'))
    if not branch.get('protected'):
        raise ValueError('publication requires protected master')
    info = json.loads(gh('api', f'repos/{policy["repository"]}/actions/runs/{args.run}'))
    jobs = json.loads(gh('api', '--paginate', '--slurp', f'repos/{policy["repository"]}/actions/runs/{args.run}/jobs?filter=latest&per_page=100'))
    trusted_run(info, manifest, policy, [j for page in jobs for j in page['jobs']])
    # Approved candidate workflow bytes must match this protected publisher checkout.
    workflow = gh('api', '-H', 'Accept: application/vnd.github.raw+json',
                  f'repos/{policy["repository"]}/contents/{policy["candidate_workflow"]}?ref={manifest["source"]}')
    if workflow != (release.ROOT/policy['candidate_workflow']).read_text().strip():
        raise ValueError('candidate workflow differs from reviewed publication workflow contract')
    for item in payloads(manifest).values():
        release.verify_asset(folder/item['name'], item)
    for key, report in manifest['qualification'].items():
        if release.read(folder/(key + '.json')) != report:
            raise ValueError('qualification report content mismatch')
    for f, expected in manifest['candidate_digests'].items():
        candidate = folder/f'candidate-{f}.json'
        if release.digest(candidate) != expected or release.read(candidate) != manifest['candidates'][f]:
            raise ValueError('candidate record mismatch')
    for path in [folder/name for name in payloads(manifest)] + [folder/'candidate-manifest.json']:
        gh('attestation','verify', str(path), '--repo', policy['repository'],
           '--signer-workflow', policy['repository']+'/'+policy['candidate_workflow'],
           '--source-digest', manifest['source'], '--deny-self-hosted-runners')
    shutil.copy2(folder/'candidate-manifest.json', folder/'release-manifest.json')


def offline_material(args):
    import json
    policy = release.read(release.ROOT/'release/policy.json')
    folder = Path(args.directory).resolve()
    manifest = release.read(folder/'release-manifest.json')
    release.eligible(manifest, policy)
    tag = preview_tag(manifest)
    if manifest.get('eligible') is not True or manifest.get('blockers'):
        raise ValueError('selected candidate was not declared eligible')
    if manifest['run_id'] != args.run or release.digest(folder/'release-manifest.json') != args.digest:
        raise ValueError('publication selection changed')
    repo = policy['repository']
    paths = [folder/name for name in payloads(manifest)] + [folder/'release-manifest.json']
    subjects = {path.name: release.digest(path) for path in paths}
    releases = json.loads(gh('api', '--paginate', '--slurp', f'repos/{repo}/releases?per_page=100'))
    existing = next((r for page in releases for r in page if r['tag_name'] == tag), None)
    kit = folder/'offline-verification.tar.gz'
    if existing and any(a['name'] == kit.name for a in existing['assets']):
        gh('release', 'download', tag, '--repo', repo, '--pattern', kit.name, '--dir', str(folder))
        gh('attestation', 'verify', str(kit), '--repo', repo,
           '--signer-workflow', repo+'/'+policy['publication_workflow'], '--deny-self-hosted-runners')
        with tempfile.TemporaryDirectory() as temp:
            retained = release.extract(kit, Path(temp)/'retained')
            if release.read(retained/'subjects.json') != subjects:
                raise ValueError('retained verification kit belongs to different payloads')
        return
    offline = folder/'offline'
    offline.mkdir()
    for path in paths:
        subprocess.run(['gh','attestation','download',str(path),'--repo',repo], cwd=offline, check=True)
    (offline/'trusted_root.jsonl').write_text(gh('attestation','trusted-root')+'\n')
    (offline/'repository-id.txt').write_text(gh('api','repos/'+repo,'--jq','.id')+'\n')
    release.write(offline/'subjects.json', subjects)
    release.archive_tree(offline, kit, 'offline-verification')


def publish(args):
    policy = release.read(release.ROOT/'release/policy.json')
    folder = Path(args.directory).resolve()
    manifest = release.read(folder/'release-manifest.json')
    release.eligible(manifest, policy)
    tag = preview_tag(manifest)
    if manifest.get('eligible') is not True or manifest.get('blockers'):
        raise ValueError('selected candidate was not declared eligible')
    if manifest['run_id'] != args.run or release.digest(folder/'release-manifest.json') != args.digest:
        raise ValueError('publication selection changed')
    entries = payloads(manifest)
    for item in entries.values():
        release.verify_asset(folder/item['name'], item)
    repo = policy['repository']
    import json
    tags = json.loads(gh('api', '--paginate', '--slurp', f'repos/{repo}/tags?per_page=100'))
    existing_tag = next((t for page in tags for t in page if t['name'] == tag), None)
    if existing_tag and existing_tag['commit']['sha'] != manifest['source']:
        raise ValueError('existing release tag points to a different source revision')
    paths = [folder/name for name in entries] + [folder/'release-manifest.json', folder/'offline-verification.tar.gz']
    gh('attestation', 'verify', str(folder/'offline-verification.tar.gz'), '--repo', repo,
       '--signer-workflow', repo+'/'+policy['publication_workflow'], '--deny-self-hosted-runners')
    with tempfile.TemporaryDirectory() as scratch:
        kit = release.extract(folder/'offline-verification.tar.gz', Path(scratch)/'kit')
        expected = {path.name: release.digest(path) for path in paths if path.name != 'offline-verification.tar.gz'}
        if release.read(kit/'subjects.json') != expected:
            raise ValueError('verification kit belongs to different payloads')
    # Retry only existing matching bytes. Never use --clobber or replace a tag.
    import json
    releases = json.loads(gh('api', '--paginate', '--slurp', f'repos/{repo}/releases?per_page=100'))
    existing = next((r for page in releases for r in page if r['tag_name'] == tag), None)
    if existing:
        with tempfile.TemporaryDirectory() as scratch:
            gh('release','download',tag,'--repo',repo,'--dir',scratch)
            desired = {p.name: p for p in paths}
            for old in Path(scratch).iterdir():
                if old.name not in desired or release.digest(old) != release.digest(desired[old.name]):
                    raise ValueError('existing release bytes conflict; use a new preview version')
            for path in paths:
                if not (Path(scratch)/path.name).exists():
                    gh('release','upload',tag,str(path),'--repo',repo)
    else:
        gh('release','create',tag,'--repo',repo,'--target',manifest['source'], '--draft', '--prerelease',
           '--title','BEAM/Erlang '+manifest['version'], '--notes','Experimental preview; see the release manifest and verification guide.', *map(str,paths))
    # Verify hosted bytes before exposing the release or producing site metadata.
    with tempfile.TemporaryDirectory() as scratch:
        gh('release','download',tag,'--repo',repo,'--dir',scratch)
        for path in paths:
            if release.digest(Path(scratch)/path.name) != release.digest(path):
                raise ValueError('hosted release asset mismatch')
    gh('release','edit',tag,'--repo',repo,'--draft=false','--prerelease')
    release.write(folder/'site-release.json', {'schema':1, 'version':manifest['version'],
        **release.version_metadata(manifest['version']), 'status':'preview',
        'manifest': {'url':f'https://github.com/{repo}/releases/download/{tag}/release-manifest.json',
                     'sha256':release.digest(folder/'release-manifest.json')},
        'downloads': [{'flavor':f,'url':f'https://github.com/{repo}/releases/download/{tag}/'+r['archive']['name'],
                       **r['archive']} for f,r in manifest['candidates'].items()]})

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('stage', choices=['prepare','offline','publish'])
    p.add_argument('--directory', required=True)
    p.add_argument('--run', required=True)
    p.add_argument('--digest', required=True)
    a = p.parse_args()
    if a.stage == 'prepare': prepare(a)
    elif a.stage == 'offline': offline_material(a)
    else: publish(a)

if __name__ == '__main__': main()
