"""Prepare a runner; product compilation remains in CMake."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import urllib.request
import catalog


def run(*args, **kwargs):
    subprocess.run(list(map(str,args)), check=True, **kwargs)


def export(values):
    for key,value in values.items():
        os.environ[key]=value
    if os.environ.get('GITHUB_ENV'):
        with open(os.environ['GITHUB_ENV'],'a') as f:
            for key,value in values.items():
                if '\n' in value or '\r' in value: raise ValueError('multiline environment value')
                f.write(key+'='+value+'\n')


def msvc(arch):
    vswhere=Path(os.environ['ProgramFiles(x86)'])/'Microsoft Visual Studio/Installer/vswhere.exe'
    install=subprocess.check_output([str(vswhere),'-latest','-products','*','-requires','Microsoft.VisualStudio.Component.VC.Tools.x86.x64','-property','installationPath'],text=True).strip()
    vcvars=Path(install)/'VC/Auxiliary/Build/vcvarsall.bat'
    host='arm64' if platform.machine().lower()=='arm64' else 'x64'
    target='arm64' if arch=='arm64' else 'x64'
    selected=host if host==target else host+'_'+target
    output=subprocess.check_output(['cmd','/d','/c',str(catalog.ROOT/'ci/windows-env.cmd'),str(vcvars),selected],text=True)
    wanted={'path','include','lib','libpath','windowssdkdir','windowssdkversion','vctoolsinstalldir'}
    export({k:v for line in output.splitlines() if '=' in line for k,v in [line.split('=',1)] if k.lower() in wanted})


def msys(environment,lock):
    root=Path(os.environ['RUNNER_TEMP'])/'msys64'
    if not root.exists():
        filename=lock['archive'];base=f"https://github.com/msys2/msys2-installer/releases/download/{lock['release']}/{filename}"
        archive=root.parent/filename
        urllib.request.urlretrieve(base,archive)
        expected=urllib.request.urlopen(base+'.sha256').read().decode().split()[0]
        if hashlib.sha256(archive.read_bytes()).hexdigest()!=expected: raise ValueError('MSYS2 checksum mismatch')
        run('tar','-xf',archive,'-C',root.parent)
    triplet={'ucrt64':'ucrt-x86_64','clang64':'clang-x86_64','clangarm64':'clang-aarch64'}[environment]
    prefix='mingw-w64-'+triplet+'-'
    env=dict(os.environ,MSYSTEM=environment.upper(),CHERE_INVOKING='1')
    bash=root/'usr/bin/bash.exe'
    run(bash,'-lc','pacman --noconfirm -Syu',env=env)
    run(bash,'-lc','pacman --noconfirm -S --needed '+ ' '.join(prefix+p for p in ['toolchain','cmake','ninja','python','pkgconf'])+' perl',env=env)
    export({'PATH':str(root/environment/'bin')+os.pathsep+str(root/'usr/bin')+os.pathsep+os.environ['PATH'],'MSYSTEM':environment.upper()})


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--target',required=True);p.add_argument('--executor',action='store_true');a=p.parse_args()
    t=catalog.target(a.target);lock=json.loads((catalog.ROOT/'ci/toolchains.json').read_text())
    if platform.system()=='Windows':
        if t['compiler'] in ('msvc','ndk'):
            msvc(t['arch'] if t['family']=='windows' else 'amd64')
            if t['family']=='windows':
                # Native pkgconf wheels cover AMD64 and ARM64 without MSYS path translation.
                root=Path(os.environ['RUNNER_TEMP'])/'beam-pkgconf'
                run(sys.executable,'-m','venv',root)
                run(root/'Scripts/python.exe','-m','pip','install','--only-binary=:all:',
                    '--no-deps','pkgconf=='+lock['pkgconf']['python_package'])
                export({'PATH':str(root/'Scripts')+os.pathsep+os.environ['PATH']})
        else: msys(t['compiler'],lock['msys2'])
    elif platform.system()=='Linux' and not t['container']:
        run('sudo','apt-get','update')
        run('sudo','apt-get','install','-y','build-essential','cmake','ninja-build','perl','python3','libncurses-dev','unzip','pkg-config')
        if t['family']=='integration': run('bash',catalog.ROOT/'ci/prepare-otp-runner.sh')
    elif platform.system()=='Darwin':
        developer=os.environ.get('BEAM_DEVELOPER_DIR') or subprocess.check_output(['xcode-select','-p'],text=True).strip()
        if not Path(developer).is_dir():raise ValueError('selected Apple developer directory is missing')
        export({'DEVELOPER_DIR':developer})
        run('xcodebuild','-version')
        run('xcrun','--sdk','iphonesimulator' if t['family']=='ios' and t['executor']=='ios-simulator' else 'iphoneos' if t['family']=='ios' else 'macosx','--show-sdk-version')
        for name in ('cmake','ninja','pkgconf'):
            if not shutil.which(name): run('brew','install',name)
    if t['family']=='android':
        sdk=Path(os.environ.get('ANDROID_HOME') or os.environ.get('ANDROID_SDK_ROOT',''))
        managers=list(sdk.glob('cmdline-tools/*/bin/sdkmanager*'))
        if not managers: raise ValueError('Install Android command-line tools and set ANDROID_HOME')
        manager=next((m for m in managers if 'latest' in m.parts),managers[0])
        packages=['ndk;'+lock['android']['ndk'],'platform-tools','platforms;android-'+str(lock['android']['api']),'build-tools;'+lock['android']['build_tools']]
        if a.executor: packages+=['emulator']+lock['android']['system_images']
        subprocess.run([str(manager),'--licenses'],input='y\n'*100,text=True,check=True)
        run(manager,*packages)
        export({'ANDROID_NDK_ROOT':str(sdk/'ndk'/lock['android']['ndk']),'BEAM_ANDROID_TOOLS_BIN':str(manager.parent)})
    print(json.dumps({'target':t['id'],'host':platform.platform(),'machine':platform.machine()}))

if __name__=='__main__': main()
