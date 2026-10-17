# Curated mod catalog

`catalog.json` is downloaded by the app at start-up (raw GitHub URL), so fixing a dead link is a one-line
commit here, with no APK release. Schema (version 1) is parsed by `modcore/.../Catalog.java`:

```json
{
  "id": "unique-slug",
  "title": "Shown in the list",
  "description": "One or two sentences",
  "iwad": "doom2",
  "type": "map | gameplay | resource",
  "homepage": "https://...",
  "files": [{ "name": "file.pk3", "sha256": "hex", "size": 123, "urls": ["mirror 1", "mirror 2"] }],
  "loadOrder": ["file.pk3"],
  "args": ["+skill", "3"],
  "broken": false
}
```

- `sha256` is checked after download. Fill it in for every curated entry; only idgames hits have none.
- A `.zip` is unpacked and only loadable files (`wad pk3 pk7 ipk3 ipk7 deh bex`) are kept. To keep an
  archive intact for the engine, name it `.pk3`.
- `broken` is set by `scripts/check-catalog.py` (weekly workflow); the app shows such entries as unavailable.
