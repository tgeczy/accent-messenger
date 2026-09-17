"""Current source or packaged add-on with NVDA doubles; never drives live NVDA."""
import argparse
import hashlib
import importlib.util
import json
import shutil
from pathlib import Path
import struct
import sys
import tempfile
import threading
import time
import types
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--addon', type=Path, help='Test a packaged add-on instead of current source')
parser.add_argument('--scratch', type=Path, help='New directory for the isolated add-on copy')
parser.add_argument('--output', type=Path, help='JSON report path')
args = parser.parse_args()
ADDON = args.addon
SCRATCH = args.scratch or Path(tempfile.mkdtemp(prefix='Messenger native test ')) / 'addon'
SCRATCH.parent.mkdir(parents=True, exist_ok=True)
if ADDON:
    SCRATCH.mkdir(exist_ok=False)
    with zipfile.ZipFile(ADDON) as archive:
        for name in archive.namelist():
            if SCRATCH.resolve() not in (SCRATCH / name).resolve().parents:
                raise ValueError('Unsafe archive member')
        archive.extractall(SCRATCH)
else:
    shutil.copytree(ROOT / 'nvda-addon', SCRATCH,
                    ignore=shutil.ignore_patterns('__pycache__', '*.pyc'))
PACKAGE = SCRATCH / 'synthDrivers/messengerExperimental'
if not ADDON:
    arch = 'x64' if struct.calcsize('P') == 8 else 'x86'
    (PACKAGE / arch).mkdir()
    (PACKAGE / 'data').mkdir()
    shutil.copy2(ROOT / ('build-' + arch) / 'MinSizeRel/messenger.dll', PACKAGE / arch)
    for name in ('SPKMIC.TSR', 'calibration.json'):
        shutil.copy2(ROOT / 'assets' / name, PACKAGE / 'data')


class Event:
    def __init__(self):
        self.values = []
    def notify(self, **kwargs):
        self.values.append(kwargs)


class Player:
    def __init__(self, **kwargs):
        assert kwargs['samplesPerSec'] == 16000
        self.chunks = []
        self.paused = self.closed = False
    def feed(self, data, onDone=None):
        assert not self.paused and not self.closed
        self.chunks.append(data)
        time.sleep(.001)
        if onDone:
            onDone()
    def sync(self):
        pass
    def idle(self):
        pass
    def stop(self):
        self.paused = False
    def pause(self, switch):
        self.paused = switch
    def close(self):
        self.closed = True


class Index:
    def __init__(self, index):
        self.index = index


class Break:
    def __init__(self, duration):
        self.time = duration


class Base:
    VoiceSetting = RateSetting = PitchSetting = VolumeSetting = staticmethod(lambda: None)


DONE, INDEX, ERRORS = Event(), Event(), []


def module(name, **values):
    result = types.ModuleType(name)
    result.__dict__.update(values)
    sys.modules[name] = result


module('config', conf={'audio': {'outputDevice': 'default'}})
module('logHandler', log=types.SimpleNamespace(error=ERRORS.append, exception=ERRORS.append))
module('nvwave', WavePlayer=Player)
module('speech')
module('speech.commands', IndexCommand=Index, BreakCommand=Break)
module('autoSettingsUtils')
module('autoSettingsUtils.driverSetting', BooleanDriverSetting=lambda *args, **kwargs: (args, kwargs),
       NumericDriverSetting=lambda *args, **kwargs: (args, kwargs))
module('synthDriverHandler', SynthDriver=Base, VoiceInfo=lambda *args: args,
       synthDoneSpeaking=DONE, synthIndexReached=INDEX)
spec = importlib.util.spec_from_file_location('messenger_test_adapter', str(PACKAGE / '__init__.py'))
adapter = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = adapter
spec.loader.exec_module(adapter)


def wait_for(predicate, timeout=10):
    end = time.monotonic()+timeout
    while time.monotonic() < end:
        if predicate():
            return
        time.sleep(.005)
    raise AssertionError('Timed out')


