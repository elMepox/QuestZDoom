#!/usr/bin/env python3
"""Checks every catalog URL and marks entries with no working mirror as broken.

Usage: check-catalog.py catalog/catalog.json   (rewrites the file when a flag changed)
Exit status is 0 even when links are broken: the result is the updated file, not a failure.
"""
import json
import sys
import urllib.request

UA = "DoomXR-catalog-check (+https://github.com/elmepox/questzdoom)"


def alive(url):
    for method in ("HEAD", "GET"):
        try:
            req = urllib.request.Request(url, method=method, headers={"User-Agent": UA, "Range": "bytes=0-0"})
            with urllib.request.urlopen(req, timeout=30) as r:
                return r.status < 400
        except Exception:
            continue
    return False


def main(path):
    with open(path) as f:
        cat = json.load(f)
    changed = False
    for e in cat["entries"]:
        urls = [u for f in e.get("files", []) for u in f.get("urls", [])]
        ok = all(any(alive(u) for u in f.get("urls", [])) for f in e.get("files", [])) if urls else True
        if bool(e.get("broken", False)) == ok:
            e["broken"] = not ok
            changed = True
            print(("BROKEN " if not ok else "FIXED  ") + e["id"])
    if changed:
        with open(path, "w") as f:
            json.dump(cat, f, indent=2, ensure_ascii=False)
            f.write("\n")


if __name__ == "__main__":
    main(sys.argv[1])
