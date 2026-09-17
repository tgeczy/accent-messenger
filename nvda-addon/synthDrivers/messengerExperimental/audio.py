"""NVDA audio API compatibility, shared by the old waveOut and new WASAPI paths."""
import config
import nvwave


def create_player():
    try:
        device = config.conf['audio']['outputDevice']
    except KeyError:
        device = config.conf['speech']['outputDevice']
    # The common signature works on NVDA 2021.1 through 2026.2. Avoid newer
    # purpose/stream APIs so the Windows 7 waveOut implementation remains usable.
    return nvwave.WavePlayer(channels=1, samplesPerSec=16000, bitsPerSample=16,
                             outputDevice=device)