class AdapterTests(unittest.TestCase):
    def setUp(self):
        DONE.values.clear()
        INDEX.values.clear()
        ERRORS.clear()
        self.synth = adapter.SynthDriver()

    def tearDown(self):
        self.synth.terminate()
        self.assertFalse(self.synth._thread.is_alive())
        self.assertTrue(self.synth._player.closed)
        self.assertIsNone(self.synth._engine._handle)

    def test_upgrade_identity_and_duplicate_cleanup(self):
        manifest = (SCRATCH / 'manifest.ini').read_text(encoding='utf-8')
        self.assertIn('name = accentMessenger\n', manifest)
        self.assertEqual(adapter.SynthDriver.name, 'messengerExperimental')
        self.assertFalse((SCRATCH / 'installTasks.py').exists())
        removed = []
        addons = [types.SimpleNamespace(
            manifest={'name': name, 'version': version},
            requestRemove=lambda key=(name, version): removed.append(key))
            for name, version in [('messengerExperimental', '0.3.3'),
                                  ('accent-messenger-experimental', '0.3.4'),
                                  ('unrelated', '0.3.4'),
                                  ('accentMessenger', '0.4.0')]]
        module('addonHandler', getAvailableAddons=lambda: iter(addons))
        module('globalVars', appArgs=types.SimpleNamespace(secure=False))
        module('globalPluginHandler', GlobalPlugin=type('Plugin', (), {'terminate': lambda self: None}))
        timers, dialogs, restarts = [], [], []
        def timer(delay, callback):
            timers.append(callback)
            return types.SimpleNamespace(Stop=lambda: None)
        module('wx', YES=1, NO=2, YES_NO=4, ICON_WARNING=8, OK=16, ICON_INFORMATION=32, CallLater=timer)
        answers = [2]
        def dialog(*args):
            dialogs.append(args)
            return answers.pop(0)
        module('gui', messageBox=dialog)
        module('core', restart=lambda: restarts.append(True))
        spec = importlib.util.spec_from_file_location('messenger_migration_test', str(SCRATCH / 'globalPlugins/accentMessengerMigration.py'))
        plugin_module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(plugin_module)
        plugin = plugin_module.GlobalPlugin()
        self.assertEqual(len(timers), 1)
        timers.pop()()
        self.assertEqual(removed, [])  # No never removes anything.
        plugin.terminate()
        answers.extend([1, 1])
        plugin = plugin_module.GlobalPlugin()
        timers.pop()()  # Prompt returns at the next startup.
        self.assertEqual(removed, [('messengerExperimental', '0.3.3'), ('accent-messenger-experimental', '0.3.4')])
        self.assertEqual(restarts, [True])
        plugin.terminate()
        # A removal failure identifies the failed package and does not restart.
        original_remove = addons[0].requestRemove
        def fail_remove():
            raise OSError('test removal failure')
        addons[0].requestRemove = fail_remove
        answers.extend([1, 16])
        plugin = plugin_module.GlobalPlugin()
        timers.pop()()
        self.assertIn('messengerExperimental', dialogs[-1][0])
        self.assertEqual(restarts, [True])
        self.assertEqual(removed[-1], ('accent-messenger-experimental', '0.3.4'))
        self.assertEqual(len(ERRORS), 1)
        ERRORS.clear()
        plugin.terminate()
        addons[0].requestRemove = original_remove
        # A pending timer cannot prompt after the plugin is terminated.
        plugin = plugin_module.GlobalPlugin()
        pending_callback = timers.pop()
        plugin.terminate()
        pending_callback()
        self.assertEqual(answers, [])
        addons[:] = [addons[-1]]  # Successful migration: dormant plugin.
        plugin = plugin_module.GlobalPlugin()
        timers.pop()()
        self.assertEqual(len(dialogs), 5)
        plugin.terminate()
        sys.modules['globalVars'].appArgs.secure = True
        plugin = plugin_module.GlobalPlugin()
        self.assertEqual(timers, [])
        plugin.terminate()

    def test_indexes_and_completion(self):
        self.synth.speak(['Hello.', Index(12), Break(30), 'This is my voice.', Index(13)])
        wait_for(lambda: DONE.values)
        self.assertEqual([v['index'] for v in INDEX.values], [12, 13])
        self.assertGreater(sum(map(len, self.synth._player.chunks)), 16000)
        self.assertEqual(ERRORS, [])

    def observe_render(self):
        ready = threading.Event()
        original = self.synth._synthesize
        def observe(*args):
            pcm = original(*args)
            ready.set()
            return pcm
        self.synth._synthesize = observe
        return ready

    def test_pause_resume(self):
        ready = self.observe_render()
        self.synth.pause(True)
        self.synth.speak(['Hello.', Index(20)])
        self.assertTrue(ready.wait(5))
        self.assertEqual(self.synth._player.chunks, [])
        self.assertEqual(INDEX.values, [])
        self.synth.pause(False)
        wait_for(lambda: DONE.values)
        self.assertEqual([v['index'] for v in INDEX.values], [20])

    def test_cancel_paused_audio_preserves_engine(self):
        ready = self.observe_render()
        handle = self.synth._engine._handle
        self.synth.pause(True)
        self.synth.speak(['Old speech.', Index(99)])
        self.assertTrue(ready.wait(5))
        self.synth.cancel()
        self.synth.speak(['New speech.', Index(30)])
        wait_for(lambda: DONE.values)
        self.assertEqual([v['index'] for v in INDEX.values], [30])
        self.assertEqual(self.synth._engine._handle, handle)
        self.assertEqual(ERRORS, [])

    def test_rapid_navigation(self):
        handle = self.synth._engine._handle
        for i in range(60):
            self.synth.cancel()
            self.synth.speak(['This is a lengthy stale navigation label.', Index(100+i)])
            time.sleep(.005)
        self.synth.cancel()
        self.synth._player.chunks.clear()
        start = time.perf_counter()
        self.synth.speak(['Final button.', Index(999)])
        wait_for(lambda: self.synth._player.chunks)
        elapsed = (time.perf_counter()-start)*1000
        wait_for(lambda: DONE.values)
        self.assertEqual([v['index'] for v in INDEX.values], [999])
        self.assertEqual(self.synth._engine._handle, handle)
        self.assertLess(elapsed, 1000)
        self.assertEqual(ERRORS, [])
        print('Latest label first feed: %.1f ms (audio double)' % elapsed)

    def test_error_releases_queue_and_recovers(self):
        original = self.synth._synthesize
        def fail(*args):
            raise RuntimeError('Injected failure')
        self.synth._synthesize = fail
        self.synth.speak(['Failed.', Index(99), 'Discard this.'])
        wait_for(lambda: DONE.values)
        self.assertEqual(INDEX.values, [])
        self.assertEqual(len(ERRORS), 1)
        self.synth._synthesize = original
        self.synth.speak(['Recovered.', Index(50)])
        wait_for(lambda: len(DONE.values) == 2)
        self.assertEqual([v['index'] for v in INDEX.values], [50])

    def test_chunking_and_settings(self):
        captured = []
        def capture(epoch, text, settings):
            captured.append((text, settings))
            return b''
        self.synth._synthesize = capture
        self.synth._set_rate(100)
        self.synth._set_pitch(100)
        self.synth._set_volume(0)
        text = 'long screen reader text ' * 30
        self.synth.speak([text])
        wait_for(lambda: DONE.values)
        self.assertEqual(' '.join(t for t, _ in captured), text.strip())
        self.assertTrue(all(len(t) <= 160 for t, _ in captured))
        self.assertTrue(all(s == {'rate': 17, 'pitch': 9, 'volume': 0, 'voice': 5,
                                 'intonation': 0, 'spacing': 0, 'output_gain': 200} for _, s in captured))

    def test_voice_options_snapshot_and_defaults(self):
        self.assertEqual(self.synth._get_voice(), 'v5')
        self.assertEqual(self.synth._get_inflection(), 100)
        self.assertEqual(self.synth._get_wordSpacing(), 0)
        self.assertEqual(len(self.synth._get_availableVoices()), 10)
        captured = []
        self.synth._synthesize = lambda epoch, text, settings: captured.append(settings) or b''
        for position, mode in zip((0, 25, 50, 75, 100), (1, 2, 3, 4, 0)):
            self.synth._set_voice('v3')
            self.synth._set_inflection(position)
            self.synth._set_wordSpacing(2)
            self.synth.speak(['This is my voice.'])
        self.synth._set_voice('v5')
        self.synth._set_wordSpacing(0)
        wait_for(lambda: len(DONE.values) == 5)
        self.assertEqual([s['intonation'] for s in captured], [1, 2, 3, 4, 0])
        self.assertTrue(all(s['voice'] == 3 and s['spacing'] == 2 for s in captured))
        self.assertEqual(ERRORS, [])

    def test_old_nvda_audio_configuration(self):
        conf = sys.modules['config'].conf
        try:
            sys.modules['config'].conf = {'speech': {'outputDevice': 'default'}}
            adapter.create_player().close()
        finally:
            sys.modules['config'].conf = conf

    def test_number_modes_before_chunking(self):
        captured = []
        self.synth._synthesize = lambda epoch, text, settings: captured.append(text) or b''
        self.synth.speak(['100', Index(70), '123 and 1,234.50'])
        wait_for(lambda: DONE.values)
        self.assertEqual(captured, ['one hundred', 'one hundred twenty three and one thousand two hundred thirty four point five zero'])
        self.assertEqual([v['index'] for v in INDEX.values], [70])
        captured.clear()
        self.synth._set_readDigits(True)
        self.synth.speak(['100', Index(71), '123'])
        wait_for(lambda: len(DONE.values) == 2)
        self.assertEqual(captured, ['one zero zero', 'one two three'])
        self.synth._set_readDigits(False)
        captured.clear()
        self.synth.speak(['100 ' * 40])
        wait_for(lambda: len(DONE.values) == 3)
        self.assertEqual(' '.join(captured), ('one hundred ' * 40).strip())
        self.assertTrue(all(len(text) <= 160 for text in captured))
        self.assertEqual(ERRORS, [])

    def test_numbers_reach_native_as_english_words(self):
        expected_engine = adapter.Engine(*adapter.paths())
        try:
            for index, (digits, words) in enumerate(((False, 'one hundred'), (True, 'one zero zero')), 1):
                self.synth._player.chunks.clear()
                self.synth._set_readDigits(digits)
                expected = expected_engine.synthesize(0, words, output_gain=200)
                self.synth.speak(['100'])
                wait_for(lambda: len(DONE.values) == index)
                self.assertEqual(b''.join(self.synth._player.chunks), expected + b'\0\0')
            self.assertEqual(ERRORS, [])
        finally:
            expected_engine.close()

    def test_cancel_interrupts_blocked_audio_feed(self):
        entered, release, stopped = threading.Event(), threading.Event(), threading.Event()
        player = self.synth._player
        original_feed, original_stop = player.feed, player.stop
        calls = []

        def blocked_feed(data, onDone=None):
            entered.set()
            release.wait(3)
            # Simulate the remaining race: feed accepted just after stop.
            player.chunks.append(data)

        def stop():
            calls.append('stop')
            player.chunks.clear()
            original_stop()
            stopped.set()
            release.set()

        player.feed, player.stop = blocked_feed, stop
        self.synth.speak(['Old speech.', Index(99)])
        self.assertTrue(entered.wait(5))
        cancelled = threading.Event()
        def cancel():
            self.synth.cancel()
            cancelled.set()
        thread = threading.Thread(target=cancel)
        thread.start()
        try:
            self.assertTrue(stopped.wait(.25), 'Cancel cannot reach stop while feed blocks')
            self.assertTrue(cancelled.wait(.25))
        finally:
            release.set()
            thread.join(5)
        wait_for(lambda: len(calls) >= 2)
        self.assertEqual(player.chunks, [])
        player.feed = original_feed
        self.synth.speak(['New speech.', Index(88)])
        wait_for(lambda: DONE.values)
        self.assertEqual([v['index'] for v in INDEX.values], [88])

    def test_index_callback_does_not_drain_before_next_text(self):
        player = self.synth._player
        callbacks, rendered = [], []
        def feed(data, onDone=None):
            player.chunks.append(data)
            if onDone:
                callbacks.append(onDone)
        def synthesize(epoch, text, settings):
            rendered.append(text)
            return b'\1\0' * 320
        def forbidden_sync():
            raise AssertionError('Index blocks the synthesis worker')
        player.feed, player.sync = feed, forbidden_sync
        self.synth._synthesize = synthesize
        self.synth.speak(['First.', Index(71), 'Second.', Index(72)])
        wait_for(lambda: len(callbacks) == 3)
        self.assertEqual(rendered, ['First.', 'Second.'])
        self.assertEqual(INDEX.values, [])
        callbacks[0]()
        self.assertEqual([v['index'] for v in INDEX.values], [71])
        self.synth.cancel()
        # Backends may deliver callbacks already in flight after stop.
        for callback in callbacks[1:]:
            callback()
        self.assertEqual([v['index'] for v in INDEX.values], [71])
        self.assertEqual(DONE.values, [])

    def test_cancel_during_idle_discards_old_completion(self):
        entered, release = threading.Event(), threading.Event()
        callbacks = []
        player = self.synth._player
        original_feed, original_idle, original_stop = player.feed, player.idle, player.stop
        def feed(data, onDone=None):
            player.chunks.append(data)
            if onDone:
                callbacks.append(onDone)
        def idle():
            entered.set()
            release.wait(3)
        def stop():
            original_stop()
            release.set()
        player.feed, player.idle, player.stop = feed, idle, stop
        self.synth.speak(['Old.'])
        self.assertTrue(entered.wait(5))
        self.synth.cancel()
        for callback in callbacks:
            callback()
        self.assertEqual(DONE.values, [])
        player.feed, player.idle = original_feed, original_idle
        self.synth.speak(['Fresh.'])
        wait_for(lambda: DONE.values)
        self.assertEqual(len(DONE.values), 1)


if __name__ == '__main__':
    result = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(AdapterTests))
    report = {'addon_sha256': hashlib.sha256(ADDON.read_bytes()).hexdigest() if ADDON else None,
              'tests_run': result.testsRun, 'failures': len(result.failures), 'errors': len(result.errors),
              'successful': result.wasSuccessful(), 'live_nvda_tested': False,
              'scope': 'Packaged add-on' if ADDON else 'Current source and built DLL; NVDA API/audio doubles. No audio played.'}
    name = 'native-nvda-test-report-%s-%s.json' % (struct.calcsize('P')*8, sys.version.split()[0])
    output = args.output or ROOT / 'dist' / name
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps(report, indent=2))
    sys.exit(0 if result.wasSuccessful() else 1)
