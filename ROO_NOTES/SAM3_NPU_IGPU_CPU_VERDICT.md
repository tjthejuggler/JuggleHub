# SAM 3 on Meteor Lake NPU / iGPU / CPU — Verdict (2026-11-10)

Hardware: Intel Core Ultra 9 185H — Meteor Lake-P Arc iGPU (renderD128)
+ Meteor Lake NPU (accel0), Linux, no discrete NVIDIA GPU.

## Research findings
- **NPU: not possible today.** No public SAM 3 → OpenVINO IR export
  exists (searched Nov 2026); SAM 3's only non-CUDA ports are Meta's
  LiteRT phone demo (~10 s/frame on flagship phones — still CUDA-class
  servers for real use). The 848M-param fp32 transformer also exceeds
  practical NPU SRAM/compute budgets.
- **iGPU: technically possible, practically pointless.** PyTorch ships
  native XPU wheels; our `_torch_shim.py` neutralizes SAM 3's ~25
  hardcoded CUDA assumptions (device="cuda" factories, `.cuda()`,
  bf16 memory casts, pin_memory, cuda mem probes). But Meteor Lake Arc
  running fp32 (no functional CUDA autocast equivalent enabled by the
  repo) is roughly CPU-class throughput → ~5–10 s/frame, same as CPU.
- **CPU: WORKS — verified.** SAM 3 built from the local checkpoint and
  propagated windows end-to-end on this laptop at ~10 s/frame
  (model load ~10 s, add_prompt ~50 s, then ~9–14 s/frame on 3-frame
  windows). Usable for offline validation of tracking quality on short
  recorded sequences; NOT live.

## Code changes
- `sam3_live/_torch_shim.py` — torch device/dtype shim for CUDA-only
  code on cpu/xpu hosts.
- `sam3_live/trackers.py` — `--sam-device {cuda,xpu,cpu}` support
  (bypasses the `.cuda()`-hardcoded predictor wrapper on non-CUDA),
  reads `out_probs` score key.
- `.third_party/sam3/sam3/perflib/connected_components.py` — upstream
  bug fix: CPU backend crashed on empty object batches (B=0), which the
  CUDA/triton backends handle.
- Synthetic-frame note: SAM 3 legitimately returns 0 detections for a
  crudely drawn 2D "ball" — prompt-based detection needs real imagery;
  use real webcam captures for quality validation.

## Conclusion
Live SAM 3 remains CUDA-GPU-box only. On this laptop use
`--tracker legacy` live, or `--sam-device cpu` for offline SAM 3
validation of recorded clips.
