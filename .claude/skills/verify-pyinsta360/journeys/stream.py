"""Stream live video + IMU for N seconds; dump the first packet, IMU head and rates."""
import argparse
import time

import numpy as np

import pyinsta360 as pi
from _common import BACKEND, EVIDENCE, check, finish, write_json

ap = argparse.ArgumentParser()
ap.add_argument("--seconds", type=float, default=3.0)
ap.add_argument("--serial", default=None)
ap.add_argument("--video-queue", type=int, default=64)
ap.add_argument("--read-delay", type=float, default=0.0, help="sleep per loop to act as a slow consumer")
ap.add_argument("--save-frame", action="store_true", help="decode the first frames to frame.png (needs a real H.264/H.265 stream)")
args = ap.parse_args()

param = pi.LiveStreamParam(using_lrv=False, enable_audio=False)
packets, imu_chunks = [], []
with pi.Camera(args.serial) as cam:
    encode_type = cam.video_encode_type()
    cam.start_live_streaming(param, video_queue_size=args.video_queue)
    check(cam.state == pi.CameraState.STREAMING, "start_live_streaming() moves state to STREAMING")
    start = time.monotonic()
    while time.monotonic() - start < args.seconds:
        p = cam.read_video(timeout=0.05)
        if p is not None:
            packets.append(p)
        imu = cam.read_imu()
        if len(imu):
            imu_chunks.append(imu)
        if args.read_delay:
            time.sleep(args.read_delay)
    elapsed = time.monotonic() - start
    stats = cam.stream_stats()
    cam.stop_live_streaming()
    check(cam.state == pi.CameraState.OPEN, "stop_live_streaming() returns state to OPEN")
    check(cam.read_video(timeout=0.2) is None, "read_video() after stop returns None")

imu = np.concatenate(imu_chunks) if imu_chunks else np.zeros(0)
ts = [p.timestamp for p in packets]
summary = {
    "seconds": elapsed, "encode_type": str(encode_type),
    "video_packets": len(packets), "video_fps": len(packets) / elapsed,
    "video_bytes": sum(len(p) for p in packets), "stream_indices": sorted({p.stream_index for p in packets}),
    "imu_samples": int(len(imu)), "imu_hz": len(imu) / elapsed,
    "imu_dtype": str(imu.dtype), "stats": stats,
}
if len(imu):
    accel_norm = np.sqrt(imu["ax"] ** 2 + imu["ay"] ** 2 + imu["az"] ** 2)
    summary["accel_norm_median_g"] = float(np.median(accel_norm))
print("summary:", summary)
write_json("stream_summary.json", summary)

check(len(packets) > 0, "received video packets")
if args.read_delay:
    check(stats["video_dropped"] > 0, "a slow consumer makes the full video queue drop and count packets")
check(all(b > a for a, b in zip(ts, ts[1:])), "video timestamps strictly increase")
check(len(imu) > 0, "received IMU samples")
check(list(imu.dtype.names or []) == ["timestamp", "ax", "ay", "az", "gx", "gy", "gz"], "IMU dtype fields match GyroData")
if packets:
    (EVIDENCE / "first_packet.bin").write_bytes(packets[0].data)
if len(imu):
    np.save(EVIDENCE / "imu_head.npy", imu[:200])
if BACKEND == "real" and len(imu):
    check(0.8 < summary["accel_norm_median_g"] < 1.2, "accel magnitude near 1 g (camera roughly still, units are g)")

if args.save_frame and packets:
    from pyinsta360.decode import VideoDecoder

    decoder = VideoDecoder(encode_type)
    frames = []
    for p in packets[:60]:
        frames += decoder.decode(p)
        if frames:
            break
    if not frames:
        frames = decoder.flush()
    if check(bool(frames), "decoded at least one frame from the first 60 packets"):
        from PIL import Image

        Image.fromarray(frames[0]).save(EVIDENCE / "frame.png")
        print("frame.png", frames[0].shape)
finish()
