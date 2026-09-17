"""Small ctypes boundary. No external Python or third-party Python packages."""
import ctypes
import hashlib
import json
from pathlib import Path
import re
import unicodedata

DRIVER_SHA256 = 'a2963b7025b67c577e3e3839342d9f49c39c7039d0ffcad36b1f066c816a1572'


def plain_text(text):
    replacements = {'\u2019': "'", '\u2018': "'", '\u201c': '"', '\u201d': '"',
                    '\u2013': '-', '\u2014': '-', '\u2026': '...', '~': ' tilde '}
    for original, replacement in replacements.items():
        text = text.replace(original, replacement)
    text = ''.join(c for c in unicodedata.normalize('NFKD', text) if not unicodedata.combining(c))
    text = text.encode('ascii', errors='replace').decode('ascii')
    return re.sub(r'\s+', ' ', re.sub(r'[\x00-\x1f\x7f]', ' ', text)).strip()


def paths(root=None):
    root = Path(root) if root else Path(__file__).parent
    arch = 'x64' if ctypes.sizeof(ctypes.c_void_p) == 8 else 'x86'
    return root / arch / 'messenger.dll', root / 'data/SPKMIC.TSR', root / 'data/calibration.json'


class Engine:
    """One owner thread for synthesis/close. cancel may run on NVDA's thread."""
    def __init__(self, library, driver, calibration):
        self._handle = None
        data = Path(driver).read_bytes()
        if hashlib.sha256(data).hexdigest() != DRIVER_SHA256:
            raise ValueError('Unrecognized Messenger pronunciation driver')
        fit = json.loads(Path(calibration).read_text(encoding='utf-8'))
        if fit['source_sha256']['driver'] != DRIVER_SHA256:
            raise ValueError('Calibration does not match the driver')
        parameters = fit['models']['constant']['parameters']
        if len(parameters) != 10 or not fit['models']['constant']['optimizer_success']:
            raise ValueError('Invalid Messenger calibration')
        self.dll = dll = ctypes.CDLL(str(Path(library).resolve()))
        pointer, count = ctypes.c_void_p, ctypes.c_uint32
        dll.msg_api_version.argtypes = []
        dll.msg_api_version.restype = count
        self._api_version = dll.msg_api_version()
        if self._api_version not in (1, 2):
            raise RuntimeError('Unsupported Messenger library version')
        dll.msg_create.argtypes = [ctypes.c_char_p, count, ctypes.POINTER(ctypes.c_double)]
        dll.msg_create.restype = pointer
        dll.msg_destroy.argtypes = [pointer]
        dll.msg_destroy.restype = None
        dll.msg_cancel.argtypes = [pointer, count]
        dll.msg_cancel.restype = None
        dll.msg_synthesize.argtypes = [pointer, count, ctypes.c_char_p, ctypes.c_int, ctypes.c_int, ctypes.c_int]
        dll.msg_synthesize.restype = ctypes.c_int
        if self._api_version >= 2:
            dll.msg_synthesize_options.argtypes = [pointer, count, ctypes.c_char_p] + [ctypes.c_int] * 7
            dll.msg_synthesize_options.restype = ctypes.c_int
        for name in ('msg_pcm', 'msg_frames'):
            fn = getattr(dll, name)
            fn.argtypes = [pointer, ctypes.POINTER(count)]
            fn.restype = pointer
        dll.msg_error.argtypes = [pointer]
        dll.msg_error.restype = ctypes.c_char_p
        self._handle = dll.msg_create(data, len(data), (ctypes.c_double * 10)(*parameters))
        if not self._handle:
            raise RuntimeError('Messenger pronunciation engine could not initialize')

    def cancel(self, generation):
        if self._handle:
            self.dll.msg_cancel(self._handle, generation)

    def synthesize(self, generation, text, rate=5, pitch=5, volume=100,
                   voice=5, intonation=0, spacing=0, output_gain=100):
        if not self._handle:
            raise RuntimeError('Messenger engine is closed')
        if not isinstance(text, str) or len(text) > 600:
            raise ValueError('Expected up to 600 characters')
        if not all(isinstance(v, int) and 0 <= v <= limit for v, limit in ((rate, 17), (pitch, 9), (volume, 100))):
            raise ValueError('Invalid rate, pitch or volume')
        if not all(isinstance(v, int) and 0 <= v <= limit for v, limit in
                   ((voice, 9), (intonation, 4), (spacing, 9))):
            raise ValueError('Invalid voice, intonation or spacing')
        if not isinstance(output_gain, int) or not 100 <= output_gain <= 200:
            raise ValueError('Invalid output gain')
        request = (self._handle, generation, plain_text(text).encode('ascii'), rate, pitch, volume)
        if self._api_version >= 2:
            result = self.dll.msg_synthesize_options(*request, voice, intonation, spacing, output_gain)
        else:
            if (voice, intonation, spacing, output_gain) != (5, 0, 0, 100):
                raise RuntimeError('Voice options require a newer Messenger library')
            result = self.dll.msg_synthesize(*request)
        if result == 1:
            return b''
        if result != 0:
            raise RuntimeError(self.dll.msg_error(self._handle).decode('utf-8', errors='replace'))
        count = ctypes.c_uint32()
        pcm = self.dll.msg_pcm(self._handle, ctypes.byref(count))
        return ctypes.string_at(pcm, count.value * 2) if count.value else b''

    def frames(self):
        count = ctypes.c_uint32()
        ptr = self.dll.msg_frames(self._handle, ctypes.byref(count))
        return ctypes.string_at(ptr, count.value) if count.value else b''

    def close(self):
        if self._handle:
            self.dll.msg_destroy(self._handle)
            self._handle = None
