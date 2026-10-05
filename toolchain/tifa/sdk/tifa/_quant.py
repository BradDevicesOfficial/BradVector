"""TIFA-SDK reference — FP8/FP4/INT4 digit formats (exact, stdlib-only).

Spec-correct encodings/decodings for the BradNeuro numerics:
  FP8 E4M3FN, FP8 E5M2, FP4 E2M1, INT4, INT8.
Pure Python over IEEE-754 float32 via struct — no numpy.

FP8 E4M3FN: sign 1, exp 4, mant 3, bias 7.
  exponent 0b1111 (15) reserved: mant==000 -> +-Inf, else NaN.
  max finite normal = (1 + 7/8) * 2^(14-7) = 240.

FP8 E5M2:  sign 1, exp 5, mant 2, bias 15.
  exponent 0b11111 (31) reserved: mant==00 -> +-Inf, else NaN.
  max finite normal = (1 + 3/4) * 2^(30-15) = 57344.

FP4 E2M1:  sign 1, exp 2, mant 1, bias 1.
  values: 0, +-0.5, +-0.75, +-1, +-1.5, +-2, +-3.
  (the NVIDIA-compatible E2M1 set; subnormals cover 0.5/0.75.)
"""

from __future__ import annotations

from ._errors import TifaError

import math
import struct
from typing import Callable

_E4M3 = (1, 4, 3, 7)
_E5M2 = (1, 5, 2, 15)


def _encode_float(f: float, fmt: tuple[int, int, int, int]) -> int:
    """float -> encoded bits in a custom format (round-to-nearest-even)."""
    sign, exp, mant, bias = fmt
    if math.isnan(f):
        return (0x7F << (mant + 1)) | (1 << mant)  # quiet NaN
    if math.isinf(f):
        base = 0x7F << (mant + 1)
        return base if f > 0 else base | (0x80 << (exp + mant))
    b = 1 if f < 0 else 0
    a = abs(f)
    if a == 0.0:
        return 0 if f >= 0 else 0x80 << (exp + mant)
    maxf = (1 + (1 - 2 ** -mant)) * 2 ** ((2 ** exp - 2) - bias)
    if a >= maxf:
        e = (2 ** exp) - 2
        return (b << (exp + mant)) | (e << mant) | ((1 << mant) - 1)
    frac, e2 = math.frexp(a)          # a = frac * 2**e2, frac in [0.5, 1)
    e_unbiased = e2 - 1
    e_field = e_unbiased + bias
    if e_field <= 0:                  # subnormal
        step = 2 ** (-bias - mant)
        m = round(a / step)
        if m >= 1 << mant:            # rounded up to smallest normal
            return (b << (exp + mant)) | (1 << mant)
        return (b << (exp + mant)) | int(m)
    m = round((frac - 0.5) * 2 ** (mant + 1))
    if m >= 1 << mant:                # mantissa overflow -> bump exponent
        e_field += 1
        m = 0
        if e_field >= (1 << exp) - 1:
            e_field = (1 << exp) - 2
            m = (1 << mant) - 1
    return (b << (exp + mant)) | (int(e_field) << mant) | int(m)


def _decode_float(bits: int, fmt: tuple[int, int, int, int]) -> float:
    sign, exp, mant, bias = fmt
    s = -1.0 if (bits >> (exp + mant)) & 1 else 1.0
    e = (bits >> mant) & (2 ** exp - 1)
    m = bits & (2 ** mant - 1)
    if e == 2 ** exp - 1:
        if m:
            return float("nan")
        return s * float("inf")
    if e == 0:
        return s * (m * 2 ** -mant) * 2 ** (-bias + 1)
    return s * (1 + m * 2 ** -mant) * 2 ** (e - bias)


_FP4_MAG = {0b0000: 0.0, 0b0001: 0.5, 0b0010: 0.75, 0b0011: 1.0,
            0b0100: 1.5, 0b0101: 2.0, 0b0110: 3.0}


def _fp4_encode(f: float) -> int:
    if math.isnan(f) or math.isinf(f):
        raise OverflowError("FP4 has no Inf/NaN; clamp or mask before quantize")
    a = abs(f)
    if a == 0.0:
        return 0 if f >= 0 else 0b1000
    v = min(_FP4_MAG.values(), key=lambda x: abs(x - a))
    code = [k for k, m in _FP4_MAG.items() if m == v][0]
    return code if f >= 0 else code | 0b1000


def _fp4_decode(bits: int) -> float:
    mag = _FP4_MAG[bits & 0b0111]
    return mag if not (bits & 0b1000) else -mag


def _int_encode(bits: int) -> Callable[[float], int]:
    lo, hi = -(2 ** (bits - 1)), 2 ** (bits - 1) - 1

    def enc(f: float) -> int:
        v = int(round(f))
        return min(hi, max(lo, v)) & ((1 << bits) - 1)

    return enc


def _int_decode(bits: int) -> Callable[[int], float]:
    n = 1 << (bits - 1)

    def dec(b: int) -> float:
        return float(b if b < n else b - (1 << bits))

    return dec


class Quantizer:
    """Tensor quantizer: dynamic range (HB-AIM style) or static scale.

    dtype: 'fp8' | 'fp8_e5m2' | 'fp4' | 'int4' | 'int8'
    mode:  'dynamic' (scale from tensor abs-max) | 'static' (scale=...)
    """

    __slots__ = ("dtype", "mode", "scale", "_enc", "_dec", "_bits")

    def __init__(self, dtype: str, mode: str = "dynamic", scale: float | None = None):
        if dtype not in ("fp8", "fp8_e5m2", "fp4", "int4", "int8"):
            raise TifaError(f"unknown tifa dtype {dtype!r}")
        self.dtype = dtype
        self.mode = mode
        self.scale = scale
        if dtype == "fp4":
            self._enc, self._dec, self._bits = _fp4_encode, _fp4_decode, 4
        elif dtype in ("int4", "int8"):
            n = int(dtype[3:])
            self._enc, self._dec, self._bits = _int_encode(n), _int_decode(n), n
        else:
            fmt = _E4M3 if dtype == "fp8" else _E5M2
            self._enc = lambda f, fmt=fmt: _encode_float(f, fmt)
            self._dec = lambda b, fmt=fmt: _decode_float(b, fmt)
            self._bits = 8

    def quantize(self, values):
        """values: iterable of floats -> list of encoded ints + decode + scale."""
        if self.mode == "dynamic":
            scale = max((abs(v) for v in values), default=0.0)
            self.scale = scale
        scale = self.scale or 1.0
        out = [self._enc(v / scale) for v in values]
        return out, self._dec, scale


def encode_float(value, dtype="fp8") -> int:
    q = Quantizer(dtype, mode="static", scale=1.0)
    return q.quantize([value])[0][0]


def decode_bits(bits, dtype="fp8") -> float:
    q = Quantizer(dtype, mode="static", scale=1.0)
    return q._dec(bits)