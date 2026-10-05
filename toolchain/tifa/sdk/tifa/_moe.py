"""TIFA-SDK reference — MoE expert spooler.

Emulates the siliconside MoE Spooler decision (BRADCVT_DRIVERS.md §4):
given an expert-weight matrix, route per-token top-k experts to HB-AIM
(active cache) and mark the tail "cold" for SPMP streaming. The math is
real; the DMA to HB-AIM is not (no silicon here), so `route()` returns
the decision, not a memory transfer.
"""

from __future__ import annotations

from ._errors import TifaError

import heapq


class MoESpooler:
    def __init__(self, top_k: int = 2, cache_capacity: int = 8):
        if top_k < 1 or top_k > 8:
            raise TifaError("top_k must be in 1..8")
        self.top_k = top_k
        self.cache_capacity = cache_capacity
        self.cache = {}          # expert_idx -> hit count
        self.policy = "topk-absmax"

    def route(self, weights, token_gate=None):
        """weights: (experts, hidden) rows of float expert vectors.

        token_gate: optional per-token logits (tokens, experts) to route
                    gate-decisions; if None, uses row abs-max.
        Returns dict: active (top_k), cold (else), scores per expert.
        """
        if weights is None or len(weights) == 0:
            raise TifaError("expert matrix is empty")
        experts = len(weights)
        if token_gate is not None:
            scores = [float(max(abs(g) for g in row)) for row in token_gate]
            # aggregate across tokens by max gate value per expert
            colmax = [0.0] * experts
            for row in token_gate:
                for j, g in enumerate(row):
                    if abs(g) > colmax[j]:
                        colmax[j] = abs(float(g))
            scores = colmax
        else:
            scores = [float(max(abs(x) for x in row)) for row in weights]
        ranked = heapq.nlargest(self.top_k, range(experts), key=lambda i: scores[i])
        active = {i: scores[i] for i in ranked}
        cold = {i: scores[i] for i in range(experts) if i not in active}
        for i in ranked:
            self.cache[i] = self.cache.get(i, 0) + 1
        if len(self.cache) > self.cache_capacity:
            for i in [i for i, _ in sorted(self.cache.items(), key=lambda kv: kv[1])]:
                if len(self.cache) <= self.cache_capacity:
                    break
                del self.cache[i]
        return {"active": active, "cold": cold, "resident_experts": list(self.cache)}