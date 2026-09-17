"""NVDA speech queue for the native experimental Messenger reconstruction."""
from collections import OrderedDict
import queue
import threading

from .engine import Engine, paths
from .audio import create_player
from .numwords import normalise
from autoSettingsUtils.driverSetting import BooleanDriverSetting, NumericDriverSetting
from logHandler import log
from speech.commands import BreakCommand, IndexCommand
from synthDriverHandler import SynthDriver as BaseSynthDriver, VoiceInfo, synthDoneSpeaking, synthIndexReached


def _chunks(text, maximum=160):
    text = text.strip()
    while text:
        if len(text) <= maximum:
            yield text
            return
        end = text.rfind(' ', 0, maximum + 1)
        if end < maximum // 3:
            end = maximum
        yield text[:end]
        text = text[end:].lstrip()


class SynthDriver(BaseSynthDriver):
    name = 'messengerExperimental'
    description = 'Accent Messenger'
    supportedSettings = (BaseSynthDriver.VoiceSetting(), BaseSynthDriver.RateSetting(),
                         BaseSynthDriver.PitchSetting(), BaseSynthDriver.VolumeSetting(),
                         NumericDriverSetting('inflection', '&Inflection', defaultVal=100,
                                              minStep=25, normalStep=25, largeStep=25,
                                              availableInSettingsRing=True),
                         NumericDriverSetting('wordSpacing', '&Word spacing', defaultVal=0,
                                              minVal=0, maxVal=9, normalStep=1, largeStep=1),
                         BooleanDriverSetting('readDigits', 'Read numbers as individual &digits', defaultVal=False))
    supportedCommands = {IndexCommand, BreakCommand}
    supportedNotifications = {synthIndexReached, synthDoneSpeaking}

    @classmethod
    def check(cls):
        return all(path.is_file() for path in paths())

    def __init__(self):
        self._rate, self._pitch, self._volume = 50, 50, 100
        self._voice, self._inflection, self._wordSpacing = 'v5', 100, 0
        self._readDigits = False
        self._epoch = 0
        self._closed = False
        self._lock = threading.RLock()
        self._resume = threading.Event()
        self._resume.set()
        self._queue = queue.Queue()
        self._engine = Engine(*paths())
        try:
            self._player = create_player()
        except Exception:
            self._engine.close()
            raise
        self._thread = threading.Thread(target=self._run, name='MessengerSpeech', daemon=True)
        self._thread.start()

    def _get_availableVoices(self):
        return OrderedDict(('v%d' % n, VoiceInfo('v%d' % n,
            'Messenger V%d%s' % (n, ' (default)' if n == 5 else ''), 'en_US'))
            for n in range(10))

    def _get_voice(self):
        return self._voice

    def _set_voice(self, value):
        if value not in self._get_availableVoices():
            raise ValueError('Unknown Messenger voice characteristic')
        self._voice = value

    def _get_inflection(self):
        return self._inflection

    def _set_inflection(self, value):
        self._inflection = max(0, min(4, (int(value) + 12) // 25)) * 25

    def _get_wordSpacing(self):
        return self._wordSpacing

    def _set_wordSpacing(self, value):
        self._wordSpacing = max(0, min(9, int(value)))

    def _get_rate(self):
        return self._rate

    def _set_rate(self, value):
        self._rate = max(0, min(100, int(value)))

    def _get_pitch(self):
        return self._pitch

    def _set_pitch(self, value):
        self._pitch = max(0, min(100, int(value)))

    def _get_volume(self):
        return self._volume

    def _set_volume(self, value):
        self._volume = max(0, min(100, int(value)))

    def _get_readDigits(self):
        return self._readDigits

    def _set_readDigits(self, value):
        self._readDigits = bool(value)

    def speak(self, speechSequence):
        with self._lock:
            if self._closed:
                return
            epoch = self._epoch
            rate = round(self._rate / 10) if self._rate <= 50 else round(5 + (self._rate - 50) * 12 / 50)
            settings = {'rate': rate, 'pitch': min(9, int(self._pitch * 10 / 100)), 'volume': self._volume,
                        'voice': int(self._voice[1:]), 'intonation': (1, 2, 3, 4, 0)[self._inflection // 25],
                        'spacing': self._wordSpacing, 'output_gain': 200}
            pending = []

            def flush():
                for chunk in _chunks(normalise(' '.join(pending), spell_out=self._readDigits, lang='en')):
                    self._queue.put((epoch, 'text', (chunk, settings)))
                pending.clear()

            for item in speechSequence:
                if isinstance(item, str):
                    pending.append(item)
                elif isinstance(item, IndexCommand):
                    flush()
                    self._queue.put((epoch, 'index', item.index))
                elif isinstance(item, BreakCommand):
                    flush()
                    self._queue.put((epoch, 'break', max(0, min(10000, int(item.time)))))
            flush()
            self._queue.put((epoch, 'done', None))

    def cancel(self):
        with self._lock:
            self._epoch = (self._epoch + 1) & 0xffffffff
            self._engine.cancel(self._epoch)
            self._resume.set()
            # Drop queued labels immediately; holding Tab must not build a backlog.
            while True:
                try:
                    self._queue.get_nowait()
                except queue.Empty:
                    break
            self._player.stop()

    def pause(self, switch):
        with self._lock:
            if switch:
                self._resume.clear()
            else:
                self._resume.set()
            self._player.pause(switch)

    def _synthesize(self, epoch, text, settings):
        return self._engine.synthesize(epoch, text, **settings)

    def _feed(self, epoch, data, on_done=None):
        for offset in range(0, len(data), 640):  # Small chunks; actual latency depends on WavePlayer.
            while True:
                self._resume.wait()
                with self._lock:
                    if epoch != self._epoch or self._closed:
                        return
                    if not self._resume.is_set():
                        continue
                # WavePlayer.feed can block for buffer space. Cancel MUST be
                # able to call stop while it waits; never hold _lock here.
                chunk = data[offset:offset + 640]
                if on_done is not None and offset + 640 >= len(data):
                    self._player.feed(chunk, onDone=on_done)
                else:
                    self._player.feed(chunk)
                with self._lock:
                    if epoch != self._epoch or self._closed:
                        # Cancel can land between the check and feed. Clear
                        # that last block before this sole worker feeds any
                        # newer speech. It is at most 20 ms of stale audio.
                        self._player.stop()
                        return
                break

    def _notify(self, epoch, kind, payload):
        with self._lock:
            if epoch == self._epoch and not self._closed:
                if kind == 'index':
                    synthIndexReached.notify(synth=self, index=payload)
                else:
                    synthDoneSpeaking.notify(synth=self)

    def _run(self):
        while True:
            item = self._queue.get()
            if item is None:
                with self._lock:
                    self._engine.close()
                return
            epoch, kind, payload = item
            if epoch != self._epoch or self._closed:
                continue
            try:
                if kind == 'text':
                    text, settings = payload
                    self._feed(epoch, self._synthesize(epoch, text, settings))
                elif kind == 'break':
                    self._feed(epoch, b'\0\0' * (payload * 16))
                elif kind in ('index', 'done'):
                    # A one-sample marker orders notifications with playback
                    # without draining the player before the next text chunk.
                    self._feed(epoch, b'\0\0',
                               lambda e=epoch, k=kind, p=payload: self._notify(e, k, p))
                    if kind == 'done' and self._queue.empty() and epoch == self._epoch:
                        # Release legacy waveOut resources/ducking at a real
                        # idle boundary. stop can interrupt this wait.
                        self._player.idle()
            except Exception:
                with self._lock:
                    if epoch == self._epoch and not self._closed:
                        log.exception('Messenger speech failed')
                        # Discard the failed utterance's remaining audio and
                        # indexes, and release NVDA's speech queue.
                        self.cancel()
                        synthDoneSpeaking.notify(synth=self)

    def terminate(self):
        with self._lock:
            self._closed = True
        self.cancel()
        self._queue.put(None)
        self._thread.join(timeout=3)
        if self._thread.is_alive():
            log.error('Messenger speech thread did not stop; retaining its engine until exit')
        else:
            self._player.close()
