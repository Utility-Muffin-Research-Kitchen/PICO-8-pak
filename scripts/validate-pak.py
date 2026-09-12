#!/usr/bin/env python3
"""Use the public CONTENT-1 reference validator and merge model."""
import json
from pathlib import Path
import sys

contract, pak = map(Path, sys.argv[1:])
sys.path[:0] = [str(contract / 'contracts/leaf-content/scripts'), str(contract / 'contracts/leaf-services/scripts')]
import content_model
import minischema
manifest = json.loads((pak / 'pak.json').read_text())
schema = json.loads((contract / 'contracts/leaf-content/content-paks-v1.schema.json').read_text())
assert minischema.is_valid(manifest, schema)[0]
assert not content_model.validate_manifest(manifest, str(pak), {'install_lane': 'platform', 'source_id': 'primary'})
base = {'platform': 'mlp1', 'systems': [{'id': 'PICO8', 'rom_root': 'Roms/PICO8',
        'image_root': 'Images/PICO8', 'default_core': 'fake08', 'alternate_cores': []}],
        'cores': [{'id': 'fake08', 'type': 'retroarch', 'file_name': 'fake08_libretro.so'}]}
contributor = {'provider': 'mlp1/PICO8.pak', 'provides': manifest['provides']}
merged, diagnostics = content_model.merge(base, [contributor])
assert not diagnostics, diagnostics
assert merged['systems'][0]['default_core'] == 'fake08'
assert merged['systems'][0]['alternate_cores'] == ['pico8_native']
collision = {'provider': 'mlp1/Other.pak', 'provides': manifest['provides']}
_, diagnostics = content_model.merge(base, [contributor, collision])
assert diagnostics and any('collision' in item['reason'] for item in diagnostics), diagnostics
print('PASS: CONTENT-1 schema, package paths, default preserved, alternate merge, collision diagnostics')
