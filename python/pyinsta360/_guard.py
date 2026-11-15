"""Marks the windows in which an SDK crash is a discovery crash.

`pyinsta360.supervise` installs a shared flag here in its child process. Outside
supervision `flag` stays None and `discovering()` costs one attribute check.
"""
from contextlib import contextmanager

flag = None  # multiprocessing.RawValue('i') or None


@contextmanager
def discovering():
    f = flag
    if f is None:
        yield
        return
    f.value = 1
    try:
        yield
    finally:
        f.value = 0
