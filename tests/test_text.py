"""Text entering the legacy driver must be readable ASCII, without controls."""
import importlib.util
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    'messenger_text_tests', ROOT / 'nvda-addon/synthDrivers/messengerExperimental/engine.py')
engine = importlib.util.module_from_spec(spec)
spec.loader.exec_module(engine)


@pytest.mark.parametrize(('text', 'expected'), [
    ('Hello, Tomi!', 'Hello, Tomi!'),
    ('caf\u00e9', 'cafe'),
    ('e\u0301', 'e'),
    ('\u201cIt\u2019s fine\u201d', '"It\'s fine"'),
    ('one\u2014two\u2026', 'one-two...'),
    ('a~b', 'a tilde b'),
    ('\x1bA0\x00hello\x7f', 'A0 hello'),
    ('  one\r\n\t two  ', 'one two'),
    ('', ''),
])
def test_plain_text(text, expected):
    assert engine.plain_text(text) == expected
