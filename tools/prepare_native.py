"""Verify and unpack the pinned native dependency (Python 3.13 build tool)."""
import hashlib
import json
from pathlib import Path
import tarfile
import urllib.request

root = Path(__file__).resolve().parents[1]
(root / '.build').mkdir(exist_ok=True)
pin = json.loads((root / 'native/dependencies.json').read_text())['unicorn']
archive = root / '.build/unicorn-2.1.4.tar.gz'
if not archive.is_file():
    urllib.request.urlretrieve(pin['url'], archive)
if hashlib.sha256(archive.read_bytes()).hexdigest() != pin['sha256']:
    raise ValueError('Unicorn source hash mismatch')
with tarfile.open(archive) as source:
    source.extractall(root / '.build', filter='data')
print('Verified Unicorn 2.1.4 source is ready.')
