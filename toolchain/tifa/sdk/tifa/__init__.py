"""tifa — TIFA AI toolchain reference SDK.

Python-first AI toolchain for the Brad silicon family. See README.md /
api.md for the full reference and its honest boundaries.

Usage:
    import tifa
    model = tifa.optimize(model)
    tifa.Quantizer("fp8").quantize(...)
    tifa.MoESpooler(2).route(weights)
"""

from ._errors import TifaError as TifaError
from ._graph import Device, LayerKind, LayerSpec, Model, AutoNeuroStream
from ._moe import MoESpooler
from ._quant import Quantizer, encode_float, decode_bits
from .optimize import OptimizedModel, optimize


__version__ = "0.1.0-reference"
__all__ = [
    "TifaError",
    "Device",
    "LayerKind",
    "LayerSpec",
    "Model",
    "AutoNeuroStream",
    "MoESpooler",
    "Quantizer",
    "encode_float",
    "decode_bits",
    "OptimizedModel",
    "optimize",
]