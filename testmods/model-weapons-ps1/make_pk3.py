#!/usr/bin/env python3
"""Builds doomxr-model-weapons-ps1.pk3: a MODELDEF pack that swaps all nine Doom 1/2 weapons for PS1-style 3D models.

The models come from models/doom_weapons_ps1/doom_weapons_ps1.blend (in the parent custom_doom_for_vr folder): low-poly
meshes with one baked 15-bit-colour texture atlas each, exported to models/<weapon>/<weapon>.{obj,mtl,png}.
Model space as UZDoom's OBJ loader expects it: +x forward (barrel), +y up, +z left; 1 unit = 1 meter; the origin is
the grip, so it sits at the controller position.
Usage: make_pk3.py [output.pk3]
"""
import os, sys, zipfile

HERE = os.path.dirname(os.path.abspath(__file__))

# weapon class -> (model folder, sprite frames of its ready/fire states and muzzle flash)
WEAPONS = [
    ("Fist",           "fist",           [("PUNG", "ABCD")]),
    ("Chainsaw",       "chainsaw",       [("SAWG", "ABCD")]),
    ("Pistol",         "pistol",         [("PISG", "ABCDE"), ("PISF", "A")]),
    ("Shotgun",        "shotgun",        [("SHTG", "ABCD"), ("SHTF", "AB")]),
    ("SuperShotgun",   "supershotgun",   [("SHT2", "ABCDEFGHIJ")]),
    ("Chaingun",       "chaingun",       [("CHGG", "AB"), ("CHGF", "AB")]),
    ("RocketLauncher", "rocketlauncher", [("MISG", "AB"), ("MISF", "ABCD")]),
    ("PlasmaRifle",    "plasmarifle",    [("PLSG", "AB"), ("PLSF", "AB")]),
    ("BFG9000",        "bfg9000",        [("BFGG", "AB"), ("BFGF", "AB")]),
]


def modeldef():
    out = []
    for cls, folder, sprites in WEAPONS:
        frames = "\n".join("\tFrameIndex %s %s 0 0" % (s, f) for s, fs in sprites for f in fs)
        out.append(f'''Model "{cls}"
{{
	Path "models/{folder}"
	Model 0 "{folder}.obj"
	Skin 0 "{folder}.png"
	Scale 1.0 1.0 1.0
{frames}
}}
''')
    return "\n".join(out)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "doomxr-model-weapons-ps1.pk3")
    stamp = (2026, 10, 19, 0, 0, 0)
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr(zipfile.ZipInfo("MODELDEF", stamp), modeldef())
        for _, folder, _ in WEAPONS:
            for ext in ("obj", "mtl", "png"):
                with open(os.path.join(HERE, "models", folder, f"{folder}.{ext}"), "rb") as f:
                    z.writestr(zipfile.ZipInfo(f"models/{folder}/{folder}.{ext}", stamp), f.read())
    print("wrote", out)


if __name__ == "__main__":
    main()
