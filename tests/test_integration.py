"""Run DLL and NVDA doubles in child processes, keeping pytest's state clean."""
import json
import sys

import pytest

pytestmark = pytest.mark.integration


def test_native_corpus_and_cancellation(repo_root, tmp_path, run_check, engine_python):
    report = tmp_path / 'native.json'
    run_check([engine_python, repo_root / 'tests/runners/native_checks.py',
               '--output', report])
    result = json.loads(report.read_text(encoding='utf-8'))
    assert result['passed']
    assert len(result['cases']) == 128
    assert len(result['cancellation_ms']) == 20


def test_nvda_adapter(repo_root, tmp_path, run_check, engine_python):
    report = tmp_path / 'nvda.json'
    run_check([engine_python, repo_root / 'tests/runners/nvda_checks.py',
               '--scratch', tmp_path / 'addon', '--output', report])
    result = json.loads(report.read_text(encoding='utf-8'))
    assert result['successful']
    assert result['tests_run'] == 14


def test_sapi_streaming_settings_and_architecture_parity(repo_root, tmp_path, run_check):
    report = tmp_path / 'sapi.json'
    run_check([sys.executable, repo_root / 'tests/runners/sapi_checks.py',
               '--scratch', tmp_path / 'sapi', '--output', report], timeout=240)
    result = json.loads(report.read_text(encoding='utf-8'))
    assert set(result['architectures']) == {'x86', 'x64'}
    for arch in result['architectures'].values():
        assert arch['passed']
        assert arch['lexical_cases'] == 242
