#!/usr/bin/env python3
"""Locate the app; download a verified platform release only when missing. Never launches it."""
import hashlib
import io
import json
import os
from pathlib import Path, PurePosixPath
import platform
import re
import shutil
import stat
import tempfile
import urllib.request
from urllib.parse import urlsplit
import zipfile

REPOSITORY = 'yifan233/codex-tibo-reset-plugin'
ASSETS = {
    'Darwin-arm64': 'codex-tibo-reset-plugin-macos-arm64.zip',
    'Windows-x86_64': 'codex-tibo-reset-plugin-windows-x64.zip',
}

LEGACY_ASSETS = {
    'Darwin-arm64': 'tibo-rest-plugin-macos-arm64.zip',
    'Windows-x86_64': 'tibo-rest-plugin-windows-x64.zip',
}


def platform_key(system=None, machine=None):
    system = system or platform.system()
    machine = (machine or platform.machine()).lower()
    arch = {'amd64': 'x86_64', 'x64': 'x86_64', 'aarch64': 'arm64'}.get(machine, machine)
    return system + '-' + arch


def default_root():
    base = Path(os.environ.get('CODEX_HOME', str(Path.home() / '.codex'))) / 'apps'
    current = base / 'codex-tibo-reset-plugin'
    legacy = base / 'tibo-rest-plugin'
    if not current.exists() and any((legacy / name).is_file() for name in ['TiboReset.exe', 'Reset Countdown.app/Contents/MacOS/ResetCountdown']):
        return legacy
    return current


def validate_url(url):
    parsed = urlsplit(url)
    if (parsed.scheme != 'https' or parsed.username or parsed.password or parsed.port not in (None, 443)
            or parsed.hostname not in {'api.github.com', 'github.com', 'release-assets.githubusercontent.com', 'objects.githubusercontent.com'}):
        raise ValueError('Unexpected download host or protocol')


class ReleaseRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, request, response, code, message, headers, url):
        validate_url(url)
        return super().redirect_request(request, response, code, message, headers, url)


def read_url(url):
    validate_url(url)
    request = urllib.request.Request(url, headers={'User-Agent': 'codex-tibo-reset-plugin-skill', 'Accept-Encoding': 'identity'})
    opener = urllib.request.build_opener(ReleaseRedirect())
    with opener.open(request, timeout=60) as response:
        validate_url(response.geturl())
        data = bytearray()
        while True:
            chunk = response.read(32768)
            if not chunk:
                break
            data.extend(chunk)
            if len(data) > 16 * 1024 * 1024:
                raise ValueError('Unexpectedly large release response')
        length = response.headers.get('Content-Length')
        if length is not None and len(data) != int(length):
            raise ValueError('Incomplete download')
        return bytes(data)

def safe_extract(data, destination):
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        entries = archive.infolist()
        if len(entries) > 1024 or sum(item.file_size for item in entries) > 32 * 1024 * 1024:
            raise ValueError('Unexpectedly large extracted release')
        paths = set()
        for info in entries:
            name = PurePosixPath(info.filename)
            mode = info.external_attr >> 16
            parts = info.filename.rstrip('/').split('/')
            reserved = {'con', 'prn', 'aux', 'nul'} | {f'{p}{n}' for p in ('com', 'lpt') for n in range(1, 10)}
            if (name.is_absolute() or not parts or any(part in ('', '.', '..') or part.endswith((' ', '.'))
                    or part.split('.')[0].lower() in reserved for part in parts)
                    or any(ord(c) < 32 or c in '\\:*?"<>|' for c in info.filename)
                    or (stat.S_IFMT(mode) not in (0, stat.S_IFREG, stat.S_IFDIR))):
                raise ValueError('Unsafe release archive path')
            normalized = str(name).casefold()
            if normalized in paths:
                raise ValueError('Duplicate release archive path')
            paths.add(normalized)
        for info in entries:
            archive.extract(info, destination)

def ensure_app(root=None, key=None, reader=read_url):
    root = Path(root) if root is not None else default_root()
    key = key or platform_key()
    if key not in ASSETS:
        raise ValueError('Unsupported platform: ' + key + '; supported: macOS Apple Silicon / Windows x64')
    relative = Path('Reset Countdown.app/Contents/MacOS/ResetCountdown') if key.startswith('Darwin-') else Path('TiboReset.exe')
    executable = root / relative
    if any(path.is_symlink() for path in [root, executable, *(root / parent for parent in relative.parents)]):
        raise ValueError('Application path must not be a symbolic link')
    if executable.is_file():
        return {'status': 'found', 'executable': str(executable), 'installDir': str(root)}
    repo = REPOSITORY
    if not re.fullmatch(r'[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+', repo):
        raise ValueError('Invalid project repository')
    release = json.loads(reader('https://api.github.com/repos/' + repo + '/releases/latest'))
    assets = {item['name']: item for item in release.get('assets', [])}
    name = ASSETS[key] if ASSETS[key] in assets else LEGACY_ASSETS[key]
    if name not in assets:
        raise ValueError('Release is not published or the platform asset is missing: ' + name)
    item = assets[name]
    expected_prefix = 'https://github.com/' + repo + '/releases/download/'
    url = item['browser_download_url']
    if not url.startswith(expected_prefix):
        raise ValueError('Unexpected release asset source')
    data = reader(url)
    digest = item.get('digest') or ''
    if digest.startswith('sha256:'):
        expected = digest.split(':', 1)[1]
    else:
        checksums = assets.get('SHA256SUMS.txt')
        if not checksums or not checksums['browser_download_url'].startswith(expected_prefix):
            raise ValueError('Release has no SHA-256 checksum')
        entries = {}
        for line in reader(checksums['browser_download_url']).decode('utf-8').splitlines():
            parts = line.split()
            if len(parts) == 2:
                entries[parts[1].lstrip('*')] = parts[0]
        expected = entries.get(name, '')
    if not re.fullmatch('[0-9a-fA-F]{64}', expected) or hashlib.sha256(data).hexdigest() != expected.lower():
        raise ValueError('Release SHA-256 verification failed')
    root.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.tibo-download-', dir=root.parent) as temp:
        stage = Path(temp)
        safe_extract(data, stage)
        if not (stage / relative).is_file():
            raise ValueError('Release does not contain the expected executable')
        if key.startswith('Darwin-'):
            (stage / relative).chmod(0o755)
        root.mkdir(parents=True, exist_ok=True)
        if executable.exists():
            raise ValueError('Application appeared during install; retry to reuse it')
        if key.startswith('Darwin-'):
            destination = root / 'Reset Countdown.app'
            if destination.exists():
                raise ValueError('Incomplete application exists; move it to a backup and retry')
            shutil.move(str(stage / 'Reset Countdown.app'), str(destination))
        else:
            os.link(stage / relative, executable)
    return {'status': 'installed', 'executable': str(executable), 'installDir': str(root), 'release': release.get('tag_name')}


if __name__ == '__main__':
    try:
        print(json.dumps(ensure_app(), ensure_ascii=False))
    except Exception as exc:
        print(json.dumps({'status': 'error', 'message': str(exc)}, ensure_ascii=False))
        raise SystemExit(1)
