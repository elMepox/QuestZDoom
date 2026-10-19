#!/usr/bin/env python3
"""Builds doomxr-model-pistol.pk3: a test MODELDEF pack that swaps the Doom pistol for a small 3D model.

The model is generated here (boxes with a flat-colour palette texture), so there are no third-party assets.
Model space as UZDoom's OBJ loader expects it: +x forward (barrel), +y up, +z left; 1 unit = 1 meter.
Usage: make_pk3.py [output.pk3]
"""
import struct, sys, zipfile, zlib

# palette texel index -> colour
PALETTE = [(58, 60, 66), (28, 28, 30), (96, 64, 40), (150, 150, 158)]


def png(width, height, pixels):
    raw = b"".join(b"\x00" + bytes(c for px in pixels[y * width:(y + 1) * width] for c in px) for y in range(height))
    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def box(x0, x1, y0, y1, z0, z1, tex, verts, uvs, faces):
    """Axis-aligned box; (x0, x1) etc. are given for the top/front, bottom x may be shifted by the caller afterwards."""
    base = len(verts)
    for x in (x0, x1):
        for y in (y0, y1):
            for z in (z0, z1):
                verts.append((x, y, z))
    u = (tex + 0.5) / len(PALETTE)
    uvs.append((u, 0.5))
    uv = len(uvs)
    idx = lambda ix, iy, iz: base + ix * 4 + iy * 2 + iz + 1
    quads = [((0, 0, 0), (0, 1, 0), (0, 1, 1), (0, 0, 1)),  # back
             ((1, 0, 0), (1, 0, 1), (1, 1, 1), (1, 1, 0)),  # front
             ((0, 0, 0), (0, 0, 1), (1, 0, 1), (1, 0, 0)),  # bottom
             ((0, 1, 0), (1, 1, 0), (1, 1, 1), (0, 1, 1)),  # top
             ((0, 0, 0), (1, 0, 0), (1, 1, 0), (0, 1, 0)),  # side z0
             ((0, 0, 1), (0, 1, 1), (1, 1, 1), (1, 0, 1))]  # side z1
    for q in quads:
        faces.append([(idx(*c), uv) for c in q])


def build_obj():
    verts, uvs, faces = [], [], []
    box(-0.005, 0.20, 0.020, 0.058, -0.012, 0.012, 0, verts, uvs, faces)   # slide
    box(0.000, 0.190, 0.004, 0.020, -0.010, 0.010, 1, verts, uvs, faces)    # frame under the slide
    grip_start = len(verts)
    box(-0.020, 0.040, -0.100, 0.020, -0.011, 0.011, 2, verts, uvs, faces)  # grip
    for i in range(grip_start, grip_start + 8):                              # lean the bottom of the grip back
        x, y, z = verts[i]
        if y < 0:
            verts[i] = (x - 0.018, y, z)
    box(0.040, 0.044, -0.010, 0.020, -0.010, 0.010, 1, verts, uvs, faces)   # trigger guard front
    box(0.200, 0.215, 0.030, 0.042, -0.004, 0.004, 3, verts, uvs, faces)    # muzzle
    out = ["# DoomXR test pistol (generated)", "mtllib pistol.mtl", "o pistol", "usemtl pistol"]
    out += ["v %.4f %.4f %.4f" % v for v in verts]
    out += ["vt %.4f %.4f" % t for t in uvs]
    out += ["f " + " ".join("%d/%d" % p for p in f) for f in faces]
    return "\n".join(out) + "\n"


MTL = "newmtl pistol\nKd 1 1 1\nmap_Kd pistol.png\n"

# One entry per state sprite frame of the vanilla pistol (PISG A-E) and its muzzle flash (PISF A).
FRAMES = "\n".join("\tFrameIndex %s %s 0 0" % (s, f) for s, f in
                   [("PISG", "A"), ("PISG", "B"), ("PISG", "C"), ("PISG", "D"), ("PISG", "E"), ("PISF", "A")])
MODELDEF = f'''Model "Pistol"
{{
	Path "models/pistol"
	Model 0 "pistol.obj"
	Skin 0 "pistol.png"
	Scale 1.0 1.0 1.0
{FRAMES}
}}
'''


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "doomxr-model-pistol.pk3"
    tex = png(len(PALETTE), 1, PALETTE)
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        for name, data in (("MODELDEF", MODELDEF), ("models/pistol/pistol.obj", build_obj()), ("models/pistol/pistol.mtl", MTL)):
            z.writestr(zipfile.ZipInfo(name, (2026, 10, 18, 0, 0, 0)), data)
        z.writestr(zipfile.ZipInfo("models/pistol/pistol.png", (2026, 10, 18, 0, 0, 0)), tex)
    print("wrote", out)


if __name__ == "__main__":
    main()
