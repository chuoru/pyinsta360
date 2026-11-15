"""Opt-in process-level recovery from the SDK's discovery crash."""
import logging
import multiprocessing
import pickle
import signal
import traceback

from . import _guard

_log = logging.getLogger("pyinsta360")


class SupervisedProcessError(RuntimeError):
    """The supervised process died without returning a result.

    Attributes:
        exitcode: the child's exit code (negative: killed by that signal).
        attempts: how many times the target was started.
        during_discovery: True if the process died inside discover() or open().
    """

    def __init__(self, message, exitcode, attempts, during_discovery):
        super().__init__(message)
        self.exitcode = exitcode
        self.attempts = attempts
        self.during_discovery = during_discovery


def _child_main(conn, flag, target, args, kwargs):
    _guard.flag = flag
    try:
        message = ("ok", target(*args, **kwargs))
    except BaseException as exc:
        message = ("err", exc)
    try:
        if message[0] == "err":
            # Some exceptions pickle but cannot be unpickled in the parent.
            pickle.loads(pickle.dumps(message[1]))
        conn.send(message)
    except Exception:
        conn.send(("err", RuntimeError(traceback.format_exc())))
    finally:
        conn.close()


def _signal_name(exitcode):
    try:
        return signal.Signals(-exitcode).name
    except ValueError:
        return f"signal {-exitcode}"


def run_supervised(target, *args, retries=3, **kwargs):
    """Run ``target(*args, **kwargs)`` in a child process and return its result.

    CameraSDK 2.1.8 sometimes aborts the process while discovering cameras.
    If the child is killed by a signal while inside ``discover()`` or
    ``Camera.open()``, the target is rerun from the start, up to ``retries``
    extra times, then SupervisedProcessError is raised. Any other abnormal exit
    raises SupervisedProcessError at once. An exception raised by the target is
    re-raised here.

    On a retry the target restarts from the beginning, so work done before
    open() repeats. Only crashes while discovering or opening are retried.

    The child is spawned, never forked, because the caller may already hold SDK
    threads. ``target``, the arguments and the result must therefore be
    picklable, and ``target`` must be a module-level function.

    Meant for scripts and data-collection entry points: ``run_supervised(main)``.
    A notebook that calls ``discover()`` directly is not protected.
    """
    ctx = multiprocessing.get_context("spawn")
    flag = ctx.RawValue("i", 0)
    attempts = 0
    while True:
        attempts += 1
        flag.value = 0
        parent_conn, child_conn = ctx.Pipe(duplex=False)
        proc = ctx.Process(target=_child_main, args=(child_conn, flag, target, args, kwargs))
        proc.start()
        child_conn.close()
        try:
            kind, payload = parent_conn.recv()
        except EOFError:
            kind = payload = None
        finally:
            parent_conn.close()
            proc.join()

        if kind == "ok":
            return payload
        if kind == "err":
            raise payload

        exitcode = proc.exitcode
        in_discovery = exitcode < 0 and bool(flag.value)
        if in_discovery and attempts <= retries:
            _log.warning(
                "attempt %d of the supervised process died with %s during discovery; retrying",
                attempts,
                _signal_name(exitcode),
            )
            continue
        how = f"killed by {_signal_name(exitcode)}" if exitcode < 0 else f"exited with code {exitcode}"
        raise SupervisedProcessError(
            f"supervised process {how} after {attempts} attempt(s)"
            + (" during discovery" if in_discovery else ""),
            exitcode,
            attempts,
            in_discovery,
        )
