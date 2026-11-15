"""Take a photo and record a short clip. On a real camera this WRITES to its storage."""
import argparse
import os
import time

import pyinsta360 as pi
from _common import BACKEND, check, finish, write_json

ap = argparse.ArgumentParser()
ap.add_argument("--record-seconds", type=float, default=3.0)
ap.add_argument("--allow-camera-writes", action="store_true")
ap.add_argument("--serial", default=None)
args = ap.parse_args()
if BACKEND == "real" and not args.allow_camera_writes:
    raise SystemExit("REFUSE: capture writes photos/videos to the camera; pass --allow-camera-writes")

with pi.Camera(args.serial) as cam:
    before = set(cam.files_list())
    photo = cam.take_photo()
    print("photo:", photo.origin_urls)
    check(bool(photo), "take_photo() returned a non-empty MediaUrl")
    cam.start_recording()
    time.sleep(args.record_seconds)
    video = cam.stop_recording()
    print("video:", video.origin_urls, "lrv:", video.lrv_urls)
    check(bool(video), "stop_recording() returned a non-empty MediaUrl")
    after = set(cam.files_list())
    if BACKEND == "real":
        new = sorted(after - before)
        print("new files on camera:", new)
        check(len(new) >= 2, "camera file list grew by the photo and the clip")
    write_json("capture.json", {"photo": photo.origin_urls, "video": video.origin_urls,
                                "lrv": video.lrv_urls, "new_files": sorted(after - before)})
finish()
