# Decoding frames

A user turns `VideoPacket`s into RGB `HxWx3 uint8` arrays with `pyinsta360.decode.VideoDecoder(cam.video_encode_type())`, using `decode(packet)` per packet and `flush()` at the end. PyAV is an optional dependency (`pyinsta360[decode]`).

## Sub-features

- `decode` turns H.264 or H.265 packets into RGB frames, using one `VideoDecoder` per `stream_index`.
- `flush` returns the frame held back by the parser's one-packet lag.
- `missing-av` raises a clear error naming the `decode` extra when PyAV is absent.

## How to get to it (user POV)

- `from pyinsta360.decode import VideoDecoder`, then call `decoder.decode(packet)` inside the streaming loop.

## Driving it with drive.sh

Preconditions:

- Real backend only. The fake's packets are not a valid bitstream. Decoder unit behavior on a synthetic stream is covered by `tests/test_decode.py` through `tools/test.sh`.

- **Decode a live frame (real).** Run `$S/drive.sh real stream --seconds 5 --save-frame`. It prints `PASS stream N: decoded a frame from its first 60 packets` for each stream. `frame_stream0.png` and `frame_stream1.png` each show one fisheye circle (2880x2880 on the X3 in `RES_2880_2880P30` mode). Modes that send a single stream show two side-by-side circles in one frame.

## Gotchas

- Feeding packets from several `stream_index` values into one decoder interleaves two bitstreams. The first frame can still look fine, so a single good frame does not prove the stream handling is right.
- The stream may not start at a keyframe. The journey feeds up to 60 packets before it gives up. A failure on the first packet alone is not a bug.
- Inspect `frame.png` by opening it. A black or green frame is a decode failure even though a frame was returned.
