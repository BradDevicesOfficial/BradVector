"""TIFA-SDK reference — `tifa.optimize(model)` pipeline.

One-line rewrite: partition (AutoNeuroStream), bind spoolers to MoE
layers, and mark quantization-eligible tensors. Returns an
OptimizedModel with `assignments`, `specs`, `spooled`, and a report.
"""

from __future__ import annotations

from ._errors import TifaError
from ._graph import AutoNeuroStream, Device, LayerKind, LayerSpec, Model
from ._moe import MoESpooler


class OptimizedModel:
    """Result of optimize(): device assignments + quantize plan + report."""

    def __init__(self, dtype: str = "fp8"):
        self.dtype = dtype
        self.assignments: dict = {}
        self.specs: dict = {}
        self.spooled: dict = {}   # layer name -> expert count
        self.spooler = MoESpooler()

    # -- report plumbing (TIFA Studio reference) -----------------
    def report(self) -> dict:
        counts = {}
        for d in self.assignments.values():
            counts[d.value] = counts.get(d.value, 0) + 1
        return {
            "dtype": self.dtype,
            "layers": len(self.assignments),
            "device_counts": counts,
            "moe_layers_spooled": len(self.spooled),
            "quantizable_layers": [
                n for n, s in self.specs.items() if s.kind in (
                    LayerKind.LINEAR, LayerKind.CONV2D, LayerKind.ATTENTION)
            ],
        }

    def __repr__(self):  # pragma: no cover - display
        return f"<OptimizedModel {self.report()}>"


def optimize(model, dtype: str = "fp8", top_k: int = 2) -> OptimizedModel:
    """`model = tifa.optimize(model)` — the one-liner from the spec."""
    if dtype not in ("fp8", "fp8_e5m2", "fp4", "int4", "int8"):
        raise TifaError(f"optimize: unknown dtype {dtype!r}")
    node = AutoNeuroStream.partition(model)
    node.dtype = dtype
    node.spooler = MoESpooler(top_k=top_k)
    return node