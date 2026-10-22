"""Install ephemeral candidate signing credentials; never invoked by PR builds."""
import argparse
import base64
import os
from pathlib import Path
import platform
import shutil
import subprocess
import catalog


def run(*args):
    # Avoid printing commands: certificate passwords and notary credentials are secrets.
    subprocess.run(list(map(str, args)), check=True, stdout=subprocess.DEVNULL)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('operation',choices=['prepare','cleanup']);p.add_argument('--target',required=True)
    a=p.parse_args();row=catalog.target(a.target)
    if row['os'] not in ('Windows','Darwin'):return
    root=Path(os.environ['RUNNER_TEMP'])/'beam-signing';keychain=root/'candidate.keychain-db'
    if a.operation=='cleanup':
        if platform.system()=='Darwin' and keychain.exists():run('security','delete-keychain',keychain)
        elif platform.system()=='Windows' and os.environ.get('BEAM_SIGN_IDENTITY'):
            run('certutil','-user','-delstore','My',os.environ['BEAM_SIGN_IDENTITY'])
        shutil.rmtree(root,ignore_errors=True);return
    root.mkdir(exist_ok=True);certificate=root/'certificate.p12'
    certificate.write_bytes(base64.b64decode(os.environ['BEAM_SIGN_CERTIFICATE'],validate=True))
    password=os.environ['BEAM_SIGN_PASSWORD']
    if platform.system()=='Darwin':
        key_password=os.environ['BEAM_KEYCHAIN_PASSWORD']
        run('security','create-keychain','-p',key_password,keychain)
        run('security','set-keychain-settings','-lut','21600',keychain)
        run('security','unlock-keychain','-p',key_password,keychain)
        run('security','import',certificate,'-k',keychain,'-P',password,'-T','/usr/bin/codesign')
        run('security','set-key-partition-list','-S','apple-tool:,apple:,codesign:','-s','-k',key_password,keychain)
        run('security','list-keychains','-d','user','-s',keychain)
        key=root/'AuthKey.p8';key.write_bytes(base64.b64decode(os.environ['BEAM_NOTARY_KEY'],validate=True))
        run('xcrun','notarytool','store-credentials',os.environ['BEAM_NOTARY_PROFILE'],'--key',key,'--key-id',os.environ['BEAM_NOTARY_KEY_ID'],'--issuer',os.environ['BEAM_NOTARY_ISSUER'],'--keychain',keychain)
    else:run('certutil','-user','-p',password,'-importPFX','My',certificate,'NoRoot')
    certificate.unlink()

if __name__=='__main__':main()
