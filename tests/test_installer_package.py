import importlib.util
import json
import struct
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('verify_installer', ROOT / 'tools/verify_installer.py')
verifier = importlib.util.module_from_spec(spec); spec.loader.exec_module(verifier)

class InstallerPackageTest(unittest.TestCase):
    def fixture(self, root):
        table = b''
        for label, kind, subtype, offset, size in [('nvs',1,2,0x9000,0x5000),('otadata',1,0,0xe000,0x2000),('app0',0,16,0x10000,0x500000)]:
            table += struct.pack('<HBBII16sI',0x50aa,kind,subtype,offset,size,label.encode(),0)
        parts=[]
        for name, offset, content in [('bootloader.bin',0,b'boot'),('partitions.bin',0x8000,table),('boot_app0.bin',0xe000,bytes(0x2000)),('firmware.bin',0x10000,b'\xe9'+bytes(11)+b'\x09\x00'+bytes(242))]:
            (root/name).write_bytes(content);parts.append({'path':name+'?v=test','offset':offset})
        return {'name':'ESP32S3-Weather','version':'test','builds':[{'chipFamily':'ESP32-S3','parts':parts}]}
    def run_case(self, mutate=None):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory); manifest=self.fixture(root)
            if mutate: mutate(manifest)
            (root/'manifest-esp32s3-7inch-public.json').write_text(json.dumps(manifest))
            return verifier.verify_installer(root)
    def test_valid_parts(self): self.assertEqual(self.run_case()['builds'][0]['chipFamily'],'ESP32-S3')
    def test_nvs_overlap_rejected(self):
        with self.assertRaises(ValueError): self.run_case(lambda m:m['builds'][0]['parts'][0].update(offset=0x9000))
    def test_wrong_application_slot_rejected(self):
        with self.assertRaises(ValueError): self.run_case(lambda m:m['builds'][0]['parts'][3].update(offset=0x510000))
    def test_wrong_chip_rejected(self):
        with self.assertRaises(ValueError): self.run_case(lambda m:m['builds'][0].update(chipFamily='ESP32'))
    def test_merged_image_rejected(self):
        with self.assertRaises(ValueError): self.run_case(lambda m:m['builds'][0]['parts'][3].update(path='firmware-merged.bin'))

if __name__ == '__main__': unittest.main()
