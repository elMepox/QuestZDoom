#!/usr/bin/env python3
"""Builds doomxr-model-weapons-ps1.pk3: a MODELDEF pack that swaps all nine Doom 1/2 weapons for animated PS1-style
3D models.

The models come from models/doom_weapons_ps1/doom_weapons_ps1.blend (in the parent custom_doom_for_vr folder); its
text block ps1_doom_export.py writes models/<weapon>/:
  <weapon>.md3        the weapon, one MD3 frame per sprite frame (recoil, slide, pump, barrel break, chaingun spin,
                      chainsaw shake), with one baked 15-bit-colour atlas <weapon>.png
  <weapon>_flash.md3  the muzzle flash for the flash layer, one frame per flash sprite frame, skin flash.png
MD3 units are centimetres (Scale 0.01 turns them into meters); the origin is the grip, so it sits at the
controller position. The engine interpolates between frames unless the block says NoInterpolation.
Usage: make_pk3.py [output.pk3]
"""
import os, sys, zipfile

HERE = os.path.dirname(os.path.abspath(__file__))

# weapon class -> (model folder, weapon sprite frames, flash sprite frames, flags of the weapon model)
# Frame letters are in the order of the MD3 frames.
WEAPONS = [
    ("Fist",           "fist",           [("PUNG", "ABCD")],       [],                  []),
    ("Chainsaw",       "chainsaw",       [("SAWG", "ABCD")],       [],                  ["NoInterpolation"]),
    ("Pistol",         "pistol",         [("PISG", "ABCDE")],      [("PISF", "A")],     ["NoInterpolation"]),
    ("Shotgun",        "shotgun",        [("SHTG", "ABCD")],       [("SHTF", "AB")],    []),
    ("SuperShotgun",   "supershotgun",   [("SHT2", "ABCDEFGH")],   [("SHT2", "IJ")],    []),
    ("Chaingun",       "chaingun",       [("CHGG", "AB")],         [("CHGF", "AB")],    ["NoInterpolation"]),
    ("RocketLauncher", "rocketlauncher", [("MISG", "AB")],         [("MISF", "ABCD")],  []),
    ("PlasmaRifle",    "plasmarifle",    [("PLSG", "AB")],         [("PLSF", "AB")],    []),
    ("BFG9000",        "bfg9000",        [("BFGG", "AB")],         [("BFGF", "AB")],    []),
]


def block(cls, folder, model, skin, sprites, flags):
    frames, n = [], 0
    for s, letters in sprites:
        for f in letters:
            frames.append("\tFrameIndex %s %s 0 %d" % (s, f, n))
            n += 1
    lines = [f'Model "{cls}"', "{", f'\tPath "models/{folder}"', f'\tModel 0 "{model}"', f'\tSkin 0 "{skin}"',
             "\tScale 0.01 0.01 0.01"] + ["\t" + fl for fl in flags] + frames + ["}", ""]
    return "\n".join(lines)


def modeldef():
    out = []
    for cls, folder, sprites, flash, flags in WEAPONS:
        out.append(block(cls, folder, f"{folder}.md3", f"{folder}.png", sprites, flags))
        if flash:   # a second block: the flash layer draws only the flash, full bright from its Bright states
            out.append(block(cls, folder, f"{folder}_flash.md3", "flash.png", flash, ["NoInterpolation"]))
    return "\n".join(out)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "doomxr-model-weapons-ps1.pk3")
    stamp = (2026, 10, 19, 0, 0, 0)
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr(zipfile.ZipInfo("MODELDEF", stamp), modeldef())
        for _, folder, _, flash, _ in WEAPONS:
            files = [f"{folder}.md3", f"{folder}.png"] + ([f"{folder}_flash.md3", "flash.png"] if flash else [])
            for name in files:
                with open(os.path.join(HERE, "models", folder, name), "rb") as f:
                    z.writestr(zipfile.ZipInfo(f"models/{folder}/{name}", stamp), f.read())
    print("wrote", out)


if __name__ == "__main__":
    main()
