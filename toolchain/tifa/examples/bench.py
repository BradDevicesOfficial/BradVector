#!/usr/bin/env python3
"""TIFA-SDK reference demo — build a model, optimize it, spool and quantize.

Run:  python3 src/tifa/examples/bench.py
"""

import sys

sys.path.insert(0, "/storage/self/primary/Project/Brad Devices/src/tifa/sdk")

from tifa import (
    AutoNeuroStream, Device, LayerSpec, MoESpooler,
    Quantizer, TifaError, decode_bits, encode_float, optimize,
)


def main():
    print("TIFA-SDK reference demo (stdlib-only, no silicon behind it)\n")

    # 1. one-line PyTorch-style rewrites start from a LayerSpec graph
    model = {
        "stem":    LayerSpec.conv2d(3, 16, 3),
        "mx0":     LayerSpec.linear(16, 256),
        "moe":     LayerSpec.moe(256, 1024, experts=16),
        "attn":    LayerSpec.attention(1024, 8),
        "relu":    LayerSpec.act("relu"),
        "out":     LayerSpec.linear(1024, 10),
    }
    opt = optimize(model, dtype="fp8", top_k=2)
    for name, dev in opt.assignments.items():
        print(f"  assign  {name:8s} -> {dev}")
    report = opt.report()
    print(f"  report  {report}")
    assert opt.spooled == {"moe": 16}
    assert opt.assignments["out"] is Device.BRADVECTOR

    # 2. expert spooler: active vs cold (the MoE decision)
    weights = [[float(w) for w in range(4)] for _ in range(16)]  # 16 experts x 4 dims
    spool = opt.spooler.route(weights)
    assert len(spool["active"]) == 2
    print(f"\n  moe     active={sorted(spool['active'])} cold={len(spool['cold'])} "
          f"resident={spool['resident_experts']}")

    # 3. quantization without calibration (HB-AIM dynamic range)
    tensor = [1.0, -1.0, 0.5, 128.0, -255.0, 3.75, 1e-5]
    for dtype in ("fp8", "fp8_e5m2", "fp4", "int4", "int8"):
        q = Quantizer(dtype)
        enc, dec, scale = q.quantize(tensor)
        back = [round(dec(b) * scale, 5) for b in enc]
        print(f"  quant   {dtype:8s} scale={scale:8.2f}  {back}")

    # exact spot checks (spec-correct, not fuzzy "looks right" numbers)
    assert encode_float(1.0, "fp8") == 0b00111000, "fp8 1.0"
    assert encode_float(240.0, "fp8") == 0b01110111, "fp8 max finite = 240"
    assert encode_float(57344.0, "fp8_e5m2") == 0b01111011, "e5m2 max = 57344"
    assert encode_float(3.0, "fp4") == 0b0110, "fp4 max = 3"
    assert decode_bits(0b1110, "fp4") == -3.0, "fp4 -3"
    assert encode_float(-5, "int4") == 0b1011, "int4 -5"

    print("\n  spot checks ok: FP8 E4M3/E5M2, FP4 E2M1, INT4 — spec-correct")
    print("\nTIFA-SDK reference demo complete.")


if __name__ == "__main__":
    main()