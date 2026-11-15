#!/usr/bin/env python3
"""Smoke test against a real camera: stream for N seconds and report rates.

    python examples/stream_smoke.py --seconds 10 --save-frame first.png

Decoding the first frame needs PyAV (`pip install pyinsta360[decode]`) and, for
--save-frame, Pillow.
"""
import argparse
import sys
import time


import pyinsta360 as pi


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--seconds", type=float, default=10.0)
    ap.add_argument("--serial", default=None, help="open this camera instead of the first one")
    ap.add_argument("--save-frame", metavar="PNG", help="decode the first frame and save it")
    ap.add_argument("--log-level", default="ERR", choices=[l for l in pi.LogLevel.__members__])
    args = ap.parse_args()

    pi.set_log_level(pi.LogLevel.__members__[args.log_level])
    print("SDK", pi.sdk_version())
    devices = pi.discover()
    print("discovered:", devices)
    if not devices:
        print("no camera found; is it connected over USB and in Android mode?", file=sys.stderr)
        return 1

    # Settings the reference bridge found necessary on real hardware: using_lrv
    # delivers no video in dual-lens mode, and audio is ignored anyway.
    param = pi.LiveStreamParam(using_lrv=False, enable_audio=False, video_bitrate=8 * 1024 * 1024)

    video_count = 0
    video_bytes = 0
    imu_count = 0
    first_packet = None
    with pi.Camera(args.serial) as cam:
        print("opened:", cam.info, "encode:", cam.video_encode_type())
        cam.start_live_streaming(param)
        start = time.monotonic()
        while time.monotonic() - start < args.seconds:
            packet = cam.read_video(timeout=0.05)
            if packet is not None:
                video_count += 1
                video_bytes += len(packet)
                if first_packet is None:
                    first_packet = packet
            imu_count += len(cam.read_imu())
        elapsed = time.monotonic() - start
        stats = cam.stream_stats()
        encode_type = cam.video_encode_type()

    print("video: %d packets, %.1f fps, %.2f Mbit/s" % (
        video_count, video_count / elapsed, video_bytes * 8 / elapsed / 1e6))
    print("imu:   %d samples, %.1f Hz" % (imu_count, imu_count / elapsed))
    print("dropped:", stats)

    if args.save_frame and first_packet is not None:
        from pyinsta360.decode import VideoDecoder
        from PIL import Image

        decoder = VideoDecoder(encode_type)
        frames = decoder.decode(first_packet) + decoder.flush()
        if frames:
            Image.fromarray(frames[0]).save(args.save_frame)
            print("saved", args.save_frame, frames[0].shape)
        else:
            print("first packet decoded to no frame (it may not start at a keyframe)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
