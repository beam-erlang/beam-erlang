#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Exercise installed Android toolchain and native delivery rejection boundaries."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import tomllib


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk',type=Path,action='append',required=True)
    parser.add_argument('--cmake',default='cmake')
    parser.add_argument('--work',type=Path,required=True)
    parser.add_argument('--report',type=Path,required=True)
    args=parser.parse_args();args.work.mkdir(parents=True,exist_ok=True)
    result={'status':'running','tests':{},'targets':[]}
    try:
        for sdk in args.sdk:
            sdk=sdk.resolve(strict=True)
            spec=importlib.util.spec_from_file_location('android_delivery',sdk/'share/beam/tools/beam-android.py')
            delivery=importlib.util.module_from_spec(spec);spec.loader.exec_module(delivery)
            _,facts,paths=delivery.sdk(sdk,args.cmake);result['targets'].append(facts)
            abi=facts['android-abi'];key=abi+'-'+facts['flavor'];checks=[]
            def rejected(name,operation,message):
                try:operation()
                except (ValueError,RuntimeError) as error:
                    if message not in str(error):raise AssertionError((name,str(error))) from error
                else:raise AssertionError('Invalid input accepted: '+name)
                checks.append(name)
            with tempfile.TemporaryDirectory(prefix='android-input-',dir=args.work) as temporary:
                root=Path(temporary);native=paths['runtime'];original=native.read_bytes()
                rejected('wrong-elf-abi',lambda:delivery.elf_bytes(original,'x86_64' if abi=='arm64-v8a' else 'arm64-v8a',native.name),'ABI mismatch')
                rejected('wrong-soname',lambda:delivery.elf_bytes(original,abi,'different.so'),'SONAME')
                rejected('duplicate-native',lambda:delivery.closure([native,native],abi),'Conflicting')
                system=root/'liblog.so';system.write_bytes(original)
                rejected('system-library-shadow',lambda:delivery.closure([system],abi),'system library')
                header=struct.unpack_from('<16sHHIQQQIHHHHHH',original)
                load=next(header[5]+i*header[9] for i in range(header[10]) if struct.unpack_from('<I',original,header[5]+i*header[9])[0]==1)
                small=bytearray(original);struct.pack_into('<Q',small,load+48,4096)
                rejected('4k-only-elf',lambda:delivery.elf_bytes(small,abi,native.name),'16 KB')
                writable=bytearray(original);flags=struct.unpack_from('<I',writable,load+4)[0];struct.pack_into('<I',writable,load+4,flags|3)
                rejected('writable-executable-segment',lambda:delivery.elf_bytes(writable,abi,native.name),'Writable executable')
                paths_all=[paths['runtime'],paths['runtime.cxx']]+[paths["library." + c + ".shared"] for c in ('file','network','zlib')]
                closure=delivery.closure(paths_all,abi)
                required=next((name for name in closure if any(name in r['needed'] for r in closure.values())),None)
                if required:
                    rejected('missing-native-dependency',lambda:delivery.closure([p for p in paths_all if p.name!=required],abi),'dependency closure')
                source=root/'consumer';source.mkdir();(source/'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.25)\nproject(android_input LANGUAGES C)\nfind_package(BEAM CONFIG REQUIRED COMPONENTS Runtime)\n')
                toolchain=sdk/'lib/cmake/BEAM/BEAMToolchain.cmake'
                def configure(name,extra,env,message):
                    run=subprocess.run([args.cmake,'-S',str(source),'-B',str(root/name),'-G','Ninja','-DCMAKE_TOOLCHAIN_FILE='+str(toolchain),*extra],env=env,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=60)
                    if run.returncode==0 or message not in run.stdout:raise AssertionError((name,run.returncode,run.stdout))
                    checks.append(name)
                for name,value in [('ANDROID_ABI','x86_64' if abi=='arm64-v8a' else 'arm64-v8a'),('ANDROID_PLATFORM','android-27'),('ANDROID_STL','c++_static')]:
                    configure('mismatch-'+name,['-D'+name+'='+value],os.environ.copy(),name+' differs')
                env=os.environ.copy();env.pop('ANDROID_NDK_ROOT',None)
                configure('missing-ndk',[],env,'ANDROID_NDK_ROOT')
                ndk=root/'wrong-ndk';ndk.mkdir();(ndk/'source.properties').write_text('Pkg.Revision = 27.0.0\n');env['ANDROID_NDK_ROOT']=str(ndk)
                configure('wrong-ndk-revision',[],env,'NDK revision differs')
            result['tests'][key]=checks
        result['status']='passed'
    except BaseException as error:result.update(status='failed',error=str(error));raise
    finally:
        result['cleanup']='completed';args.report.parent.mkdir(parents=True,exist_ok=True);args.report.write_text(json.dumps(result,indent=2)+'\n')


if __name__=='__main__':main()
