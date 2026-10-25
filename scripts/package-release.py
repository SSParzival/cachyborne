#!/usr/bin/env python3
"""Bundle tracked source and pinned submodules, excluding game data and native binaries."""
import hashlib
from pathlib import Path
import subprocess
import tarfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    version = (ROOT / 'VERSION').read_text().strip()
    destination = ROOT / 'dist'
    destination.mkdir(exist_ok=True)
    name = f'cachyborne-{version}'
    archive = destination / f'{name}-source.tar.gz'
    tracked = subprocess.check_output(['git', 'ls-files', '--recurse-submodules', '-z'], cwd=ROOT).split(b'\0')
    with tarfile.open(archive, 'w:gz', compresslevel=6) as output:
        for raw in tracked:
            if not raw:
                continue
            relative = Path(raw.decode())
            if relative.parts[0] in ('out', 'dist', 'user', 'logs', '.vscode'):
                raise RuntimeError(f'Unexpected private/generated tracked path: {relative}')
            output.add(ROOT / relative, arcname=str(Path(name) / relative), recursive=False)
    with archive.open('rb') as stream:
        digest = hashlib.file_digest(stream, 'sha256').hexdigest()
    checksum = destination / 'SHA256SUMS'
    checksum.write_text(f'{digest}  {archive.name}\n')
    print(archive)
    print(checksum)


if __name__ == '__main__':
    main()
