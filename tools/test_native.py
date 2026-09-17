"""Native parity, latency and cancellation checks; --reference needs research deps."""
import argparse
import ctypes
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import threading
import time

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--reference', action='store_true')
parser.add_argument('--package', type=Path)
parser.add_argument('--stress-count', type=int, default=20)
parser.add_argument('--skip-corpus', action='store_true')
args = parser.parse_args()
package = args.package or ROOT / 'nvda-addon/synthDrivers/messengerExperimental'
spec = importlib.util.spec_from_file_location('messenger_native_test', str(package / 'engine.py'))
native = importlib.util.module_from_spec(spec)
spec.loader.exec_module(native)
arch = 'x64' if ctypes.sizeof(ctypes.c_void_p) == 8 else 'x86'
if args.package:
    paths = native.paths(package)
else:
    paths = (ROOT / ('build-' + arch) / 'MinSizeRel/messenger.dll',
             ROOT / 'assets/SPKMIC.TSR', ROOT / 'assets/calibration.json')
start = time.perf_counter()
engine = native.Engine(*paths)
report = {'architecture': arch, 'python': sys.version.split()[0],
          'init_ms': (time.perf_counter()-start)*1000, 'dll_sha256': hashlib.sha256(paths[0].read_bytes()).hexdigest(),
          'cases': [], 'cancellation_ms': []}
reference = None
if args.reference:
    sys.path.insert(0, str(ROOT / 'tools'))
    from experimental_synth import ExperimentalSynth
    import numpy as np
    reference = ExperimentalSynth(paths[1], paths[2])
texts = ['Hello.', 'This is my voice.', 'Sassy fish, zippy zebras.', 'B', 'D', 'E', 'T', 'X',
         'Help button. Search edit.', 'This is a longer sentence, with punctuation and a question?',
         '\x1bA0 ~ caf\u00e9', '1 2 3 45 678 1994', 'Open file dialog. Cancel button.',
         '4', 'edge', 'manager', 'G', 'J', '1', 'one', 'won', 'on', 'awn', 'Window.', 'Water.',
         'P', 'pee', 'pillow', 'feeling', 'eloquence', 'paper', 'computer']
for rate, pitch, volume in ([] if args.skip_corpus else [(5, 5, 100), (0, 0, 73), (17, 9, 100), (11, 4, 50)]):
    for text in texts:
        start = time.perf_counter()
        try:
            pcm = engine.synthesize(0, text, rate, pitch, volume)
        except Exception:
            print(arch, repr(text), rate, pitch, volume, flush=True)
            raise
        row = {'text': text, 'rate': rate, 'pitch': pitch, 'volume': volume,
               'ms': (time.perf_counter()-start)*1000, 'samples': len(pcm)//2, 'sha256': hashlib.sha256(pcm).hexdigest()}
        assert pcm
        if reference:
            start = time.perf_counter()
            old, stats = reference.synthesize(text, rate, pitch, volume, noise_scale=10**(-3/20),
                                              formant_setup=True, voiced_exponent=.2, p_transition=True,
                                              straight_e=True)
            row['reference_ms'] = (time.perf_counter()-start)*1000
            assert engine.frames() == bytes(reference.probe.dsp_bytes), row
            assert len(old) == len(pcm), row
            delta = np.frombuffer(old, '<i2').astype(int)-np.frombuffer(pcm, '<i2').astype(int)
            row['max_sample_delta'] = int(np.max(np.abs(delta)))
            assert row['max_sample_delta'] <= 1, row
        report['cases'].append(row)
assert engine.synthesize(0, 'Silent', volume=0) == b''
for delay in ([0, .0005, .002, .01, .03]*((args.stress_count+4)//5))[:args.stress_count]:
    old = len(report['cancellation_ms'])
    errors = []
    def synthesize():
        try:
            engine.synthesize(old, 'A fairly long sentence to cancel during pronunciation or rendering. ' * 5, rate=0)
        except Exception as error:
            errors.append(str(error))
    thread = threading.Thread(target=synthesize)
    thread.start()
    time.sleep(delay)
    start = time.perf_counter()
    engine.cancel(old+1)
    thread.join(2)
    elapsed = (time.perf_counter()-start)*1000
    assert not thread.is_alive() and not errors, errors
    assert engine.synthesize(old, 'Stale') == b''
    assert engine.synthesize(old+1, 'Hello.')
    report['cancellation_ms'].append(elapsed)
engine.close()
report['passed'] = True
report['reference_checked'] = bool(reference)
output = ROOT / 'dist' / ('native-' + ('stress-' if args.skip_corpus else 'test-') + arch + '-' + report['python'] + '.json')
output.parent.mkdir(exist_ok=True)
output.write_text(json.dumps(report, indent=2), encoding='utf-8')
print(json.dumps({'report': str(output), 'passed': True, 'cases': len(report['cases']),
                  'init_ms': report['init_ms'], 'maximum_cancel_ms': max(report['cancellation_ms']),
                  'maximum_sample_delta': max((r.get('max_sample_delta', 0) for r in report['cases']), default=0)}, indent=2))
