#!/usr/bin/env python3
"""One-time, non-destructive directory migration; no content is deleted."""
from pathlib import Path
import subprocess

base = Path('/Users/zhouzhiyuan/飞秒质谱仪/软件开发')
for name in ('03-源码', '05-交付', '06-历史归档'):
    if (base / name).exists():
        raise RuntimeError('Destination already exists: ' + name)
old = base / '07-当前交付'
release = old / '20260903-交付版'
latest = release / 'Windows7/飞秒质谱工作站-Windows7-x64-Qt5.12-8GB-更新版20260904'
assert (latest / '飞秒质谱工作站.exe').is_file()
assert (release / 'macOS/飞秒质谱工作站.app').is_dir()
assert (base.parent / 'Windows源码.zip').is_file()
(base / 'QITestQt').rename(base / '03-源码')
(base / 'QITestQt').symlink_to('03-源码', target_is_directory=True)
(base / '99-历史归档').rename(base / '06-历史归档')
delivery = base / '05-交付'
delivery.mkdir()
(release / 'macOS').rename(delivery / 'Mac')
(release / 'macOS').symlink_to(delivery / 'Mac', target_is_directory=True)
latest.rename(delivery / 'Windows')
latest.symlink_to(delivery / 'Windows', target_is_directory=True)
old.rename(base / '06-历史归档/旧交付')
old.symlink_to('06-历史归档/旧交付', target_is_directory=True)
for name in ('Windows源码.zip', 'Windows源码.zip.sha256'):
    (base.parent / name).rename(delivery / name)
# Finder only: preserve old build-cache/venv absolute paths without duplicate UI entries.
for name in ('QITestQt', '07-当前交付'):
    subprocess.run(['chflags', '-h', 'hidden', str(base / name)], check=True)
print('Migration complete; original artifacts retained in 06-历史归档/旧交付')
