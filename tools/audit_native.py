"""Audit PE architecture, Windows subsystem floor, imports and exports."""
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]
vswhere = Path('C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe')
vs = subprocess.check_output([str(vswhere), '-latest', '-products', '*', '-property', 'installationPath'], text=True).strip()
dumpbin = sorted(Path(vs).glob('VC/Tools/MSVC/*/bin/Hostx64/x64/dumpbin.exe'))[-1]
reports = {}
for arch, machine in [('x86', 0x14c), ('x64', 0x8664)]:
    path = ROOT / ('build-' + arch) / 'MinSizeRel/messenger.dll'
    data = path.read_bytes()
    offset = struct.unpack_from('<I', data, 0x3c)[0]
    assert struct.unpack_from('<H', data, offset+4)[0] == machine
    subsystem = struct.unpack_from('<HH', data, offset+24+48)
    assert subsystem == (6, 1), subsystem
    imports = subprocess.check_output([str(dumpbin), '/imports', str(path)], text=True)
    dlls = re.findall(r'^    ([\w.-]+\.dll)\s*$', imports, re.M | re.I)
    assert [d.lower() for d in dlls] == ['kernel32.dll'], dlls
    # Post-Windows-7 APIs must be resolved through thunks, never static imports.
    forbidden = ['CreateFile2', 'GetSystemTimePreciseAsFileTime', 'GetTempPath2W',
                 'WaitOnAddress', 'WakeByAddressSingle', 'SetThreadDescription',
                 'GetThreadInformation', 'GetSystemTimePreciseAsFileTime']
    assert not any(re.search(r'\b'+name+r'\b', imports) for name in forbidden)
    exports = subprocess.check_output([str(dumpbin), '/exports', str(path)], text=True)
    for name in ['msg_create', 'msg_destroy', 'msg_synthesize', 'msg_synthesize_options', 'msg_cancel', 'msg_frames', 'msg_pcm', 'msg_error', 'msg_api_version']:
        assert re.search(r'\b'+name+r'\b', exports), name
    reports[arch] = {'sha256': hashlib.sha256(data).hexdigest(), 'bytes': len(data),
                     'subsystem': list(subsystem), 'imports': dlls, 'runtime_dll_required': False}
(ROOT / 'dist').mkdir(exist_ok=True)
(ROOT / 'dist/native-import-audit.json').write_text(json.dumps(reports, indent=2))
print(json.dumps(reports, indent=2))
