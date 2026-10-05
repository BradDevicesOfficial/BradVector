"""TIFA-SDK reference — graph model, AutoNeuroStream partition.

A small, honest graph DSL. Layers are `LayerSpec` nodes; a graph is a
dict of `name -> LayerSpec`. AutoNeuroStream assigns each layer a
silicon device purely from its type — the same rule table the hardware
documentation describes (BRADCVT_DRIVERS.md §4):
  kernel-heavy layers  -> BradNeuro
  dot-product / linear -> BradVector
  fused elementwise    -> BradFx
and MoE layers get an expert spooler bound to them.
"""

from __future__ import annotations

from ._errors import TifaError

import enum
from typing import Union


class Device(enum.Enum):
    BRADNEURO = "brad-neuro"   # BradNeuro tensor cores (HB subsystems, Atlas AUC)
    BRADVECTOR = "brad-vector" # BradVector wide-vector math (BVX tensile ops)
    BRADFX = "brad-fx"         # BradFx fused activations / elementwise
    BRADCPU = "brad-cpu"       # BradCore scalar fallback
    HOST = "host"              # CPU in the DevKit, not on-device

    def __str__(self):  # pragma: no cover - ergonomics
        return self.value


class LayerKind(enum.Enum):
    CONV2D = "conv2d"
    LINEAR = "linear"
    MOE = "moe"
    ATTENTION = "attention"
    ACT = "act_kind"
    POOL = "pool"


class LayerSpec:
    """Graph node: a layer kind plus its tensor geometry."""

    __slots__ = ("kind", "shape", "experts", "heads", "act_kind")

    def __init__(self, kind: LayerKind, shape: tuple, experts: int = 0,
                 heads: int = 0, act_kind: str = ""):
        self.kind = kind
        self.shape = shape
        self.experts = experts
        self.heads = heads
        self.act_kind = act_kind

    # conveniences for graph builders
    @classmethod
    def conv2d(cls, cin: int, cout: int, k: int = 3) -> "LayerSpec":
        return cls(LayerKind.CONV2D, (cin, cout, k, k))

    @classmethod
    def linear(cls, nin: int, nout: int) -> "LayerSpec":
        return cls(LayerKind.LINEAR, (nin, nout))

    @classmethod
    def moe(cls, nin: int, hidden: int, experts: int) -> "LayerSpec":
        return cls(LayerKind.MOE, (nin, hidden), experts=experts)

    @classmethod
    def attention(cls, nin: int, heads: int) -> "LayerSpec":
        return cls(LayerKind.ATTENTION, (nin, nin), heads=heads)

    @classmethod
    def act(cls, kind: str = "relu") -> "LayerSpec":
        return cls(LayerKind.ACT, (0,), act_kind=kind)

    def __repr__(self):  # pragma: no cover - display
        return f"<LayerSpec {self.kind.value} {self.shape} ex={self.experts} hd={self.heads} {self.act_kind}>"


Graph = Union[dict, "Model"]

_DEVICE_RULE = {
    LayerKind.CONV2D: Device.BRADNEURO,
    LayerKind.ATTENTION: Device.BRADNEURO,
    LayerKind.MOE: Device.BRADNEURO,
    LayerKind.LINEAR: Device.BRADVECTOR,
    LayerKind.POOL: Device.BRADVECTOR,
    LayerKind.ACT: Device.BRADFX,
}


class Model:
    """Reference model container (also torch-importable shape)."""

    def __init__(self, layers: dict | None = None):
        self.layers: dict = layers or {}

    def add(self, name: str, spec: LayerSpec) -> "Model":
        self.layers[name] = spec
        return self


class AutoNeuroStream:
    """Static partitioner: layer type -> silicon device."""

    @staticmethod
    def assign(spec: LayerSpec) -> Device:
        return _DEVICE_RULE[spec.kind]

    @classmethod
    def partition(cls, graph) -> "OptimizedModel":
        from .optimize import OptimizedModel
        layers = graph.layers if isinstance(graph, Model) else graph
        if not isinstance(layers, dict) or not layers:
            raise TifaError("graph must be {name: LayerSpec} or a Model")
        node = OptimizedModel()
        for name, spec in layers.items():
            if not isinstance(spec, LayerSpec):
                raise TifaError(f"{name!r} is not a LayerSpec")
            node.assignments[name] = cls.assign(spec)
            node.specs[name] = spec
            if spec.kind is LayerKind.MOE:
                node.spooled[name] = spec.experts
        return node