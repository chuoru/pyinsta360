import os
import threading

import pyinsta360


def add(a, b=0):
    return a + b


def open_and_report(counter_file):
    with open(counter_file, "a") as f:
        f.write("x")
    with pyinsta360.Camera() as cam:
        return cam.info.serial_number


def open_then_abort(counter_file):
    open_and_report(counter_file)
    os.abort()


def raise_value_error():
    raise ValueError("boom")


class Unpicklable(Exception):
    def __init__(self):
        super().__init__("no pickle")
        self.lock = threading.Lock()


def raise_unpicklable():
    raise Unpicklable()


def exit_nonzero():
    os._exit(3)
