#!/usr/bin/env python3
"""Package only the public environment using the shared installer process."""
import argparse
import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path
from verify_installer import verify_installer


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tools-root', required=True, type=Path,
                        help='checkout of cyd-web-installer 1.2.0+ with firmware chip detection')
    parser.add_argument('--project', default='.', type=Path)
    args = parser.parse_args()
    root = args.project.resolve()
    build = root / '.pio/build/esp32s3-7inch-public'
    identity = json.loads((build / 'installer_build.json').read_text())
    config = (root / 'include/config.h').read_text()
    version = re.search(r'#define\s+FIRMWARE_VERSION\s+"([^"]+)"', config).group(1)
    project = re.search(r'PROJECT_NAME\s*=\s*"([^"]+)"', config).group(1)
    digest = hashlib.sha256((build / 'firmware.bin').read_bytes()).hexdigest()
    if not identity['publicBuild'] or identity['firmwareVersion'] != version or identity['project'] != project or identity['applicationSha256'] != digest:
        sys.exit('error: public build missing/stale; rebuild esp32s3-7inch-public')
    script = args.tools_root.resolve() / 'tools/make_manifests.py'
    subprocess.run([sys.executable, str(script), '--project', str(root)], check=True, cwd=root)
    site = root / '_site'
    manifest = verify_installer(site)
    builds = manifest['builds']
    if len(builds) != 1 or builds[0]['chipFamily'] != 'ESP32-S3' or len(builds[0]['parts']) != 4:
        sys.exit('error: installer must contain four separate ESP32-S3 flash parts')
    lines = []
    for path in sorted(site.rglob('*')):
        if path.is_file() and path.name != 'SHA256SUMS':
            lines.append(hashlib.sha256(path.read_bytes()).hexdigest() + '  ' + path.relative_to(site).as_posix())
    (site / 'SHA256SUMS').write_text('\n'.join(lines) + '\n')
    print('Public ESP32-S3 installer preview and checksums: ' + str(site))

if __name__ == '__main__':
    main()
