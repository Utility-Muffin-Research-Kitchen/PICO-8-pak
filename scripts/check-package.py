#!/usr/bin/env python3
"""Audit the package allowlist and the runtime/store metadata mirrors."""
import json
from pathlib import Path
import sys

root = Path(__file__).resolve().parent.parent
pak = Path(sys.argv[1])
expected = {'launch.sh', 'launch-cart.sh', 'pak.json', 'bin/pico8-launch',
            'bin/wget', 'bin/pico8-message', 'bin/pico8-import', 'LICENSE', 'README.md',
            'art/PICO8-photo.png', 'art/LICENSE-ASSETS.md',
            'art/PICO8-wordmark.png', 'art/WORDMARK-SOURCE.md',
            'art/PICO8-grid.png', 'art/GRID-ICON-SOURCE.md'}
actual = {p.relative_to(pak).as_posix() for p in pak.rglob('*') if p.is_file()}
assert actual == expected, (actual - expected, expected - actual)
assert not any(p.is_symlink() for p in pak.rglob('*')), 'Packages must be FAT32 safe'
manifest = json.loads((pak / 'pak.json').read_text())
assert manifest['icon'] == 'art/PICO8-photo.png'
assert manifest['content_art'] == {'schema': 1, 'systems': [
    {'id': 'PICO8', 'wordmark': 'art/PICO8-wordmark.png'}]}
store = json.loads((root / 'pakrat.json').read_text())['leaf']['packages'][0]
assert manifest['pak_version'] == store['version']
assert manifest['min_leaf_version'] == store['min_leaf_version']
core = manifest['provides']['cores'][0]
assert core['id'] == 'pico8_native' and core['path'] == 'launch-cart.sh'
assert 'requires_direct_drm' not in core
assert not manifest['provides'].get('systems')
assert manifest['provides']['system_extensions'] == [{'system_id': 'PICO8', 'add_alternate_cores': ['pico8_native']}]
for name in ('launch.sh', 'launch-cart.sh', 'bin/pico8-launch', 'bin/wget', 'bin/pico8-message', 'bin/pico8-import'):
    assert (pak / name).stat().st_mode & 0o111, name
print('PASS: package allowlist, FAT32 safety, metadata parity, native alternate only')
