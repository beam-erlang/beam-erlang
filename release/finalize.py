"""Finalize platform signatures before packaging and acquired-byte acceptance."""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import release
sys.path.insert(0,str(release.ROOT/'ci'))
import catalog


def run(*args):subprocess.run(list(map(str,args)),check=True)


def finalize(target,prefix,build,identity=''):
    row=catalog.target(target)
    if row['os']=='Darwin':
        if not identity:raise ValueError('macOS candidates require BEAM_SIGN_IDENTITY in the signing keychain')
        files=[]
        for path in prefix.rglob('*'):
            if path.is_file() and not path.is_symlink():
                with path.open('rb') as f:magic=f.read(4)
                if magic in (b'\xcf\xfa\xed\xfe',b'\xfe\xed\xfa\xcf'):files.append(path)
        for path in sorted(files,key=lambda p:len(p.parts),reverse=True):
            command=['codesign','--force','--options','runtime','--timestamp','--sign',identity]
            if row['flavor']=='jit':command+=['--entitlements',str(release.ROOT/'release/macos-entitlements.plist')]
            run(*command,path)
        # Notarization precedes final payload hashing; published archive bytes are
        # assembled only after Apple accepts this signed installation.
        submission=build/'notarization.zip'
        run('ditto','-c','-k','--keepParent',prefix,submission)
        profile=os.environ.get('BEAM_NOTARY_PROFILE')
        if not profile:raise ValueError('macOS candidates require BEAM_NOTARY_PROFILE')
        run('xcrun','notarytool','submit',submission,'--keychain-profile',profile,'--keychain',Path(os.environ['RUNNER_TEMP'])/'beam-signing/candidate.keychain-db','--wait')
    elif row['os']=='Windows':
        if not identity:raise ValueError('Windows candidates require the signing certificate thumbprint in BEAM_SIGN_IDENTITY')
        timestamp=os.environ.get('BEAM_TIMESTAMP_URL')
        if not timestamp:raise ValueError('Windows candidates require BEAM_TIMESTAMP_URL')
        for path in sorted(prefix.rglob('*')):
            if path.suffix.lower() in ('.exe','.dll'):
                run('signtool','sign','/sha1',identity,'/fd','SHA256','/tr',timestamp,'/td','SHA256',path)
                run('signtool','verify','/pa',path)
    # iOS SDK frameworks are signed by the consuming application after copying;
    # signatures on that application are not part of the downloaded SDK identity.
    run('cmake','-DCMAKE_INSTALL_PREFIX='+str(prefix),'-DCMAKE_INSTALL_CONFIG_NAME=Release','-P',build/'InstallRuntimeArtifacts.cmake')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--target',required=True);p.add_argument('--prefix',type=Path,required=True);p.add_argument('--build',type=Path,required=True)
    a=p.parse_args();finalize(a.target,a.prefix.resolve(),a.build.resolve(),os.environ.get('BEAM_SIGN_IDENTITY',''))
