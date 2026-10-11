"""Shared tooling regressions; INSTALLER_TOOLS_ROOT selects its checkout."""
import importlib.util
import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
TOOLS = Path(os.environ.get('INSTALLER_TOOLS_ROOT', ROOT.parent / 'cyd-web-installer'))
SCRIPT = TOOLS / 'tools/make_manifests.py'

@unittest.skipUnless(SCRIPT.exists(), 'Set INSTALLER_TOOLS_ROOT to shared tooling 1.2.0+')
class ChipDetectionTest(unittest.TestCase):
    def test_detect_and_reject_headers(self):
        spec=importlib.util.spec_from_file_location('shared_manifests',SCRIPT)
        tool=importlib.util.module_from_spec(spec);spec.loader.exec_module(tool)
        with tempfile.TemporaryDirectory() as directory:
            image=Path(directory)/'app.bin'
            for chip,expected in [(0,'ESP32'),(9,'ESP32-S3')]:
                image.write_bytes(b'\xe9'+bytes(11)+chip.to_bytes(2,'little')+bytes(2))
                self.assertEqual(tool.chip_family(image),expected)
            for header in [b'',bytes(16),b'\xe9'+bytes(11)+b'\xff\xff'+bytes(2)]:
                image.write_bytes(header)
                with self.assertRaises(SystemExit):tool.chip_family(image)

if __name__ == '__main__': unittest.main()
