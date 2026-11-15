# Decoding frames

A user turns `VideoPacket`s into RGB `HxWx3 uint8` arrays with `pyinsta360.decode.VideoDecoder(cam.video_encode_type())`, using `decode(packet)` per packet and `flush()` at the end. PyAV is an optional dependency (`pyinsta360[decode]`).

## Sub-features

- `decode` turns H.264 or H.265 packets into RGB frames.
- `flush` returns the frame held back by the parser's one-packet lag.
- `missing-av` raises a clear error naming the `decode` extra when PyAV is absent.

## How to get to it (user POV)

- `from pyinsta360.decode import VideoDecoder`, then call `decoder.decode(packet)` inside the streaming loop.

## Driving it with drive.sh

Preconditions:

- Real backend only. The fake's packets are not a valid bitstream. Decoder unit behavior on a synthetic stream is covered by `tests/test_decode.py` through `tools/test.sh`.

- **Decode a live frame (real).** Run `$S/drive.sh real stream --seconds 5 --save-frame`. It prints `PASS decoded at least one frame from the first 60 packets`. `frame.png` in the evidence dir shows two side-by-side fisheye circles, which is the raw dual-fisheye layout in 360 mode.

## Gotchas

- The stream may not start at a keyframe. The journey feeds up to 60 packets before it gives up. A failure on the first packet alone is not a bug.
- Inspect `frame.png` by opening it. A black or green frame is a decode failure even though a frame was returned.
