# SAM 3 Ungated Weights Acquisition (ADR — graph MCP offline at write time)

Date: 2026-11-10

## Context
Official facebook/sam3 HF repos are gated behind Meta's license acceptance
form. User declined the form; alternatives were researched and applied.

## Decision
1. **Checkpoint**: un-gated mirror `1038lab/sam3` rehosts the *official*
   `sam3.pt` byte-for-byte — downloaded to git-ignored
   `models/sam3/sam3.pt` and sha256-verified:
   `9999e2341ceef5e136daa386eecb55cb414446a00ac2b55eb2dfd2f7c3cf8c9e`
   (3,450,062,241 bytes). `sam3_live/fetch_weights.sh` automates this.
2. **Loading**: official `build_sam3_predictor(checkpoint_path=...)` with
   the local file — no `hf auth login`, no form, no runtime network.
   Code repo cloned to git-ignored `.third_party/sam3`, pip `-e` install
   (manual extra deps: einops iopath hydra-core triton pycocotools psutil
   timm pandas submitit).
3. **Live adaptation**: OSS predictor has no per-frame append API →
   `Sam3Tracker` runs sliding-window sessions (default 48 frames) over an
   in-memory PIL buffer; previous-window final boxes are re-fed as prompts
   for cross-window ID continuity. Score threshold + top-N filter handle
   the dedup-free path.
4. **Fallbacks**: missing checkpoint → clean error naming the mirror URL;
   CPU-only host → clean error suggesting `--tracker legacy` (official
   code requires CUDA — verified on this CPU-only dev box).

## Open items
- Dev box has no NVIDIA GPU: end-to-end inference must be validated on
  the GPU box (imports + checkpoint torch.load verified here).
- SAM 3.1 `.pt` (multiplex) has no official-layout un-gated mirror yet
  (unsloth/sam3.1 is transformers-format); default remains SAM 3.0 via
  `--sam-version sam3`.
- Sync this record to the codebase-memory graph ADR when MCP reconnects.
