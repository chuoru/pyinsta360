import logging
import signal

import pytest

import pyinsta360 as pi
import supervise_targets as targets


@pytest.fixture
def crash_file(tmp_path, monkeypatch, fake):
    path = tmp_path / "crash"
    monkeypatch.setenv("FAKE_INSTA360_DISCOVERY_CRASH_FILE", str(path))
    return path


def test_result_args_and_kwargs_pass_through(fake):
    assert pi.run_supervised(targets.add, 2, b=5) == 7


def test_crash_in_discovery_is_retried(crash_file, tmp_path, caplog):
    crash_file.write_text("1")
    counter = tmp_path / "runs"
    with caplog.at_level(logging.WARNING, logger="pyinsta360"):
        pi.run_supervised(targets.open_and_report, str(counter))
    assert counter.read_text() == "xx"
    assert any("attempt 1" in r.getMessage() and "SIGABRT" in r.getMessage() for r in caplog.records)


def test_retries_exhausted(crash_file, tmp_path):
    crash_file.write_text("99")
    counter = tmp_path / "runs"
    with pytest.raises(pi.SupervisedProcessError) as info:
        pi.run_supervised(targets.open_and_report, str(counter), retries=2)
    assert info.value.attempts == 3
    assert info.value.during_discovery is True
    assert info.value.exitcode == -signal.SIGABRT
    assert counter.read_text() == "xxx"


def test_crash_outside_discovery_is_not_retried(fake, tmp_path):
    counter = tmp_path / "runs"
    with pytest.raises(pi.SupervisedProcessError) as info:
        pi.run_supervised(targets.open_then_abort, str(counter))
    assert info.value.attempts == 1
    assert info.value.during_discovery is False
    assert info.value.exitcode == -signal.SIGABRT
    assert counter.read_text() == "x"


def test_nonzero_exit_without_result_is_not_retried(fake):
    with pytest.raises(pi.SupervisedProcessError) as info:
        pi.run_supervised(targets.exit_nonzero)
    assert (info.value.exitcode, info.value.attempts) == (3, 1)


def test_target_exception_is_reraised(fake):
    with pytest.raises(ValueError, match="boom"):
        pi.run_supervised(targets.raise_value_error)


def test_unpicklable_exception_becomes_runtime_error(fake):
    with pytest.raises(RuntimeError, match="Traceback") as info:
        pi.run_supervised(targets.raise_unpicklable)
    assert "_thread.lock" in str(info.value)
