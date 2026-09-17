"""Assemble the portable native Messenger beta and its corresponding source."""
import argparse
import hashlib
import json
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parents[1]
DRIVER_HASH = 'a2963b7025b67c577e3e3839342d9f49c39c7039d0ffcad36b1f066c816a1572'
VERSION = '0.4.0'
NAME = 'accent-messenger'
ADDON_ID = 'accentMessenger'  # Explicit migration; synth ID stays messengerExperimental.


def add(archive, path, name):
    add_bytes(archive, Path(path).read_bytes(), name)


def add_bytes(archive, content, name):
    info = zipfile.ZipInfo(name, (2026, 9, 16, 0, 0, 0))
    info.compress_type = zipfile.ZIP_DEFLATED
    archive.writestr(info, content)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fit', type=Path, default=ROOT / 'assets/calibration.json')
    parser.add_argument('--driver', type=Path, default=ROOT / 'assets/SPKMIC.TSR')
    args = parser.parse_args()
    manifest = (ROOT / 'nvda-addon/manifest.ini').read_text(encoding='utf-8')
    fields = dict(line.split(' = ', 1) for line in manifest.splitlines() if ' = ' in line)
    if fields.get('name') != ADDON_ID or fields.get('version') != VERSION:
        raise ValueError('Manifest must match the release identity and version')
    if hashlib.sha256(args.driver.read_bytes()).hexdigest() != DRIVER_HASH:
        raise ValueError('Unknown original driver')
    fit = json.loads(args.fit.read_text(encoding='utf-8'))
    model = fit['models']['constant']
    if not model['optimizer_success'] or len(model['parameters']) != 10:
        raise ValueError('Invalid calibration')
    calibration = {'warning': 'Experimental acoustic calibration, not recovered DSP equations.',
                   'source_sha256': fit['source_sha256'],
                   'models': {'constant': {'optimizer_success': True, 'parameters': model['parameters']}},
                   'fit_report_sha256': fit.get('fit_report_sha256') or hashlib.sha256(args.fit.read_bytes()).hexdigest()}
    dist = ROOT / 'dist'
    dist.mkdir(exist_ok=True)
    sources = dist / (NAME + '-' + VERSION + '-source.zip')
    # Explicit source roots exclude private memories and unrelated scratch files.
    with zipfile.ZipFile(sources, 'w') as z:
        for folder in ('native', 'nvda-addon', 'sapi'):
            for path in sorted((ROOT / folder).rglob('*')):
                if path.is_file() and '__pycache__' not in path.parts:
                    add(z, path, path.relative_to(ROOT).as_posix())
        for name in ('CMakeLists.txt', '.clang-format', '.gitignore', 'LICENSE', 'THIRD-PARTY-NOTICES.md', 'docs/native-release.md',
                     'docs/voice-controls.md', 'docs/sapi.md',
                     'README.md', 'tools/build_nvda.py', 'tools/build_sapi.py', 'tools/test_sapi.py', 'tools/audit_sapi.py', 'tools/prepare_native.py', 'tools/test_native.py',
                     'tools/test_native_nvda.py', 'tools/test_numwords.py', 'tools/audit_native.py'):
            add(z, ROOT / name, name)
        add(z, ROOT / '.build/unicorn-2.1.4.tar.gz', '.build/unicorn-2.1.4.tar.gz')
        add(z, args.driver, 'assets/SPKMIC.TSR')
        add_bytes(z, json.dumps(calibration, indent=2), 'assets/calibration.json')
    output = dist / (NAME + '-' + VERSION + '.nvda-addon')
    base = 'synthDrivers/messengerExperimental/'
    with zipfile.ZipFile(output, 'w', zipfile.ZIP_DEFLATED) as z:
        for path in sorted((ROOT / 'nvda-addon').rglob('*')):
            if path.is_file() and '__pycache__' not in path.parts:
                add(z, path, path.relative_to(ROOT / 'nvda-addon').as_posix())
        for arch in ('x86', 'x64'):
            add(z, ROOT / ('build-' + arch) / 'MinSizeRel/messenger.dll', base + arch + '/messenger.dll')
        add(z, args.driver, base + 'data/SPKMIC.TSR')
        add_bytes(z, json.dumps(calibration, indent=2), base + 'data/calibration.json')
        for name in ('LICENSE', 'THIRD-PARTY-NOTICES.md'):
            add(z, ROOT / name, name)
        for source, name in (
                ('NUMPY-LICENSE.txt', 'NUMPY-LICENSE.txt'),
                ('PCG-LICENSE.md', 'PCG-LICENSE.md'),
                ('YY-Thunks/LICENSE', 'YY-Thunks-LICENSE.txt')):
            add(z, ROOT / 'native/third_party' / source, 'licenses/' + name)
        # Corresponding source is a separate companion download.
    with zipfile.ZipFile(output) as z:
        if (z.testzip() or len(z.namelist()) != len(set(z.namelist()))
                or any(name.startswith('source/') for name in z.namelist())):
            raise RuntimeError('Invalid archive')
    report = {}
    for path in (output, sources):
        report[path.name] = {'bytes': path.stat().st_size, 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}
    (dist / (NAME + '-' + VERSION + '-checksums.json')).write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
