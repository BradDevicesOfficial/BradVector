# TIFA AI toolchain — reference SDK (`import tifa`)

The developer weapon that makes CUDA irrelevant for the next generation
(`BRADCVT_DRIVERS.md` §4), as a **runnable Python reference**.

```python
import tifa

model = tifa.optimize(model)          # one-line rewrite
tifa.AutoNeuroStream.partition(model)  # BNV / BVX / BFX placement
tifa.MoESpooler(top_k=2).route(moe_layer)
tifa.Quantizer("fp8").quantize(tensor)  # FP8/FP4/INT4, no calibration
```

## What this directory is

| Path | What it is |
|------|-----------|
| `api.md` | Full API reference (objects, signatures, honesty notes) |
| `sdk/tifa/` | The reference package — stdlib-only, zero deps |
| `examples/bench.py` | Runnable demo: build a model, partition, spool, quantize |

Run the demo:

```sh
python3 src/tifa/examples/bench.py
```

## Honest boundaries

- **Reference, not shipping silicon.** There is no BradNeuro tensor
  core in a datacenter to actually execute these kernels today. What is
  real: the graph partitioner, the expert-routing math, and the
  FP8/FP4/INT4 converters — all correct-to-spec and dependency-free.
- **Graph DSL, not torch (yet).** `tifa.optimize` lowers a `LayerSpec`
  graph. A PyTorch-bridge (`model = tifa.optimize(model)` accepting real
  `torch.nn.Module`) is listed in `api.md` as [Gen1] — the day the
  kernel library ships, not a stub pretending to be one today.
- **No calibration for quantization:** HB-AIM dynamic range finding
  means quantize-from-activation-range. Implemented as
  `--dynamic-range` per tensor; a static-calibration mode exists for
  honest models that need it.