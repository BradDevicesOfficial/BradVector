# TIFA-SDK API — reference (Python)

Python-first AI toolchain for the Brad silicon family. Mirrors
`BRADCVT_DRIVERS.md` §4 and §5 (Rise of the Streets AI tick uses the
same `tifa` package). Stdlib-only reference; kernel execution targets
are [Gen1].

## `import tifa`

### `tifa.optimize(model, dtype="fp8", top_k=2) -> OptimizedModel`

One-line rewrite. Takes a graph built from `LayerSpec` (or a dict in
`{"layer_name": LayerSpec}` form) and returns an `OptimizedModel`:
every layer has a device assignment, every quantization-eligible tensor
has a `dtype`, and MoE layers have a spooler bound to them.

```python
model = {
    "stem":   LayerSpec.conv2d(3, 16, 3),
    "mx1":    LayerSpec.linear(16, 1024),
    "moe":    LayerSpec.moe(1024, 2048, experts=16),
    "out":    LayerSpec.linear(2048, 10),
}
opt = tifa.optimize(model)
opt.dtype            # -> "fp8"
opt.assignments      # -> {"stem": Device.BRADVECTOR, "mx1": Device.BRADVECTOR, ...}
```

> [Gen1] accept real `torch.nn.Module` directly. The reference accepts
> the `LayerSpec` graph because the torch lowering table ships with the
> kernel library — we will not fake an import-time `ModelNotFoundError`
> dance around an empty promise.

### `tifa.Device` (enum)

`BRADNEURO` (tensor cores), `BRADVECTOR` (wide vector math), `BRADFX`
(fused elementwise/activation). Also `BRADCPU` (fallback) and `HOST`.

### `tifa.LayerSpec`

Namedtuple-ish graph node with device hints.

| Factory | Shape | Device suffix |
|---------|-------|---------------|
| `.conv2d(cin, cout, k)` | `(cin, cout, k, k)` | BNV (BradNeuro) |
| `.linear(nin, nout)` | `(nin, nout)` | BVX-tensile (BradVector) |
| `.moe(nin, hidden, experts)` | `(nin, hidden, experts)` | BNV w/ MoE spooler |
| `.attention(nin, heads)` | `(nin, nheads)` | BNV |
| `.act(kind)` | scalar | BFX (BradFx) |

### `tifa.AutoNeuroStream`

Static. Partitions a graph across BradNeuro / BradVector / BradFx by
layer type (conv/attention/moe → BNV, linear → BVX, elementwise/activation
→ BFX). `.partition(model)` returns an `OptimizedModel`.

### `tifa.MoESpooler(top_k: int)`

Expert prefetch router: `route(weights)` takes an expert-weight matrix
`(experts, hidden)` and returns the top-`top_k` expert rows plus their
scores — the "active experts to HB-AIM, cold ones to SPMP" decision the
silicon spec calls the MoE spooler.

### `tifa.Quantizer(dtype, mode="dynamic")`

Wraps the converters for tensors (lists of floats). `mode="dynamic"`
uses tensor `abs-max` as the range (HB-AIM style, no calibration);

`mode="static", scale=...` uses a supplied scale. `.quantize(f, dims)`.

## Digit formats

`tifa` F8/F4 encode/decode are exact and spec-correct:

| dtype | bits | layout | notes |
|-------|------|--------|-------|
| `fp8` | 1+4+3 | E4M3FN | `0b0_1111_xxx` = NaN (`0x7F`), `0b0_1111_000` = +Inf |
| `fp8_e5m2` | 1+5+2 | E5M2 | `0b0_11111_00` = +Inf, `0b0_11111_xx` xx>0 = NaN |
| `fp4` | 1+2+1 | E2M1 | values ±{0, 0.5, 0.75, 1, 1.5, 2, 3} |
| `int4` | 1+3 | two's complement | range [−8, 7], zero included |
| `int8` | 1+7 | two's complement | range [−128, 127] |

## Errors

`tifa.TifaError` (subclass of `ValueError`) for unknown dtype, empty
expert matrices, malformed graphs, and unquantizable inputs.

## Extras

- `tifa.BradSense · DSL`: planned call
  `tifa.neural_render(...)` sharing tensor space with inference
  (`BradSense Interop`, §4 last row) — documented in the RTS SDK spec,
  listed here as [Gen1].
- `tifa.TIFA Studio` ([Gen1]): GUI model optimizer + profiler + deploy;
  reference API returns an `OptimizationReport` with per-layer device /
  dtype counts (see `examples/bench.py`).