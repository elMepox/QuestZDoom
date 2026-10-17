#!/usr/bin/env bash
# Applies patches/uzdoom/*.patch to the external/UZDoom submodule. Safe to run repeatedly.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
target="$root/external/UZDoom"
[ -f "$target/CMakeLists.txt" ] || { echo "UZDoom submodule missing: run 'git submodule update --init --recursive'" >&2; exit 1; }
shopt -s nullglob
for p in "$root"/patches/uzdoom/*.patch; do
  if git -C "$target" apply --check "$p" 2>/dev/null; then
    git -C "$target" apply "$p"
    echo "applied $(basename "$p")"
  elif git -C "$target" apply --check --reverse "$p" 2>/dev/null; then
    echo "already applied $(basename "$p")"
  else
    echo "ERROR: $(basename "$p") neither applies nor is already applied" >&2
    exit 1
  fi
done
