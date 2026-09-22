#!/usr/bin/env python3
"""Fetch pinned upstream compatibility inputs. No user documents are collected."""
import hashlib
import pathlib
import urllib.request
import zipfile
import xml.etree.ElementTree as ET
ROOT = pathlib.Path(__file__).resolve().parents[1]
DEST = ROOT / 'tests' / 'fixtures' / 'external'
DEST.mkdir(parents=True, exist_ok=True)
FIXTURES = [
 ('skeleton.hwpx', 'https://raw.githubusercontent.com/airmang/python-hwpx/main/src/hwpx/data/Skeleton.hwpx', '9910eeb69abf7d9ad02edb9d616b589e79c553d7'),
 ('table.hwp', 'https://raw.githubusercontent.com/mete0r/pyhwp/master/tests/hwp5_tests/fixtures/table.hwp', 'fb1041009fc464aa6e4bc02f1b91c1bf66745bc4'),
 ('charshape.hwp', 'https://raw.githubusercontent.com/mete0r/pyhwp/master/tests/hwp5_tests/fixtures/charshape.hwp', '5f846b143e5c03fefe5be06b15d0d5cc80ca9c02'),
 ('headerfooter.hwp', 'https://raw.githubusercontent.com/mete0r/pyhwp/master/tests/hwp5_tests/fixtures/headerfooter.hwp', '60c0934f02736ba1d4031ca6d5f171e5b98450d2'),
 ('password.hwp', 'https://raw.githubusercontent.com/mete0r/pyhwp/master/tests/hwp5_tests/fixtures/password-12345.hwp', 'ade3f124b8f3a3d159ec44a9ef5da0292446af2c'),
]
for name, url, expected in FIXTURES:
    data = urllib.request.urlopen(url, timeout=30).read(8 * 1024 * 1024)
    actual = hashlib.sha1(b'blob ' + str(len(data)).encode() + b'\0' + data).hexdigest()
    if actual != expected:
        raise RuntimeError(f'Upstream content changed for {name}: {actual}')
    (DEST / name).write_bytes(data)
    print(name, len(data), hashlib.sha256(data).hexdigest())
with zipfile.ZipFile(DEST / 'skeleton.hwpx') as z:
    print('REFERENCE SECTION', z.read('Contents/section0.xml').decode())
    root = ET.fromstring(z.read('Contents/header.xml'))
    for ns, local in [('head','charPr'),('head','paraPr'),('head','borderFill'),('head','style')]:
        el = root.find('.//{http://www.hancom.co.kr/hwpml/2011/'+ns+'}'+local)
        if el is not None: print('REFERENCE', local, ET.tostring(el, encoding='unicode'))
