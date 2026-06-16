#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Copy installed UCRT64 tools and supply missing UTF-8 process manifests."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import xml.etree.ElementTree as ET

ASSEMBLY = 'urn:schemas-microsoft-com:asm.v1'
APPLICATION = 'urn:schemas-microsoft-com:asm.v3'
SETTINGS = 'http://schemas.microsoft.com/SMI/2019/WindowsSettings'
ET.register_namespace('', ASSEMBLY)
ET.register_namespace('asmv3', APPLICATION)


def sha256(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def code_hash(path):
    data = path.read_bytes()
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    count = struct.unpack_from('<H', data, pe + 6)[0]
    optional_size = struct.unpack_from('<H', data, pe + 20)[0]
    for index in range(count):
        offset = pe + 24 + optional_size + index * 40
        if data[offset:offset + 8].rstrip(b'\0') == b'.text':
            size, start = struct.unpack_from('<II', data, offset + 16)
            return hashlib.sha256(data[start:start + size]).hexdigest()
    raise ValueError(f'PE executable has no code section: {path}')


def manifest(mt, executable, output):
    result = subprocess.run([mt, f'-inputresource:{executable};1', f'-out:{output}'],
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            creationflags=subprocess.CREATE_NO_WINDOW)
    if result.returncode:
        return ET.Element(f'{{{ASSEMBLY}}}assembly', manifestVersion='1.0')
    return ET.fromstring(output.read_bytes())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True, help='Installed MSYS2 ucrt64 prefix')
    parser.add_argument('--destination', type=Path, required=True, help='Separate copy, also named ucrt64')
    parser.add_argument('--manifest-tool', type=Path, required=True, help='Windows SDK mt.exe')
    parser.add_argument('--verify-existing', action='store_true', help='Verify a previously prepared copy')
    args = parser.parse_args()
    source = args.source.resolve(strict=True)
    destination = args.destination.resolve()
    mt = args.manifest_tool.resolve(strict=True)
    if source.name != 'ucrt64' or destination.name != 'ucrt64':
        parser.error('Preserve the ucrt64 directory name for GCC sysroot discovery')
    if source == destination or source in destination.parents or destination in source.parents:
        parser.error('Source and destination must be separate directory trees')
    local = source.parent / 'var/lib/pacman/local'
    executables = set()
    packages = []
    for package in sorted(local.glob('mingw-w64-ucrt-x86_64-*')):
        if not package.name.startswith(('mingw-w64-ucrt-x86_64-gcc-', 'mingw-w64-ucrt-x86_64-binutils-')):
            continue
        files = package / 'files'
        if not files.is_file():
            continue
        packages.append(package.name)
        for entry in files.read_text(encoding='utf-8').splitlines():
            if entry.startswith('ucrt64/') and entry.endswith('.exe'):
                executables.add(entry.removeprefix('ucrt64/'))
    if not executables:
        parser.error('Installed pacman records do not identify GCC/binutils executables')
    if args.verify_existing:
        if not destination.is_dir():
            parser.error('The prepared toolchain does not exist')
    elif destination.exists():
        parser.error('Destination already exists; use --verify-existing or a fresh directory')
    else:
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copytree(source, destination)
    records = []
    with tempfile.TemporaryDirectory(prefix='ucrt64-manifests-', dir=destination.parent) as temporary:
        xml_path = Path(temporary) / 'process.manifest'
        for relative in sorted(executables):
            original, copied = source / relative, destination / relative
            document = manifest(mt, copied, xml_path)
            codepages = [node for node in document.iter() if node.tag == f'{{{SETTINGS}}}activeCodePage']
            if not codepages or any(node.text != 'UTF-8' for node in codepages):
                if args.verify_existing:
                    raise ValueError(f'Prepared executable lacks a UTF-8 manifest: {relative}')
                if codepages:
                    for node in codepages:
                        node.text = 'UTF-8'
                else:
                    application = document.find(f'{{{APPLICATION}}}application')
                    if application is None:
                        application = ET.SubElement(document, f'{{{APPLICATION}}}application')
                    settings = ET.SubElement(application, f'{{{APPLICATION}}}windowsSettings')
                    ET.SubElement(settings, f'{{{SETTINGS}}}activeCodePage').text = 'UTF-8'
                xml_path.write_bytes(ET.tostring(document, encoding='utf-8', xml_declaration=True))
                subprocess.run([mt, '-manifest', xml_path, f'-outputresource:{copied};1'],
                               check=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                               creationflags=subprocess.CREATE_NO_WINDOW)
            original_code = code_hash(original)
            if code_hash(copied) != original_code:
                raise ValueError(f'Executable code differs from the installed toolchain: {relative}')
            records.append({'path': relative, 'original-sha256': sha256(original),
                            'utf8-sha256': sha256(copied), 'code-sha256': original_code})
    receipt = {'source': str(source), 'destination': str(destination), 'packages': packages,
               'manifest-tool': str(mt), 'manifest-tool-sha256': sha256(mt), 'executables': records}
    output = destination.parent / 'utf8-toolchain.json'
    output.write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
    print(f'Native UCRT64 toolchain: {len(records)} UTF-8 executables; code sections unchanged. {output}')


if __name__ == '__main__':
    main()
