from fractions import Fraction

import numpy as np
import pytest

av = pytest.importorskip("av")

import pyinsta360 as pi
from pyinsta360.decode import VideoDecoder

WIDTH, HEIGHT, COUNT = 128, 64, 6


def encode_stream(codec):
    """Annex-B chunks, one per frame, of a clip that moves a white square over blue."""
    ctx = av.CodecContext.create(codec, "w")
    ctx.width, ctx.height, ctx.pix_fmt, ctx.framerate = WIDTH, HEIGHT, "yuv420p", 30
    ctx.time_base = Fraction(1, 30)
    ctx.options = {"bf": "0", "g": "30"}
    chunks = []
    for i in range(COUNT):
        rgb = np.zeros((HEIGHT, WIDTH, 3), np.uint8)
        rgb[..., 2] = 200
        rgb[16:48, 8 * i : 8 * i + 32] = 255
        frame = av.VideoFrame.from_ndarray(rgb, format="rgb24").reformat(format="yuv420p")
        frame.pts = i
        chunks.extend(bytes(p) for p in ctx.encode(frame))
    chunks.extend(bytes(p) for p in ctx.encode(None))
    return chunks


@pytest.mark.parametrize(
    "encode_type,codec",
    [(pi.VideoEncodeType.H264, "libx264"), (pi.VideoEncodeType.H265, "libx265")],
)
def test_decodes_every_frame_to_rgb(encode_type, codec):
    decoder = VideoDecoder(encode_type)
    frames = []
    for chunk in encode_stream(codec):
        frames.extend(decoder.decode(chunk))
    frames.extend(decoder.flush())

    assert len(frames) == COUNT
    for i, frame in enumerate(frames):
        assert frame.shape == (HEIGHT, WIDTH, 3) and frame.dtype == np.uint8
        inside = frame[32, 8 * i + 16].astype(int)
        outside = frame[2, 2].astype(int)
        assert inside.min() > 200
        assert outside[2] > 120 and outside[0] < 80


def test_accepts_objects_with_a_data_attribute():
    class Packet:
        def __init__(self, data):
            self.data = data

    decoder = VideoDecoder(pi.VideoEncodeType.H264)
    frames = []
    for chunk in encode_stream("libx264"):
        frames.extend(decoder.decode(Packet(chunk)))
    frames.extend(decoder.flush())
    assert len(frames) == COUNT


def test_missing_pyav_gives_a_clear_error(monkeypatch):
    import builtins

    real_import = builtins.__import__

    def no_av(name, *args, **kwargs):
        if name == "av":
            raise ImportError("no av")
        return real_import(name, *args, **kwargs)

    monkeypatch.setattr(builtins, "__import__", no_av)
    with pytest.raises(ImportError, match=r"pyinsta360\[decode\]"):
        VideoDecoder(pi.VideoEncodeType.H264)


def test_rejects_unknown_encode_type():
    with pytest.raises(ValueError):
        VideoDecoder("h264")


def held_back_frames(decoder, chunks):
    """Most frames fed in but not yet returned at any point before flush."""
    fed = returned = held = 0
    for chunk in chunks:
        fed += 1
        returned += len(decoder.decode(chunk))
        held = max(held, fed - returned)
    return held


@pytest.mark.parametrize("threads", [2, 4])
def test_threaded_decoder_returns_the_same_frames(threads):
    chunks = encode_stream("libx264")
    single = VideoDecoder(pi.VideoEncodeType.H264)
    expected = [f for c in chunks for f in single.decode(c)] + single.flush()
    threaded = VideoDecoder(pi.VideoEncodeType.H264, threads=threads)
    frames = [f for c in chunks for f in threaded.decode(c)] + threaded.flush()

    assert len(frames) == COUNT
    for got, want in zip(frames, expected):
        np.testing.assert_array_equal(got, want)


def test_threads_hold_frames_back_until_later_packets_arrive():
    chunks = encode_stream("libx264")
    single = held_back_frames(VideoDecoder(pi.VideoEncodeType.H264), chunks)
    threaded = held_back_frames(VideoDecoder(pi.VideoEncodeType.H264, threads=4), chunks)
    assert threaded > single


@pytest.mark.parametrize("threads", [0, -1, 1.5, "4"])
def test_rejects_a_thread_count_below_one_or_not_an_int(threads):
    with pytest.raises(ValueError):
        VideoDecoder(pi.VideoEncodeType.H264, threads=threads)
