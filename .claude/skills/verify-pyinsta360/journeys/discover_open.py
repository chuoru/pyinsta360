"""Discover cameras, open one, read its identity and status, close it."""
import argparse

import pyinsta360 as pi
from _common import BACKEND, check, finish, write_json

ap = argparse.ArgumentParser()
ap.add_argument("--serial", default=None)
args = ap.parse_args()

devices = pi.discover()
print("discovered:", devices)
check(len(devices) >= 1, "discover() found at least one camera")
if not devices:
    finish()

cam = pi.Camera(args.serial)
check(cam.state == pi.CameraState.CLOSED, "new Camera starts CLOSED")
try:
    cam.battery_status()
    check(False, "battery_status() before open() raises Insta360Error")
except pi.Insta360Error:
    check(True, "battery_status() before open() raises Insta360Error")

with cam:
    check(cam.state == pi.CameraState.OPEN, "open() moves state to OPEN")
    check(cam.is_connected(), "is_connected() is True while open")
    info = cam.info
    if args.serial:
        check(info.serial_number == args.serial, "opened the requested serial")
    battery = cam.battery_status()
    snapshot = {
        "info": repr(info),
        "encode_type": str(cam.video_encode_type()),
        "function_mode": str(cam.current_function_mode()),
        "battery": {"level": battery.battery_level, "scale": battery.battery_scale,
                    "power": str(battery.power_type)},
    }
    print("camera:", snapshot)
    write_json("camera.json", snapshot)
check(cam.state == pi.CameraState.CLOSED, "leaving the with-block closes the camera")
cam.close()
check(cam.state == pi.CameraState.CLOSED, "a second close() is a no-op")
finish()
