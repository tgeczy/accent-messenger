"""Explicit native-test opt-in; no DLL loads or builds during collection."""
from pathlib import Path
import subprocess
import sys

import pytest

ROOT = Path(__file__).resolve().parents[1]


def pytest_addoption(parser):
    parser.addoption('--run-integration', action='store_true',
                     help='Run Windows native/NVDA/SAPI checks against existing builds')
    parser.addoption('--engine-python', action='append', default=[], metavar='EXE',
                     help='Python interpreter for native/NVDA checks; repeat for x86/x64')


def pytest_generate_tests(metafunc):
    if 'engine_python' in metafunc.fixturenames:
        interpreters = metafunc.config.getoption('--engine-python') or [sys.executable]
        metafunc.parametrize('engine_python', interpreters)


def pytest_collection_modifyitems(config, items):
    for item in items:
        if 'integration' not in item.keywords:
            continue
        if not config.getoption('--run-integration'):
            item.add_marker(pytest.mark.skip(reason='Use --run-integration after building the engines'))
        elif sys.platform != 'win32':
            item.add_marker(pytest.mark.skip(reason='Native integration requires Windows'))


@pytest.fixture
def repo_root():
    return ROOT


@pytest.fixture
def run_check():
    def run(command, timeout=180):
        result = subprocess.run([str(arg) for arg in command], cwd=ROOT,
                                capture_output=True, text=True, timeout=timeout)
        assert result.returncode == 0, (
            f'Command failed ({result.returncode}): {command}\n'
            f'{result.stdout}\n{result.stderr}')
        return result
    return run
