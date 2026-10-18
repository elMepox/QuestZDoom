# 3D weapon models in the hand (patch 0020)

In the headset the weapon is held by the controller. A weapon that has a MODELDEF entry is drawn as a 3D model at the
controller's pose (right hand; the left hand for the dual-wield layers). Weapons without one stay flat sprites.

## Making a model pack

Standard GZDoom/UZDoom `MODELDEF` (IQM, OBJ, MD3; not glTF, convert it in Blender). What is different in the headset:

- **Units.** One model unit is one meter, so a Blender model in meters needs `Scale 1 1 1`. `vr_weapon_model_scale`
  scales every hand-held model on top of that.
- **Frame.** Origin = the controller position. The model's forward points along the controller's aim (the direction
  the shots go). For OBJ the loader expects +x forward, +y up, +z left. MODELDEF `Offset`, `AngleOffset`,
  `PitchOffset` and `RollOffset` move and turn the model relative to the controller.
- **Tilt.** `vr_weapon_model_pitch` (degrees) tips every model up or down from the aim direction.
- **Roll.** A model follows the full controller rotation, wrist roll included. `vr_weapon_model_roll 0` keeps it upright (like the sprite, `vr_weapon_roll`).
- **Raise/lower.** The engine's weapon raise/lower tilt still applies.

## Test pack

`testmods/model-pistol/` builds `doomxr-model-pistol.pk3` (`python3 make_pk3.py`): a generated box-built pistol
that replaces the Doom pistol. It has no third-party assets. Put the pk3 into the app's `mods` folder
(`/sdcard/Android/data/io.github.elmepox.doomxr/files/mods`), pick it in the panel with a Doom IWAD and start a game.
The pistol should sit in the right hand with its barrel along the controller's aim and fire from the muzzle.

## Status

Patch 0020 applies on top of 0001-0013 and the touched files pass a clang syntax check. It has not been run in the
headset: whether the model faces the right way, its size and whether GLES draws it correctly are still to confirm.

## Hands

Each controller also gets a hand built from boxes (`app/src/main/cpp/xr/xr_hand_model.h`, no assets). The trigger curls the
index finger, the grip curls the other three and the thumb; at rest the hand is half closed, as if holding a weapon.
Weapon models and hands share one depth buffer, so a pistol sits inside the fingers.

Cvars (all saved): `vr_hand_model` (on/off), `vr_hand_scale`, `vr_hand_offset_right/up/forward` (palm position from the
controller's aim pose, meters; right is mirrored for the left hand), `vr_hand_pitch` (degrees), `vr_hand_color` (0xRRGGBB).
The offsets are a first guess and want tuning on the headset.
