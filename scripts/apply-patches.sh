#!/usr/bin/env bash
# Applies patches/uzdoom/*.patch (in name order) to the external/UZDoom submodule. Safe to run repeatedly:
# later patches may touch the same hunks as earlier ones, so "already applied" is decided from the newest
# patch downwards (and remembered in a stamp file inside the submodule's git dir).
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
target="$root/external/UZDoom"
[ -f "$target/CMakeLists.txt" ] || { echo "UZDoom submodule missing: run 'git submodule update --init --recursive'" >&2; exit 1; }
shopt -s nullglob
patches=("$root"/patches/uzdoom/*.patch)
[ ${#patches[@]} -gt 0 ] || exit 0

gitdir="$(git -C "$target" rev-parse --absolute-git-dir)"
stamp="$gitdir/doomxr-patches.stamp"
sum="$(cat "${patches[@]}" | { sha256sum 2>/dev/null || shasum -a 256; } | cut -d' ' -f1)-$(git -C "$target" rev-parse HEAD)"
last="${patches[${#patches[@]}-1]}"
if [ -f "$stamp" ]; then
  # A stamp is only trusted while the newest patch is really applied (the worktree may have been reset).
  if [ "$(cat "$stamp")" = "$sum" ] && git -C "$target" apply --check --reverse "$last" 2>/dev/null; then
    echo "UZDoom patches up to date"
    exit 0
  fi
  rm -f "$stamp"
fi

# Patches are applied in series order, so the applied ones form a prefix: find the highest applied patch
# and continue with the first unapplied one.
applied=-1
for ((i=${#patches[@]}-1; i>=0; i--)); do
  if git -C "$target" apply --check --reverse "${patches[$i]}" 2>/dev/null; then applied=$i; break; fi
done
for ((i=0; i<=applied; i++)); do echo "already applied $(basename "${patches[$i]}")"; done
for ((i=applied+1; i<${#patches[@]}; i++)); do
  if git -C "$target" apply --check "${patches[$i]}" 2>/dev/null; then
    git -C "$target" apply "${patches[$i]}"
    echo "applied $(basename "${patches[$i]}")"
  else
    echo "ERROR: $(basename "${patches[$i]}") does not apply" >&2
    exit 1
  fi
done
echo "$sum" > "$stamp"
