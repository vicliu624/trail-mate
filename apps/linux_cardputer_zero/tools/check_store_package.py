#!/usr/bin/env python3
"""Validate the final Zero Store archive, including package-owned install paths."""
import argparse
from io import BytesIO
import os
from pathlib import Path, PurePosixPath
import subprocess
import tarfile


def dpkg(arguments):
    values = []
    for argument in map(str, arguments):
        if os.name == 'nt' and len(argument) > 2 and argument[1] == ':':
            argument = '/mnt/' + argument[0].lower() + argument[2:].replace('\\', '/')
        values.append(argument)
    return ['wsl', '-e', 'dpkg-deb', *values] if os.name == 'nt' else ['dpkg-deb', *values]


def related(name):
    return name == 'trailmate' or name.startswith(('trailmate-', 'trailmate_', 'trailmate.'))


def allowed_path(name):
    path = PurePosixPath(name)
    parts = path.parts
    if path.is_absolute() or '..' in parts:
        return False
    if len(parts) < 3 or parts[0] != 'usr':
        return False
    if parts[1] in ('bin', 'lib', 'libexec', 'share') and related(parts[2]):
        return True
    if parts[1:3] in (('lib', 'tmpfiles.d'), ('lib', 'sysusers.d')):
        return len(parts) == 4 and related(parts[3])
    if parts[1:4] == ('lib', 'systemd', 'user'):
        return len(parts) == 5 and related(parts[4])
    if parts[1:3] in (('share', 'applications'), ('share', 'doc'), ('share', 'licenses')):
        return len(parts) >= 4 and related(parts[3])
    if parts[1:4] == ('share', 'lintian', 'overrides'):
        return len(parts) == 5 and related(parts[4])
    if parts[1:4] == ('share', 'APPLaunch', 'applications'):
        return len(parts) == 5 and related(parts[4])
    if parts[1:5] in (('share', 'APPLaunch', 'share', 'images'),
                      ('share', 'APPLaunch', 'share', 'icons')):
        return len(parts) == 6 and related(parts[5])
    if parts[1:4] == ('share', 'icons', 'hicolor'):
        return len(parts) == 7 and parts[5] == 'apps' and related(parts[6])
    return False


def verify(package, version):
    package = package.resolve(strict=True)
    for field, expected in (('Package', 'trailmate'), ('Version', version), ('Architecture', 'arm64')):
        actual = subprocess.check_output(dpkg(['-f', package, field]), text=True, encoding='utf-8').strip()
        if actual != expected:
            raise ValueError(f'Incorrect {field}: {actual!r}, expected {expected!r}')
    payload = subprocess.check_output(dpkg(['--fsys-tarfile', package]))
    with tarfile.open(fileobj=BytesIO(payload), mode='r:') as archive:
        files = [member for member in archive if not member.isdir()]
    invalid = sorted(member.name for member in files if not allowed_path(member.name))
    if invalid:
        raise ValueError('Store package contains unowned install paths:\n' + '\n'.join(invalid))
    print(f'Validated {package.name}: Store identity, version, architecture and {len(files)} owned install paths')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument('--deb', type=Path)
    source.add_argument('--directory', type=Path)
    parser.add_argument('--version', required=True)
    args = parser.parse_args()
    candidates = [args.deb] if args.deb else list(args.directory.rglob('trailmate_*_arm64.deb'))
    if len(candidates) != 1:
        parser.error('Expected exactly one Zero Store application archive')
    verify(candidates[0], args.version)


if __name__ == '__main__':
    main()
