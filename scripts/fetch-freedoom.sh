#!/usr/bin/env bash
# Downloads Freedoom (BSD-licensed IWADs) and extracts freedoom1.wad / freedoom2.wad into <outdir>.
# usage: scripts/fetch-freedoom.sh <outdir>      (idempotent; gradle runs this automatically)
set -euo pipefail
VERSION="0.13.0"
SHA256="3f9b264f3e3ce503b4fb7f6bdcb1f419d93c7b546f4df3e874dd878db9688f59"
out="${1:?usage: $0 <outdir>}"
if [ -f "$out/freedoom1.wad" ] && [ -f "$out/freedoom2.wad" ]; then exit 0; fi
mkdir -p "$out"
tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT
url="https://github.com/freedoom/freedoom/releases/download/v${VERSION}/freedoom-${VERSION}.zip"
echo "Downloading $url"
curl -fL --retry 3 -o "$tmp/freedoom.zip" "$url"
if command -v sha256sum >/dev/null; then got="$(sha256sum "$tmp/freedoom.zip" | cut -d' ' -f1)"; else got="$(shasum -a 256 "$tmp/freedoom.zip" | cut -d' ' -f1)"; fi
[ "$got" = "$SHA256" ] || { echo "Freedoom checksum mismatch: $got" >&2; exit 1; }
unzip -q -j "$tmp/freedoom.zip" "freedoom-${VERSION}/freedoom1.wad" "freedoom-${VERSION}/freedoom2.wad" -d "$out"
echo "Freedoom ${VERSION} ready in $out"
