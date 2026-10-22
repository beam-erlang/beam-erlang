#!/usr/bin/env python3
"""Configure pinned OTP against the verified acquired candidate, never its producer."""
import argparse
from pathlib import Path
import subprocess
import release

p = argparse.ArgumentParser(description=__doc__)
for name in ['record', 'inputs-dir', 'work', 'report']:
    p.add_argument('--'+name, required=True)
a = p.parse_args()
r = release.read(a.record)
release.validate_candidate_version(r)
root = Path(a.inputs_dir).resolve()
work = Path(a.work).resolve()
work.mkdir(parents=True, exist_ok=True)
release.inputs(release.ROOT/'release/inputs.json', qualification=True)
release.verify_asset(Path(a.record).parent/r['archive']['name'], r['archive'])
prefix = release.extract(Path(a.record).parent/r['archive']['name'], work/'SDK ü')
if release.clean_revision(root/'otp') != r['inputs']['otp']['commit']:
    raise ValueError('OTP source pin mismatch')
for key, item in r['inputs']['qualification_assets'].items():
    release.verify_asset(root/key, {'name': key, 'sha256': item['sha256'], 'size': item['size']})
reference = release.extract(root/('reference-'+r['flavor']), work/'original')
postgres = release.extract(root/'postgres', work/'postgres')
ldap = release.extract(root/'ldap', work/'ldap')
ftp = release.extract(root/'ftp', work/'ftp')
# The fixture root layouts are owned by the existing OTP test helpers.
command = ['cmake', '-S', str(root/'otp'), '-B', str(work/'build'), '-G', 'Ninja',
           '-DCMAKE_BUILD_TYPE=RelWithDebInfo', '-DBUILD_TESTING=ON',
           '-DBEAM_SDK_ROOT='+str(prefix), '-DOTP_BEAM_FLAVOR='+r['flavor'],
           '-DOTP_BEAM_SDK_VERSION='+r['sdk']['identity'],
           '-DOTP_BEAM_SDK_BUILD_ID='+r['sdk']['build-id'],
           '-DOTP_REFERENCE_PREFIX='+str(reference),
           '-DOTP_REFERENCE_PROVENANCE='+str(root/('provenance-'+r['flavor'])),
           '-DOTP_REBAR3_FILE='+str(root/'rebar3'), '-DOTP_POSTGRES_ROOT='+str(postgres),
           '-DOTP_LDAP_ROOT='+str(ldap), '-DOTP_FTP_SERVER='+str(ftp/'usr/sbin/vsftpd'),
           '-DOTP_SHORT_TEST_ROOT=/tmp/otp-preview']
subprocess.run(command, check=True)
subprocess.run(['cmake', '--build', str(work/'build'), '--parallel', '2'], check=True)
subprocess.run(['python3', str(release.ROOT/'release/release.py'), 'ctest', '--kind', 'otp',
                '--build', str(work/'build'), '--record', a.record, '--report', a.report], check=True)
