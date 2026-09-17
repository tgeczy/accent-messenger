"""Check SAPI binaries for architecture, Win7 floor and external CRT imports."""
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    vswhere = Path('C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe')
    vs = subprocess.check_output([str(vswhere), '-latest', '-products', '*',
                                  '-property', 'installationPath'], text=True).strip()
    dumpbin = sorted(Path(vs).glob('VC/Tools/MSVC/*/bin/Hostx64/x64/dumpbin.exe'))[-1]
    allowed = {'kernel32.dll', 'user32.dll', 'ole32.dll', 'oleaut32.dll',
               'advapi32.dll', 'shell32.dll', 'bcrypt.dll', 'normaliz.dll'}
    forbidden = ('CreateFile2', 'GetSystemTimePreciseAsFileTime', 'GetTempPath2W',
                 'WaitOnAddress', 'WakeByAddressSingle', 'SetThreadDescription',
                 'GetThreadInformation', 'GetDpiForWindow', 'GetDpiForSystem')
    reports = {}
    for arch, machine in [('x86', 0x14c), ('x64', 0x8664)]:
        for name in ('messenger_sapi.dll', 'messenger_settings.exe'):
            path = ROOT / ('build-' + arch) / 'sapi/MinSizeRel' / name
            data = path.read_bytes()
            offset = struct.unpack_from('<I', data, 0x3c)[0]
            assert struct.unpack_from('<H', data, offset + 4)[0] == machine
            subsystem = struct.unpack_from('<HH', data, offset + 24 + 48)
            assert subsystem == (6, 1), (path, subsystem)
            imports = subprocess.check_output([str(dumpbin), '/imports', str(path)], text=True)
            dlls = re.findall(r'^    ([\w.-]+\.dll)\s*$', imports, re.M | re.I)
            assert dlls and {d.lower() for d in dlls} <= allowed, (path, dlls)
            assert not any(re.search(r'\b' + api + r'\b', imports) for api in forbidden)
            if name.endswith('.dll'):
                exports = subprocess.check_output([str(dumpbin), '/exports', str(path)], text=True)
                for symbol in ('DllGetClassObject', 'DllCanUnloadNow',
                               'DllRegisterServer', 'DllUnregisterServer'):
                    assert re.search(r'\b' + symbol + r'\b', exports), (path, symbol)
            reports[arch + '/' + name] = {
                'sha256': hashlib.sha256(data).hexdigest(), 'bytes': len(data),
                'subsystem': list(subsystem), 'imports': dlls,
                'runtime_dll_required': False,
            }
    (ROOT / 'dist/sapi-import-audit.json').write_text(json.dumps(reports, indent=2) + '\n')
    print(json.dumps(reports, indent=2))


if __name__ == '__main__':
    main()
