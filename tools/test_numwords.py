"""Messenger's English use of Outspoken's number parser; stdlib-only tests."""
import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    'messenger_numwords', ROOT / 'nvda-addon/synthDrivers/messengerExperimental/numwords.py')
numwords = importlib.util.module_from_spec(spec)
spec.loader.exec_module(numwords)


class NumberTests(unittest.TestCase):
    def test_cardinals(self):
        for text, expected in (
            ('0', 'zero'), ('100', 'one hundred'), ('101', 'one hundred one'),
            ('123', 'one hundred twenty three'), ('999', 'nine hundred ninety nine'),
            ('1984', 'one thousand nine hundred eighty four'),
            ('1,234', 'one thousand two hundred thirty four'),
            ('1,000,000', 'one million'), ('1000000000000000', 'one quadrillion'),
            ('1000000000000000000', 'large number'),
        ):
            with self.subTest(text=text):
                self.assertEqual(numwords.normalise(text, lang='en'), expected)

    def test_digits(self):
        self.assertEqual(numwords.normalise('100 001 123', spell_out=True, lang='en'),
                         'one zero zero zero zero one one two three')
        self.assertEqual(numwords.normalise('1,234.50', spell_out=True),
                         'one two three four point five zero')

    def test_decimals_signs_and_ordinals(self):
        for text, expected in (
            ('-7 items', 'minus seven items'), ('3.14', 'three point one four'),
            ('1,234.50', 'one thousand two hundred thirty four point five zero'),
            ('the 21st', 'the twenty first'), ('3rd', 'third'),
        ):
            with self.subTest(text=text):
                self.assertEqual(numwords.normalise(text), expected)
        self.assertEqual(numwords.normalise('3rd', spell_out=True), 'third')

    def test_identifiers_and_text(self):
        for text in ('mp3', 'utf8', 'x2go', 'COM1', 'v2.1.3', '192.168.0.1',
                     '.5', 'Eloquence. Four. Hello.', ''):
            with self.subTest(text=text):
                self.assertEqual(numwords.normalise(text), text)
                self.assertEqual(numwords.normalise(text, spell_out=True), text)

    def test_huge_input_and_zero_padding(self):
        self.assertEqual(numwords.normalise('9' * 5000), 'large number')
        self.assertEqual(numwords.normalise('9' * 5000 + 'th'), 'large number')
        self.assertEqual(numwords.normalise('0' * 5000 + '100'), 'one hundred')
        self.assertEqual(numwords.normalise('0' * 5000 + '3rd'), 'third')
        self.assertEqual(numwords.normalise('9' * 5000, spell_out=True).split(), ['nine'] * 5000)


if __name__ == '__main__':
    unittest.main()
