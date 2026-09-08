#!/usr/bin/env python3
"""Refresh an existing local runtime without changing its model or user data."""
import argparse
import hashlib
import shutil
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('package', type=Path)
args = parser.parse_args()
root = Path(__file__).resolve().parent.parent
package = args.package.resolve()
delivery = root.parent / '05-交付'
if delivery not in package.parents or not (package / 'Qt5Core.dll').is_file():
    raise ValueError('Expected an existing Qt5 delivery directory')
source = root / 'build-windows7-qt512/飞秒质谱工作站.exe'
target = package / source.name
backup = root / '.qa/update-20260904-backup' / package.name
backup.mkdir(parents=True, exist_ok=True)
if not (backup / target.name).exists():
    shutil.copy2(target, backup / target.name)
shutil.copy2(source, target.with_suffix('.exe.next'))
target.with_suffix('.exe.next').replace(target)
manifest = package / 'SHA256SUMS.txt'
lines = manifest.read_text(encoding='utf-8').splitlines()
digest = hashlib.sha256(target.read_bytes()).hexdigest()
lines = [digest + '  ' + line.split('  ', 1)[-1]
         if Path(line.split('  ', 1)[-1]) == Path(target.name) else line for line in lines]
manifest.write_text('\n'.join(lines) + '\n', encoding='utf-8')
print(str(target) + '\nSHA256 ' + digest)
