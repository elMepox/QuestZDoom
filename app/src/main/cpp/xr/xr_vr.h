// DoomXR OpenXR layer: a small, engine-independent C interface.
// All functions must be called from the engine/render thread that owns the GL context.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef struct DoomXrEyeView
{
	// Frustum half-extent tangents (left/down are negative, as in OpenXR's XrFovf).
	float tanLeft, tanRight, tanUp, tanDown;
	// Orientation in degrees, using Doom conventions: yaw counter-clockwise and relative to the
	// recenter direction, pitch positive = looking down, roll positive = head tilted right.
	float yaw, pitch, roll;
	// Eye position relative to the recenter origin, in meters, expressed in the recenter heading:
	// forward, right, up.
	float forward, right, up;
} DoomXrEyeView;

// Creates the OpenXR instance and session on the current EGL context. Safe to call repeatedly.
// Returns 0 (and the app keeps running as a flat window) when OpenXR is not available.
int xr_init(void);
int xr_is_initialized(void);
// Per-eye render target size (the engine uses it as its virtual screen size).
void xr_get_render_size(int *width, int *height);

// Starts the frame (xrWaitFrame/xrBeginFrame/xrLocateViews) unless already started.
// Returns non-zero when the frame must be rendered in stereo.
int xr_frame_begin(void);
// Non-zero between xr_frame_begin and xr_frame_end when stereo rendering is wanted.
int xr_frame_active(void);
// Fills the pose/projection of an eye for the current frame.
int xr_get_eye_view(int eye, DoomXrEyeView *out);
// Copies the finished image of an eye (currently in framebuffer srcFbo, size srcW x srcH) into the swapchain.
void xr_eye_blit(int eye, unsigned srcFbo, int srcW, int srcH);
// Framebuffer the flat 2D screen (menu, console, title) should be drawn into; 0 = the window.
unsigned xr_ui_target_fbo(void);
// Ends the frame. uiWanted shows the 2D screen as a floating quad. Returns non-zero when an XR frame was
// ended (the caller must then not swap the window buffers).
int xr_frame_end(int uiWanted);
// Logging helpers: xr_trace() is true for the first frames and then every 300th; xr_log writes to logcat (tag DoomXR-XR).
int xr_trace(void);
void xr_log(const char *fmt, ...);
// ---- controllers (OpenXR actions, Oculus Touch profile) ----
enum
{
	XRB_A = 1, XRB_B = 2, XRB_X = 4, XRB_Y = 8,
	XRB_LTHUMB = 16, XRB_RTHUMB = 32, XRB_MENU = 64
};
typedef struct DoomXrInput
{
	float moveX, moveY;			// left thumbstick (x right, y forward)
	float turnX, turnY;			// right thumbstick
	float trigger[2], grip[2];	// [0] = left hand, [1] = right hand
	unsigned buttons;			// XRB_* bits
	int uiHit;					// the pointing hand's ray hits the floating 2D screen
	float uiU, uiV;				// hit position on the screen, 0..1, origin top-left
	int uiHand;					// hand doing the pointing
	int hudHover, hudTouch;		// right fingertip near / touching the forearm HUD panel
	float hudU, hudV;			// touch position on the panel, 0..1, origin top-left
} DoomXrInput;
// Latest controller state (valid after the first running frame). Returns 0 while there is no session/focus.
int xr_input_poll(DoomXrInput *out);
void xr_haptic(int hand, float amplitude, float seconds);
// Head rotation not yet handed to the engine, in degrees (yaw: counter-clockwise positive; pitch: down positive).
// Taking the yaw moves the reference of xr_get_eye_view's yaw, so the renderer only adds the remaining difference.
float xr_take_head_yaw_delta(void);
float xr_take_head_pitch_delta(void);
// Next frame, treat the current head pose as "forward".
void xr_recenter(void);

// ---- controller pose for aiming and the weapon ----
// World-space pose of a hand's aim ray in Doom map units. The caller passes the position the recenter point maps to
// (the player's eye position), the player's heading in degrees (Doom: counter-clockwise), horizontal map units per
// meter and the level's pixel stretch (the same mapping as the head position uses). Without a valid controller
// pose (not tracked, not held) a pose in front of the head is made up, so the game stays playable with head aim.
typedef struct DoomXrHand
{
	double x, y, z;			// position of the hand
	double fx, fy, fz;		// aim direction (unit, Doom world axes: x east, y north, z up)
	double rx, ry, rz;		// the hand's right
	double ux, uy, uz;		// the hand's up
	double yaw, pitch;		// aim direction as Doom angles in degrees (pitch positive = down)
} DoomXrHand;
int xr_get_hand(int hand, double baseX, double baseY, double baseZ, double baseYawDeg, double unitsPerMeter,
                double pixelStretch, DoomXrHand *out);

// ---- head-locked HUD ----
// Copies the finished HUD image (framebuffer srcFbo, srcW x srcH, cleared to transparent before drawing) into the
// screen swapchain and shows it for this frame as a quad in front of the head: width and distance in meters, drop =
// how far below the line of sight its centre sits.
void xr_hud_blit(unsigned srcFbo, int srcW, int srcH, float width, float distance, float drop);
// Wrist mode: show the HUD as a small panel (width in meters) above the left controller instead; without a tracked left
// controller it stays in front of the head. Set before xr_hud_blit each frame.
void xr_hud_set_wrist(int on, float width);

// Interactive panel mode: the bottom stripFrac of the HUD image goes to a small panel (width in meters) above the left
// controller that can be touched with the right fingertip (DoomXrInput.hud*). Set before xr_hud_blit each frame.
void xr_hud_set_panel(int on, float width, float stripFrac);

// ---- app switching ----
// Leaves the game and shows the 2D mod library panel (implemented through DoomXRActivity.returnToLibrary).
void xr_open_library(void);

#ifdef __cplusplus
}
#endif
