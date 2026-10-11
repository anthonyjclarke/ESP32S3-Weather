#!/usr/bin/env python3
"""Check packaged weather flash parts against their actual partition table."""
import json
import struct
from pathlib import Path
from urllib.parse import urlsplit


def verify_installer(site):
    manifest = json.loads((site / 'manifest-esp32s3-7inch-public.json').read_text())
    build = manifest['builds'][0]
    if len(manifest['builds']) != 1 or build['chipFamily'] != 'ESP32-S3':
        raise ValueError('Wrong installer chip family')
    parts = build['parts']
    expected = ['bootloader.bin', 'partitions.bin', 'boot_app0.bin', 'firmware.bin']
    paths = [(site / urlsplit(part['path']).path).resolve() for part in parts]
    if len(parts) != 4 or [path.name for path in paths] != expected:
        raise ValueError('Expected four separate flash images')
    if any(not path.is_relative_to(site.resolve()) for path in paths):
        raise ValueError('Flash image outside installer site')
    header = paths[3].read_bytes()[:16]
    if len(header) < 16 or header[0] != 0xe9 or int.from_bytes(header[12:14], 'little') != 9:
        raise ValueError('Application header is not ESP32-S3')
    table = paths[1].read_bytes()
    partitions = {}
    for pos in range(0, len(table) - 31, 32):
        if table[pos:pos+2] != b'\xaa\x50': break
        offset, size = struct.unpack_from('<II', table, pos+4)
        label = table[pos+12:pos+28].split(b'\0',1)[0].decode()
        partitions[label] = (offset, size)
    nvs_offset, nvs_size = partitions['nvs']
    app_offset, app_size = partitions['app0']
    ota_offset, ota_size = partitions['otadata']
    if parts[3]['offset'] != app_offset or paths[3].stat().st_size > app_size:
        raise ValueError('Application does not fit app0')
    if parts[2]['offset'] != ota_offset or paths[2].stat().st_size != ota_size:
        raise ValueError('OTA reset image does not match otadata')
    ends = []
    for part, path in zip(parts, paths):
        start = part['offset']; end = start + path.stat().st_size
        if not (end <= nvs_offset or start >= nvs_offset+nvs_size):
            raise ValueError('A flash image overlaps NVS')
        if start < 0 or end > 16*1024*1024:
            raise ValueError('A flash image lies outside 16 MB flash')
        if ends and start < ends[-1]: raise ValueError('Flash images overlap')
        ends.append(end)
    return manifest
