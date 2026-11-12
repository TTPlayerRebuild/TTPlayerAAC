"""Attach the exact ALAC backend source and GNU make bootstrap archive."""
import argparse
import hashlib
from pathlib import Path
import shutil
import urllib.request

MAKE_HASH = 'dd16fb1d67bfab79a72f5e8390735c49e3e8e70b4945a15ab1f81ddb78658fb3'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--destination', type=Path, required=True)
    a = parser.parse_args()
    source = Path((a.build / 'ffmpeg-source-path.txt').read_text())
    if (source / 'RELEASE').read_text().strip() != '9.0.2':
        raise ValueError('Expected complete FFmpeg 9.0.2 source')
    target = a.destination / 'third_party/ffmpeg'
    shutil.copytree(source, target, dirs_exist_ok=True,
        ignore=shutil.ignore_patterns('.git', '__pycache__', '*.obj', '*.o', '*.a', '*.lib', '*.exe', '*.dll', '*.pdb', '*.pyc', '*.log'))
    shutil.copyfile(Path(__file__).resolve().parents[1] / 'third_party/ffmpeg/ORIGIN.txt', target / 'ORIGIN.txt')
    archive = a.build / 'ffmpeg/make-4.4.1.tar.gz'
    if not archive.exists():
        urllib.request.urlretrieve('https://ftp.gnu.org/gnu/make/make-4.4.1.tar.gz', archive)
    if hashlib.sha256(archive.read_bytes()).hexdigest() != MAKE_HASH:
        raise ValueError('GNU make archive SHA256 mismatch')
    shutil.copyfile(archive, a.destination / 'third_party/make-4.4.1.tar.gz')


if __name__ == '__main__': main()
