#!/usr/bin/env bash
# Fetch the official SAM 3 checkpoint from an un-gated mirror.
# No HuggingFace account, license form, or auth token required.
set -euo pipefail
cd "$(dirname "$0")/.."

mkdir -p models/sam3
URL="https://huggingface.co/1038lab/sam3/resolve/main/sam3.pt"
OUT="models/sam3/sam3.pt"
SHA="9999e2341ceef5e136daa386eecb55cb414446a00ac2b55eb2dfd2f7c3cf8c9e"

if [ -f "$OUT" ] && echo "$SHA  $OUT" | sha256sum -c --quiet 2>/dev/null; then
    echo "$OUT already present and verified."
    exit 0
fi

echo "Downloading official sam3.pt (3.4 GB) from un-gated mirror..."
wget -c -O "$OUT.tmp" "$URL"
mv "$OUT.tmp" "$OUT"

echo "$SHA  $OUT" | sha256sum -c -
echo "Done: $OUT"
