"""Decode the camera's H.264/H.265 preview stream into RGB frames.

Requires PyAV: ``pip install pyinsta360[decode]``.

A decoder holds one bitstream. In modes that send each lens as its own stream, create one
VideoDecoder per ``VideoPacket.stream_index``.
"""
from typing import List

import numpy as np

from ._sdk import VideoEncodeType

_CODEC_NAMES = {VideoEncodeType.H264: "h264", VideoEncodeType.H265: "hevc"}


def _import_av():
    try:
        import av
    except ImportError as exc:
        raise ImportError(
            "pyinsta360.decode needs PyAV; install it with `pip install pyinsta360[decode]` "
            "(or `pip install av`)."
        ) from exc
    return av


class VideoDecoder:
    """Stateful streaming decoder.

    Feed packets strictly in arrival order. The bitstream parser only emits a
    frame once it has seen the start of the next one, so decoding runs one
    packet behind; call flush() once at the end of a stream to get the rest.
    """

    def __init__(self, encode_type: VideoEncodeType):
        if encode_type not in _CODEC_NAMES:
            raise ValueError("unsupported encode type: %r" % (encode_type,))
        av = _import_av()
        self._ctx = av.CodecContext.create(_CODEC_NAMES[encode_type], "r")

    def decode(self, packet) -> List[np.ndarray]:
        """Decode a VideoPacket (or any bytes-like chunk) into zero or more RGB
        frames, each an HxWx3 uint8 array."""
        data = packet.data if hasattr(packet, "data") else bytes(packet)
        return self._frames(self._ctx.parse(data))

    def flush(self) -> List[np.ndarray]:
        """Return frames still buffered inside the decoder. Ends the stream."""
        return self._frames(self._ctx.parse(None)) + self._frames([None])

    def _frames(self, packets) -> List[np.ndarray]:
        frames = []
        for p in packets:
            for frame in self._ctx.decode(p):
                frames.append(frame.to_ndarray(format="rgb24"))
        return frames
