"""Build only FFmpeg's ALAC decoder and avutil, out of tree, with MSVC/XP CRT.

The supplied source tree is never patched. Sources and GNU make are pinned;
identity changes force a clean backend rebuild, not reuse of unrelated codecs.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import tarfile
import urllib.request

VERSION = '9.0.2'
SHA256 = '8c3850283eb25fa026482078a04051e0be17347b09ef81a0849bec15a96e002e'
MAKE_SHA256 = 'dd16fb1d67bfab79a72f5e8390735c49e3e8e70b4945a15ab1f81ddb78658fb3'


def download(url, path, digest):
    if not path.exists() or hashlib.sha256(path.read_bytes()).hexdigest() != digest:
        urllib.request.urlretrieve(url, path)
    if hashlib.sha256(path.read_bytes()).hexdigest() != digest:
        raise RuntimeError('Dependency SHA256 mismatch: ' + str(path))


def extract(path, destination):
    with tarfile.open(path) as archive:
        archive.extractall(destination, filter='data')


def environment(vcvars, arch, folder):
    script = folder / ('env-' + arch + '.cmd')
    script.write_text('@echo off\ncall "' + str(vcvars) + '" ' + arch + ' >nul\nset\n', encoding='utf-8')
    result = subprocess.run(['cmd', '/d', '/c', str(script)], capture_output=True, check=True)
    env = {k.upper(): v for k, v in os.environ.items()}
    for line in result.stdout.decode('mbcs').splitlines():
        if '=' in line and not line.startswith('='):
            key, value = line.split('=', 1)
            env[key.upper()] = value
    return env


def run(command, cwd, env, log):
    with log.open('w', encoding='utf-8') as out:
        result = subprocess.run([str(x) for x in command], cwd=cwd, env=env, stdout=out, stderr=subprocess.STDOUT)
    if result.returncode:
        print('\n'.join(log.read_text(encoding='utf-8', errors='replace').splitlines()[-60:]))
        raise RuntimeError('Backend build failed; see ' + str(log))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('build', 'vs', 'ltl'):
        parser.add_argument('--' + name, type=Path, required=True)
    for name in ('source', 'bash', 'make-archive', 'make'):
        parser.add_argument('--' + name, type=Path)
    a = parser.parse_args()
    b = a.build.resolve()
    b.mkdir(parents=True, exist_ok=True)
    git = shutil.which('git')
    bash = a.bash or (Path(git).resolve().parent.parent / 'bin/bash.exe' if git else None)
    if not bash or not bash.is_file():
        raise RuntimeError('Git for Windows bash.exe is required; set TTP_FFMPEG_BASH.')
    source = a.source.resolve() if a.source else b / ('ffmpeg-' + VERSION)
    if not source.exists():
        archive = b / ('ffmpeg-' + VERSION + '.tar.xz')
        download('https://ffmpeg.org/releases/' + archive.name, archive, SHA256)
        extract(archive, b)
    if (source / 'RELEASE').read_text().strip() != VERSION:
        raise RuntimeError('Expected FFmpeg ' + VERSION)
    out = b / 'ffmpeg-alac'
    out.mkdir(exist_ok=True)
    if out == source:
        raise RuntimeError('ALAC backend must be built out of tree')
    vcvars = a.vs / 'VC/Auxiliary/Build/vcvarsall.bat'
    make = a.make.resolve() if a.make else b / 'make-4.4.1/WinRel/gnumake.exe'
    if not make.exists():
        archive = a.make_archive or b / 'make-4.4.1.tar.gz'
        download('https://ftp.gnu.org/gnu/make/make-4.4.1.tar.gz', archive, MAKE_SHA256)
        if not (b / 'make-4.4.1').exists():
            extract(archive, b)
        run(['cmd', '/d', '/c', 'build_w32.bat', '--without-guile'], b / 'make-4.4.1',
            environment(vcvars, 'x64', b), b / 'make-build.log')
    options = ['--toolchain=msvc', '--target-os=win32', '--arch=x86', '--disable-asm',
        '--disable-everything', '--disable-autodetect', '--disable-programs', '--disable-doc',
        '--disable-network', '--disable-avformat', '--disable-avdevice', '--disable-avfilter',
        '--disable-swscale', '--disable-swresample', '--enable-w32threads', '--disable-pthreads',
        '--disable-debug', '--disable-iconv', '--enable-small', '--enable-static', '--disable-shared',
        '--enable-decoder=alac', '--extra-cflags=-MT -O1 -Gy -Gw -D_WIN32_WINNT=0x0601',
        '--extra-ldflags=-NODEFAULTLIB:libcmt -DEFAULTLIB:msvcrt']
    digest = hashlib.sha256()
    for p in sorted(source.rglob('*')):
        if p.is_file() and (p.suffix in ('.c', '.h', '.S', '.asm', '.mak') or p.name in ('configure', 'Makefile')):
            digest.update(p.relative_to(source).as_posix().encode())
            digest.update(p.read_bytes())
    identity = {'source': str(source), 'source_sha256': digest.hexdigest(), 'options': options,
        'script': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(), 'vs': str(a.vs), 'ltl': str(a.ltl)}
    stamp = out / 'build-identity.json'
    libraries = [out / ('lib' + x) / (x + '.lib') for x in ('avcodec', 'avutil')]
    if stamp.exists() and json.loads(stamp.read_text()) == identity and all(x.exists() for x in libraries):
        return
    env = environment(vcvars, 'x86', b)
    ltl = a.ltl.resolve()
    env['INCLUDE'] = str(ltl / 'TargetPlatform/5.1.2600.0/header') + ';' + str(ltl / 'TargetPlatform/header') + ';' + env.get('INCLUDE', '')
    env['LIB'] = str(ltl / 'TargetPlatform/5.1.2600.0/lib/Win32') + ';' + env.get('LIB', '')
    env['MSYS2_ARG_CONV_EXCL'] = '*'
    env['PATH'] = str(bash.parent.parent / 'usr/bin') + ';' + str(bash.parent) + ';' + env['PATH']
    (out / 'Makefile').unlink(missing_ok=True)
    command = ' '.join(shlex.quote(x) for x in ['bash', (source / 'configure').as_posix()] + options)
    run([bash, '-c', command], out, env, b / 'alac-configure.log')
    config = (out / 'config.h').read_text()
    for flag in ('CONFIG_GPL', 'CONFIG_NONFREE', 'CONFIG_VERSION3', 'CONFIG_GPLV3'):
        if '#define ' + flag + ' 0' not in config:
            raise RuntimeError('Non-LGPL-2.1 backend profile: ' + flag)
    for path in (out / 'Makefile', out / 'ffbuild/config.mak'):
        path.write_text(re.sub(r'''(^|[ \t="'])/([a-zA-Z])/''', lambda m: m[1] + m[2].upper() + ':/',
            path.read_text(), flags=re.MULTILINE))
    shell = 'SHELL=' + (bash.parent.parent / 'usr/bin/sh.exe').as_posix()
    run([make, 'clean', shell], out, env, b / 'alac-clean.log')
    run([make, '-j4', 'CCDEP=', 'CXXDEP=', 'HOSTCCDEP=', shell], out, env, b / 'alac-build.log')
    if not all(p.exists() for p in libraries):
        raise RuntimeError('ALAC static libraries missing')
    stamp.write_text(json.dumps(identity, indent=2))


if __name__ == '__main__':
    main()
