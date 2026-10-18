// DoomXR OpenXR layer. Adapted in spirit from the QuestZDoom TBXR code, reduced to what a Quest 3/3S
// needs: one OpenGL ES session sharing SDL's EGL context, a projection layer for the world and a quad
// layer for the flat 2D screen.
#include "xr_vr.h"

#include <jni.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <android/log.h>

#define XR_USE_PLATFORM_ANDROID
#define XR_USE_GRAPHICS_API_OPENGL_ES
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_system.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <vector>

#define TAG "DoomXR-XR"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

#ifndef GL_SRGB8_ALPHA8
#define GL_SRGB8_ALPHA8 0x8C43
#endif
#define DOOMXR_GL_FRAMEBUFFER_SRGB_EXT 0x8DB9

namespace
{
struct Swap
{
	XrSwapchain handle = XR_NULL_HANDLE;
	int width = 0, height = 0;
	std::vector<XrSwapchainImageOpenGLESKHR> images;
	GLuint fbo = 0;
	bool acquired = false;
};

struct State
{
	bool initTried = false;
	bool ok = false;
	XrInstance instance = XR_NULL_HANDLE;
	XrSystemId system = XR_NULL_SYSTEM_ID;
	XrSession session = XR_NULL_HANDLE;
	XrSpace space = XR_NULL_HANDLE;
	XrSpace viewSpace = XR_NULL_HANDLE;
	XrSessionState sessionState = XR_SESSION_STATE_UNKNOWN;
	bool running = false;
	int width = 0, height = 0;

	Swap eye[2];
	Swap ui;

	// frame
	bool frameBegun = false;
	bool shouldRender = false;
	bool active = false;
	bool eyeDone[2] = {false, false};
	bool uiDrawn = false;
	XrTime displayTime = 0;
	XrView views[2];
	XrView lastViews[2];
	bool haveLastViews = false;

	// input
	XrActionSet actionSet = XR_NULL_HANDLE;
	XrAction aTrigger = XR_NULL_HANDLE, aSqueeze = XR_NULL_HANDLE, aStick = XR_NULL_HANDLE, aStickClick = XR_NULL_HANDLE;
	XrAction aBtn1 = XR_NULL_HANDLE, aBtn2 = XR_NULL_HANDLE, aMenu = XR_NULL_HANDLE, aAim = XR_NULL_HANDLE, aHaptic = XR_NULL_HANDLE;
	XrPath hand[2] = {XR_NULL_PATH, XR_NULL_PATH};
	XrSpace aimSpace[2] = {XR_NULL_HANDLE, XR_NULL_HANDLE};
	bool inputOk = false;
	DoomXrInput input = {};
	bool aimValid[2] = {false, false};
	XrPosef aim[2];
	float appliedPitch = 0;
	Swap laser;			// tiny swapchain for the laser pointer quad
	Swap panel;			// interactive forearm HUD panel
	bool panelOn = false, panelDrawn = false, panelShown = false;
	float panelW = 0.3f, panelFrac = 0.25f;	// panelFrac: height of the panel's corner of the screen image, as a fraction of its height
	bool panelHover = false;
	bool panelWatch = true;
	float panelBack = 0.11f, panelLift = 0.05f;	// watch mode: metres from the controller towards the elbow, and off the back of the wrist
	bool laserHit = false;
	float laserLen = 0;

	// recentering
	unsigned frameNo = 0;
	bool needRecenter = true;
	bool haveOrigin = false;
	float originPos[3] = {0, 0, 0};
	float originYaw = 0; // radians, OpenXR convention (see yawOf)

	// UI quad placement
	bool uiWasShown = false;
	bool hudWanted = false;
	float hudW = 1.4f, hudDist = 1.5f, hudDrop = 0.f;
	bool hudWrist = false;	// show the HUD above the left controller instead of in front of the head
	float hudWristW = 0.3f;
	XrPosef uiPose;
	float uiW = 1.5f, uiH = 1.5f;
} g;

const float kRad2Deg = 57.29577951308232f;

struct Vec3 { float x, y, z; };

Vec3 rotate(const XrQuaternionf &q, Vec3 v)
{
	// v' = v + 2 * cross(q.xyz, cross(q.xyz, v) + w * v)
	Vec3 u = {q.x, q.y, q.z};
	Vec3 c1 = {u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x};
	c1.x += q.w * v.x; c1.y += q.w * v.y; c1.z += q.w * v.z;
	Vec3 c2 = {u.y * c1.z - u.z * c1.y, u.z * c1.x - u.x * c1.z, u.x * c1.y - u.y * c1.x};
	return {v.x + 2 * c2.x, v.y + 2 * c2.y, v.z + 2 * c2.z};
}

// Rotation with the given columns (right-handed, unit length) as a quaternion.
XrQuaternionf quatFromBasis(Vec3 x, Vec3 y, Vec3 z)
{
	const float m00 = x.x, m10 = x.y, m20 = x.z, m01 = y.x, m11 = y.y, m21 = y.z, m02 = z.x, m12 = z.y, m22 = z.z;
	XrQuaternionf q;
	const float tr = m00 + m11 + m22;
	if (tr > 0) { float sq = sqrtf(tr + 1.f) * 2; q = {(m21 - m12) / sq, (m02 - m20) / sq, (m10 - m01) / sq, 0.25f * sq}; }
	else if (m00 > m11 && m00 > m22) { float sq = sqrtf(1.f + m00 - m11 - m22) * 2; q = {0.25f * sq, (m01 + m10) / sq, (m02 + m20) / sq, (m21 - m12) / sq}; }
	else if (m11 > m22) { float sq = sqrtf(1.f + m11 - m00 - m22) * 2; q = {(m01 + m10) / sq, 0.25f * sq, (m12 + m21) / sq, (m02 - m20) / sq}; }
	else { float sq = sqrtf(1.f + m22 - m00 - m11) * 2; q = {(m02 + m20) / sq, (m12 + m21) / sq, 0.25f * sq, (m10 - m01) / sq}; }
	return q;
}

float yawOf(const XrQuaternionf &q)
{
	Vec3 f = rotate(q, {0, 0, -1});
	return atan2f(-f.x, -f.z);
}

float wrapDeg(float d)
{
	while (d > 180.f) d -= 360.f;
	while (d <= -180.f) d += 360.f;
	return d;
}

bool check(XrResult r, const char *what)
{
	if (XR_SUCCEEDED(r)) return true;
	char buf[XR_MAX_RESULT_STRING_SIZE] = "?";
	if (g.instance != XR_NULL_HANDLE) xrResultToString(g.instance, r, buf);
	LOGE("%s failed: %s (%d)", what, buf, (int)r);
	return false;
}

bool hasExtension(const std::vector<XrExtensionProperties> &list, const char *name)
{
	for (auto &e : list) if (!strcmp(e.extensionName, name)) return true;
	return false;
}

bool createSwapchain(Swap &s, int64_t format, int w, int h)
{
	XrSwapchainCreateInfo ci = {XR_TYPE_SWAPCHAIN_CREATE_INFO};
	ci.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
	ci.format = format;
	ci.sampleCount = 1;
	ci.width = w;
	ci.height = h;
	ci.faceCount = 1;
	ci.arraySize = 1;
	ci.mipCount = 1;
	if (!check(xrCreateSwapchain(g.session, &ci, &s.handle), "xrCreateSwapchain")) return false;
	s.width = w;
	s.height = h;
	uint32_t n = 0;
	if (!check(xrEnumerateSwapchainImages(s.handle, 0, &n, nullptr), "xrEnumerateSwapchainImages")) return false;
	s.images.assign(n, XrSwapchainImageOpenGLESKHR{XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR});
	if (!check(xrEnumerateSwapchainImages(s.handle, n, &n, (XrSwapchainImageBaseHeader *)s.images.data()),
	           "xrEnumerateSwapchainImages(2)")) return false;
	glGenFramebuffers(1, &s.fbo);
	return true;
}

// Acquires the next image and attaches it to the swapchain's framebuffer. Returns the FBO (0 on failure).
GLuint acquire(Swap &s)
{
	if (s.acquired) return s.fbo;
	uint32_t idx = 0;
	XrSwapchainImageAcquireInfo ai = {XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
	if (!check(xrAcquireSwapchainImage(s.handle, &ai, &idx), "xrAcquireSwapchainImage")) return 0;
	XrSwapchainImageWaitInfo wi = {XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
	wi.timeout = XR_INFINITE_DURATION;
	if (!check(xrWaitSwapchainImage(s.handle, &wi), "xrWaitSwapchainImage"))
	{
		XrSwapchainImageReleaseInfo ri = {XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
		xrReleaseSwapchainImage(s.handle, &ri);
		return 0;
	}
	s.acquired = true;
	GLint prev = 0;
	glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prev);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, s.fbo);
	glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s.images[idx].image, 0);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prev);
	return s.fbo;
}

void release(Swap &s)
{
	if (!s.acquired) return;
	s.acquired = false;
	glFlush();
	XrSwapchainImageReleaseInfo ri = {XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
	check(xrReleaseSwapchainImage(s.handle, &ri), "xrReleaseSwapchainImage");
}

void pollEvents()
{
	XrEventDataBuffer ev;
	for (;;)
	{
		ev = {XR_TYPE_EVENT_DATA_BUFFER};
		if (xrPollEvent(g.instance, &ev) != XR_SUCCESS) break;
		switch (ev.type)
		{
		case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED:
		{
			auto *e = (XrEventDataSessionStateChanged *)&ev;
			g.sessionState = e->state;
			LOGI("session state -> %d", (int)e->state);
			if (e->state == XR_SESSION_STATE_READY)
			{
				XrSessionBeginInfo bi = {XR_TYPE_SESSION_BEGIN_INFO};
				bi.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
				if (check(xrBeginSession(g.session, &bi), "xrBeginSession")) g.running = true;
			}
			else if (e->state == XR_SESSION_STATE_STOPPING)
			{
				check(xrEndSession(g.session), "xrEndSession");
				g.running = false;
			}
			else if (e->state == XR_SESSION_STATE_EXITING || e->state == XR_SESSION_STATE_LOSS_PENDING)
			{
				g.running = false;
				SDL_Event q;
				memset(&q, 0, sizeof(q));
				q.type = SDL_QUIT;
				SDL_PushEvent(&q);
			}
			break;
		}
		case XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING:
			LOGI("reference space change pending: recentering");
			g.needRecenter = true;
			break;
		case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
			LOGW("instance loss pending");
			break;
		default:
			break;
		}
	}
}
} // namespace


namespace
{
bool createAction(XrAction &out, const char *name, const char *loc, XrActionType type, bool perHand)
{
	XrActionCreateInfo ai = {XR_TYPE_ACTION_CREATE_INFO};
	ai.actionType = type;
	strncpy(ai.actionName, name, XR_MAX_ACTION_NAME_SIZE - 1);
	strncpy(ai.localizedActionName, loc, XR_MAX_LOCALIZED_ACTION_NAME_SIZE - 1);
	if (perHand)
	{
		ai.countSubactionPaths = 2;
		ai.subactionPaths = g.hand;
	}
	return check(xrCreateAction(g.actionSet, &ai, &out), name);
}

XrPath path(const char *p)
{
	XrPath x = XR_NULL_PATH;
	xrStringToPath(g.instance, p, &x);
	return x;
}

bool setupInput()
{
	g.hand[0] = path("/user/hand/left");
	g.hand[1] = path("/user/hand/right");
	XrActionSetCreateInfo si = {XR_TYPE_ACTION_SET_CREATE_INFO};
	strcpy(si.actionSetName, "gameplay");
	strcpy(si.localizedActionSetName, "Gameplay");
	if (!check(xrCreateActionSet(g.instance, &si, &g.actionSet), "xrCreateActionSet")) return false;

	if (!createAction(g.aTrigger, "trigger", "Trigger", XR_ACTION_TYPE_FLOAT_INPUT, true) ||
	    !createAction(g.aSqueeze, "squeeze", "Grip", XR_ACTION_TYPE_FLOAT_INPUT, true) ||
	    !createAction(g.aStick, "thumbstick", "Thumbstick", XR_ACTION_TYPE_VECTOR2F_INPUT, true) ||
	    !createAction(g.aStickClick, "thumbstick_click", "Thumbstick click", XR_ACTION_TYPE_BOOLEAN_INPUT, true) ||
	    !createAction(g.aBtn1, "button_lower", "A / X", XR_ACTION_TYPE_BOOLEAN_INPUT, true) ||
	    !createAction(g.aBtn2, "button_upper", "B / Y", XR_ACTION_TYPE_BOOLEAN_INPUT, true) ||
	    !createAction(g.aMenu, "menu", "Menu", XR_ACTION_TYPE_BOOLEAN_INPUT, false) ||
	    !createAction(g.aAim, "aim_pose", "Aim pose", XR_ACTION_TYPE_POSE_INPUT, true) ||
	    !createAction(g.aHaptic, "haptic", "Haptic", XR_ACTION_TYPE_VIBRATION_OUTPUT, true))
		return false;

	struct { XrAction a; const char *p; } b[] = {
		{g.aTrigger, "/user/hand/left/input/trigger/value"},     {g.aTrigger, "/user/hand/right/input/trigger/value"},
		{g.aSqueeze, "/user/hand/left/input/squeeze/value"},     {g.aSqueeze, "/user/hand/right/input/squeeze/value"},
		{g.aStick, "/user/hand/left/input/thumbstick"},          {g.aStick, "/user/hand/right/input/thumbstick"},
		{g.aStickClick, "/user/hand/left/input/thumbstick/click"}, {g.aStickClick, "/user/hand/right/input/thumbstick/click"},
		{g.aBtn1, "/user/hand/left/input/x/click"},              {g.aBtn1, "/user/hand/right/input/a/click"},
		{g.aBtn2, "/user/hand/left/input/y/click"},              {g.aBtn2, "/user/hand/right/input/b/click"},
		{g.aMenu, "/user/hand/left/input/menu/click"},
		{g.aAim, "/user/hand/left/input/aim/pose"},              {g.aAim, "/user/hand/right/input/aim/pose"},
		{g.aHaptic, "/user/hand/left/output/haptic"},            {g.aHaptic, "/user/hand/right/output/haptic"},
	};
	std::vector<XrActionSuggestedBinding> sb;
	for (auto &e : b) sb.push_back({e.a, path(e.p)});
	XrInteractionProfileSuggestedBinding ib = {XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
	ib.interactionProfile = path("/interaction_profiles/oculus/touch_controller");
	ib.countSuggestedBindings = (uint32_t)sb.size();
	ib.suggestedBindings = sb.data();
	if (!check(xrSuggestInteractionProfileBindings(g.instance, &ib), "xrSuggestInteractionProfileBindings")) return false;

	XrSessionActionSetsAttachInfo at = {XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
	at.countActionSets = 1;
	at.actionSets = &g.actionSet;
	if (!check(xrAttachSessionActionSets(g.session, &at), "xrAttachSessionActionSets")) return false;

	for (int i = 0; i < 2; i++)
	{
		XrActionSpaceCreateInfo sp = {XR_TYPE_ACTION_SPACE_CREATE_INFO};
		sp.action = g.aAim;
		sp.subactionPath = g.hand[i];
		sp.poseInActionSpace.orientation.w = 1.f;
		if (!check(xrCreateActionSpace(g.session, &sp, &g.aimSpace[i]), "xrCreateActionSpace")) return false;
	}
	return true;
}

float getFloat(XrAction a, XrPath sub)
{
	XrActionStateGetInfo gi = {XR_TYPE_ACTION_STATE_GET_INFO};
	gi.action = a;
	gi.subactionPath = sub;
	XrActionStateFloat st = {XR_TYPE_ACTION_STATE_FLOAT};
	if (XR_FAILED(xrGetActionStateFloat(g.session, &gi, &st)) || !st.isActive) return 0.f;
	return st.currentState;
}

bool getBool(XrAction a, XrPath sub)
{
	XrActionStateGetInfo gi = {XR_TYPE_ACTION_STATE_GET_INFO};
	gi.action = a;
	gi.subactionPath = sub;
	XrActionStateBoolean st = {XR_TYPE_ACTION_STATE_BOOLEAN};
	if (XR_FAILED(xrGetActionStateBoolean(g.session, &gi, &st)) || !st.isActive) return false;
	return st.currentState == XR_TRUE;
}

XrVector2f getVec2(XrAction a, XrPath sub)
{
	XrActionStateGetInfo gi = {XR_TYPE_ACTION_STATE_GET_INFO};
	gi.action = a;
	gi.subactionPath = sub;
	XrActionStateVector2f st = {XR_TYPE_ACTION_STATE_VECTOR2F};
	if (XR_FAILED(xrGetActionStateVector2f(g.session, &gi, &st)) || !st.isActive) return {0, 0};
	return st.currentState;
}

// Intersects the aim ray with the floating screen. u,v: 0..1 from the top-left.
bool hitScreen(const XrPosef &aim, float *u, float *v, float *dist)
{
	Vec3 d = rotate(aim.orientation, {0, 0, -1});
	Vec3 n = rotate(g.uiPose.orientation, {0, 0, 1});
	float den = d.x * n.x + d.y * n.y + d.z * n.z;
	if (fabsf(den) < 1e-4f) return false;
	Vec3 pq = {g.uiPose.position.x - aim.position.x, g.uiPose.position.y - aim.position.y, g.uiPose.position.z - aim.position.z};
	float t = (pq.x * n.x + pq.y * n.y + pq.z * n.z) / den;
	if (t <= 0.f) return false;
	Vec3 hit = {aim.position.x + d.x * t - g.uiPose.position.x, aim.position.y + d.y * t - g.uiPose.position.y,
	            aim.position.z + d.z * t - g.uiPose.position.z};
	XrQuaternionf inv = {-g.uiPose.orientation.x, -g.uiPose.orientation.y, -g.uiPose.orientation.z, g.uiPose.orientation.w};
	Vec3 l = rotate(inv, hit);
	float uu = l.x / g.uiW + 0.5f, vv = 0.5f - l.y / g.uiH;
	if (uu < 0.f || uu > 1.f || vv < 0.f || vv > 1.f) return false;
	*u = uu;
	*v = vv;
	if (dist) *dist = t;
	return true;
}

// Pose of the interactive HUD panel. Watch mode: lying on the back of the left forearm, a little behind the controller,
// read like a wristwatch (text runs along the arm towards the fingers; hidden while it faces away from the eyes).
// Otherwise: upright, a hand's width above the left controller, turned towards the head.
bool computePanelPose(float w, XrPosef *out)
{
	if (!g.aimValid[0]) return false;
	(void)w;
	const XrPosef &a = g.aim[0];
	const Vec3 head = {0.5f * (g.views[0].pose.position.x + g.views[1].pose.position.x),
	                   0.5f * (g.views[0].pose.position.y + g.views[1].pose.position.y),
	                   0.5f * (g.views[0].pose.position.z + g.views[1].pose.position.z)};
	if (g.panelWatch)
	{
		// Left controller axes: x right (towards the palm side), y up (thumb), z back (towards the elbow). The back of the
		// hand faces -x; the panel's right is -z (towards the fingers), its up -y (pinky side), its normal -x.
		const Vec3 cx = rotate(a.orientation, {1, 0, 0}), cy = rotate(a.orientation, {0, 1, 0}), cz = rotate(a.orientation, {0, 0, 1});
		const Vec3 px = {-cz.x, -cz.y, -cz.z}, py = {-cy.x, -cy.y, -cy.z}, pz = {-cx.x, -cx.y, -cx.z};
		const Vec3 c = {a.position.x + cz.x * g.panelBack + pz.x * g.panelLift,
		                a.position.y + cz.y * g.panelBack + pz.y * g.panelLift,
		                a.position.z + cz.z * g.panelBack + pz.z * g.panelLift};
		const Vec3 toHead = {head.x - c.x, head.y - c.y, head.z - c.z};
		const float tl = sqrtf(toHead.x * toHead.x + toHead.y * toHead.y + toHead.z * toHead.z);
		if (tl < 1e-3f) return false;
		if ((toHead.x * pz.x + toHead.y * pz.y + toHead.z * pz.z) / tl < 0.2f) return false;	// turned away from the eyes
		out->orientation = quatFromBasis(px, py, pz);
		out->position = {c.x, c.y, c.z};
		return true;
	}
	const Vec3 up = rotate(a.orientation, {0, 1, 0});
	const Vec3 c = {a.position.x + up.x * 0.12f, a.position.y + up.y * 0.12f, a.position.z + up.z * 0.12f};
	Vec3 z = {head.x - c.x, head.y - c.y, head.z - c.z};
	const float zl = sqrtf(z.x * z.x + z.y * z.y + z.z * z.z);
	if (zl < 1e-3f) return false;
	z = {z.x / zl, z.y / zl, z.z / zl};
	Vec3 x = {z.z, 0.f, -z.x};	// cross(world up, z)
	const float xl = sqrtf(x.x * x.x + x.z * x.z);
	if (xl < 1e-3f) return false;
	x = {x.x / xl, 0.f, x.z / xl};
	const Vec3 y = {z.y * x.z - z.z * x.y, z.z * x.x - z.x * x.z, z.x * x.y - z.y * x.x};	// cross(z, x)
	out->orientation = quatFromBasis(x, y, z);
	out->position = {c.x, c.y, c.z};
	return true;
}

// Fingertip of the right controller against the panel: hover when within 12 cm in front of it, touch within 2 cm.
void updatePanelTouch(DoomXrInput &in)
{
	g.panelHover = false;
	if (!g.panelOn || !g.panelShown || !g.aimValid[1]) return;
	XrPosef p;
	if (!computePanelPose(g.panelW, &p)) return;
	const float w = g.panelW, h = g.panelW * (float)g.panel.height / (float)g.panel.width;
	const XrPosef &r = g.aim[1];
	const Vec3 fwd = rotate(r.orientation, {0, 0, -1});
	const Vec3 tip = {r.position.x + fwd.x * 0.04f - p.position.x, r.position.y + fwd.y * 0.04f - p.position.y,
	                  r.position.z + fwd.z * 0.04f - p.position.z};
	const XrQuaternionf inv = {-p.orientation.x, -p.orientation.y, -p.orientation.z, p.orientation.w};
	const Vec3 l = rotate(inv, tip);
	const float m = 0.02f;
	if (fabsf(l.x) > w * 0.5f + m || fabsf(l.y) > h * 0.5f + m || l.z > 0.12f || l.z < -0.03f) return;
	g.panelHover = true;
	in.hudHover = 1;
	in.hudTouch = l.z < 0.02f;
	in.hudU = fminf(fmaxf(l.x / w + 0.5f, 0.f), 1.f);
	in.hudV = fminf(fmaxf(0.5f - l.y / h, 0.f), 1.f);
}

// Reads all actions for this frame (called from xr_frame_begin after the session is running).
void updateInput()
{
	if (!g.inputOk) return;
	XrActiveActionSet as = {g.actionSet, XR_NULL_PATH};
	XrActionsSyncInfo sy = {XR_TYPE_ACTIONS_SYNC_INFO};
	sy.countActiveActionSets = 1;
	sy.activeActionSets = &as;
	XrResult r = xrSyncActions(g.session, &sy);
	DoomXrInput in = {};
	if (XR_FAILED(r) || r == XR_SESSION_NOT_FOCUSED)
	{
		g.input = in;
		g.aimValid[0] = g.aimValid[1] = false;
		return;
	}
	XrVector2f ls = getVec2(g.aStick, g.hand[0]), rs = getVec2(g.aStick, g.hand[1]);
	in.moveX = ls.x; in.moveY = ls.y; in.turnX = rs.x; in.turnY = rs.y;
	for (int i = 0; i < 2; i++)
	{
		in.trigger[i] = getFloat(g.aTrigger, g.hand[i]);
		in.grip[i] = getFloat(g.aSqueeze, g.hand[i]);
	}
	if (getBool(g.aBtn1, g.hand[1])) in.buttons |= XRB_A;
	if (getBool(g.aBtn2, g.hand[1])) in.buttons |= XRB_B;
	if (getBool(g.aBtn1, g.hand[0])) in.buttons |= XRB_X;
	if (getBool(g.aBtn2, g.hand[0])) in.buttons |= XRB_Y;
	if (getBool(g.aStickClick, g.hand[0])) in.buttons |= XRB_LTHUMB;
	if (getBool(g.aStickClick, g.hand[1])) in.buttons |= XRB_RTHUMB;
	if (getBool(g.aMenu, XR_NULL_PATH)) in.buttons |= XRB_MENU;

	for (int i = 0; i < 2; i++)
	{
		XrSpaceLocation loc = {XR_TYPE_SPACE_LOCATION};
		g.aimValid[i] = XR_SUCCEEDED(xrLocateSpace(g.aimSpace[i], g.space, g.displayTime, &loc)) &&
		                (loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) && (loc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT);
		if (g.aimValid[i]) g.aim[i] = loc.pose;
	}
	// The right hand points by default; the left hand takes over when only it is valid or its trigger is used.
	int hand = (g.aimValid[1] && !(g.aimValid[0] && in.trigger[0] > 0.5f && in.trigger[1] < 0.5f)) ? 1 : 0;
	in.uiHand = hand;
	g.laserHit = false;
	if (g.aimValid[hand] && g.uiWasShown)
	{
		float u, v, t;
		if (hitScreen(g.aim[hand], &u, &v, &t))
		{
			in.uiHit = 1;
			in.uiU = u;
			in.uiV = v;
			g.laserHit = true;
			g.laserLen = t;
		}
	}
	updatePanelTouch(in);
	g.input = in;
}

} // namespace

extern "C" {

int xr_trace(void) { return g.frameNo < 30 || g.frameNo % 300 == 0; }

void xr_log(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	__android_log_vprint(ANDROID_LOG_INFO, TAG, fmt, ap);
	va_end(ap);
}

int xr_is_initialized(void) { return g.ok ? 1 : 0; }

void xr_get_render_size(int *w, int *h)
{
	if (w) *w = g.width;
	if (h) *h = g.height;
}

int xr_init(void)
{
	if (g.initTried) return g.ok;
	g.initTried = true;

	JNIEnv *env = (JNIEnv *)SDL_AndroidGetJNIEnv();
	jobject activity = (jobject)SDL_AndroidGetActivity();
	JavaVM *vm = nullptr;
	if (!env || !activity || env->GetJavaVM(&vm) != JNI_OK)
	{
		LOGE("no JNI environment/activity from SDL");
		return 0;
	}

	PFN_xrInitializeLoaderKHR initLoader = nullptr;
	xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrInitializeLoaderKHR", (PFN_xrVoidFunction *)&initLoader);
	if (!initLoader)
	{
		LOGE("xrInitializeLoaderKHR not available");
		return 0;
	}
	XrLoaderInitInfoAndroidKHR li = {XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR};
	li.applicationVM = vm;
	li.applicationContext = activity;
	if (!check(initLoader((const XrLoaderInitInfoBaseHeaderKHR *)&li), "xrInitializeLoaderKHR")) return 0;

	uint32_t n = 0;
	xrEnumerateInstanceExtensionProperties(nullptr, 0, &n, nullptr);
	std::vector<XrExtensionProperties> exts(n, XrExtensionProperties{XR_TYPE_EXTENSION_PROPERTIES});
	xrEnumerateInstanceExtensionProperties(nullptr, n, &n, exts.data());
	if (!hasExtension(exts, XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME))
	{
		LOGE("runtime lacks %s", XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME);
		return 0;
	}
	std::vector<const char *> enabled = {XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME};
	bool androidCreate = hasExtension(exts, XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME);
	if (androidCreate) enabled.push_back(XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME);

	XrInstanceCreateInfoAndroidKHR ia = {XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR};
	ia.applicationVM = vm;
	ia.applicationActivity = activity;

	XrInstanceCreateInfo ci = {XR_TYPE_INSTANCE_CREATE_INFO};
	ci.next = androidCreate ? &ia : nullptr;
	strncpy(ci.applicationInfo.applicationName, "DoomXR", XR_MAX_APPLICATION_NAME_SIZE - 1);
	strncpy(ci.applicationInfo.engineName, "UZDoom", XR_MAX_ENGINE_NAME_SIZE - 1);
	ci.applicationInfo.applicationVersion = 1;
	ci.applicationInfo.engineVersion = 1;
	ci.applicationInfo.apiVersion = XR_API_VERSION_1_0;
	ci.enabledExtensionCount = (uint32_t)enabled.size();
	ci.enabledExtensionNames = enabled.data();
	if (!check(xrCreateInstance(&ci, &g.instance), "xrCreateInstance")) return 0;

	XrInstanceProperties ip = {XR_TYPE_INSTANCE_PROPERTIES};
	xrGetInstanceProperties(g.instance, &ip);
	LOGI("runtime: %s %u.%u.%u", ip.runtimeName, XR_VERSION_MAJOR(ip.runtimeVersion),
	     XR_VERSION_MINOR(ip.runtimeVersion), XR_VERSION_PATCH(ip.runtimeVersion));

	XrSystemGetInfo si = {XR_TYPE_SYSTEM_GET_INFO};
	si.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
	if (!check(xrGetSystem(g.instance, &si, &g.system), "xrGetSystem")) return 0;
	XrSystemProperties sp = {XR_TYPE_SYSTEM_PROPERTIES};
	xrGetSystemProperties(g.instance, g.system, &sp);
	LOGI("system: %s", sp.systemName);

	uint32_t vc = 0;
	xrEnumerateViewConfigurationViews(g.instance, g.system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &vc, nullptr);
	if (vc != 2)
	{
		LOGE("unexpected view count %u", vc);
		return 0;
	}
	XrViewConfigurationView cv[2] = {{XR_TYPE_VIEW_CONFIGURATION_VIEW}, {XR_TYPE_VIEW_CONFIGURATION_VIEW}};
	xrEnumerateViewConfigurationViews(g.instance, g.system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 2, &vc, cv);
	float scale = 1.0f;
	if (const char *s = getenv("DOOMXR_RENDER_SCALE")) scale = (float)atof(s);
	if (scale < 0.25f || scale > 2.0f) scale = 1.0f;
	g.width = (int)(cv[0].recommendedImageRectWidth * scale) & ~1;
	g.height = (int)(cv[0].recommendedImageRectHeight * scale) & ~1;
	LOGI("eye size %dx%d (recommended %ux%u, max %ux%u)", g.width, g.height, cv[0].recommendedImageRectWidth,
	     cv[0].recommendedImageRectHeight, cv[0].maxImageRectWidth, cv[0].maxImageRectHeight);

	PFN_xrGetOpenGLESGraphicsRequirementsKHR getReq = nullptr;
	xrGetInstanceProcAddr(g.instance, "xrGetOpenGLESGraphicsRequirementsKHR", (PFN_xrVoidFunction *)&getReq);
	if (!getReq) { LOGE("xrGetOpenGLESGraphicsRequirementsKHR missing"); return 0; }
	XrGraphicsRequirementsOpenGLESKHR req = {XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR};
	if (!check(getReq(g.instance, g.system, &req), "xrGetOpenGLESGraphicsRequirementsKHR")) return 0;
	LOGI("GLES requirement min %u.%u max %u.%u", XR_VERSION_MAJOR(req.minApiVersionSupported),
	     XR_VERSION_MINOR(req.minApiVersionSupported), XR_VERSION_MAJOR(req.maxApiVersionSupported),
	     XR_VERSION_MINOR(req.maxApiVersionSupported));

	EGLDisplay dpy = eglGetCurrentDisplay();
	EGLContext ctx = eglGetCurrentContext();
	if (dpy == EGL_NO_DISPLAY || ctx == EGL_NO_CONTEXT)
	{
		LOGE("no current EGL context (xr_init must run after the GL context is created)");
		return 0;
	}
	EGLint cfgId = 0;
	eglQueryContext(dpy, ctx, EGL_CONFIG_ID, &cfgId);
	const EGLint attr[] = {EGL_CONFIG_ID, cfgId, EGL_NONE};
	EGLConfig cfg = nullptr;
	EGLint ncfg = 0;
	if (!eglChooseConfig(dpy, attr, &cfg, 1, &ncfg) || ncfg < 1)
	{
		LOGE("could not resolve EGLConfig %d", cfgId);
		return 0;
	}
	LOGI("GL: %s / %s", glGetString(GL_RENDERER), glGetString(GL_VERSION));

	XrGraphicsBindingOpenGLESAndroidKHR gb = {XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR};
	gb.display = dpy;
	gb.config = cfg;
	gb.context = ctx;
	XrSessionCreateInfo sci = {XR_TYPE_SESSION_CREATE_INFO};
	sci.next = &gb;
	sci.systemId = g.system;
	if (!check(xrCreateSession(g.instance, &sci, &g.session), "xrCreateSession")) return 0;

	XrReferenceSpaceCreateInfo rs = {XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
	rs.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
	rs.poseInReferenceSpace.orientation.w = 1.f;
	if (!check(xrCreateReferenceSpace(g.session, &rs, &g.space), "xrCreateReferenceSpace")) return 0;

	rs.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
	if (!check(xrCreateReferenceSpace(g.session, &rs, &g.viewSpace), "xrCreateReferenceSpace(view)")) g.viewSpace = XR_NULL_HANDLE;

	g.inputOk = setupInput();
	LOGI("controller input %s", g.inputOk ? "ready (oculus/touch_controller profile)" : "FAILED to set up");

	// The engine output is already gamma encoded: use an sRGB swapchain only if sRGB writes can be switched off.
	const char *glext = (const char *)glGetString(GL_EXTENSIONS);
	bool srgbControl = glext && strstr(glext, "GL_EXT_sRGB_write_control");
	uint32_t fc = 0;
	xrEnumerateSwapchainFormats(g.session, 0, &fc, nullptr);
	std::vector<int64_t> formats(fc);
	xrEnumerateSwapchainFormats(g.session, fc, &fc, formats.data());
	auto has = [&](int64_t f) { for (auto x : formats) if (x == f) return true; return false; };
	int64_t format = formats.empty() ? GL_RGBA8 : formats[0];
	if (srgbControl && has(GL_SRGB8_ALPHA8)) format = GL_SRGB8_ALPHA8;
	else if (has(GL_RGBA8)) format = GL_RGBA8;
	if (format == GL_SRGB8_ALPHA8) glDisable(DOOMXR_GL_FRAMEBUFFER_SRGB_EXT);
	LOGI("swapchain format 0x%llx (sRGB write control: %d)", (long long)format, (int)srgbControl);

	if (!createSwapchain(g.eye[0], format, g.width, g.height) || !createSwapchain(g.eye[1], format, g.width, g.height) ||
	    !createSwapchain(g.ui, format, g.width, g.height))
		return 0;

	if (!createSwapchain(g.laser, format, 8, 8)) LOGW("laser swapchain unavailable");
	if (!createSwapchain(g.panel, format, 512, 192)) LOGW("panel swapchain unavailable");
	g.ok = true;
	LOGI("OpenXR initialized");
	return 1;
}

void xr_recenter(void) { g.needRecenter = true; }

int xr_input_poll(DoomXrInput *out)
{
	if (!g.ok || !g.running || !g.inputOk) return 0;
	if (out) *out = g.input;
	return 1;
}

void xr_haptic(int hand, float amplitude, float seconds)
{
	if (!g.ok || !g.inputOk || hand < 0 || hand > 1) return;
	XrHapticActionInfo hi = {XR_TYPE_HAPTIC_ACTION_INFO};
	hi.action = g.aHaptic;
	hi.subactionPath = g.hand[hand];
	XrHapticVibration vb = {XR_TYPE_HAPTIC_VIBRATION};
	vb.amplitude = amplitude;
	vb.duration = (XrDuration)(seconds * 1e9);
	vb.frequency = XR_FREQUENCY_UNSPECIFIED;
	xrApplyHapticFeedback(g.session, &hi, (const XrHapticBaseHeader *)&vb);
}

float xr_take_head_yaw_delta(void)
{
	if (!g.ok || !g.haveOrigin || !g.haveLastViews) return 0.f;
	const float h = yawOf(g.lastViews[0].pose.orientation);
	float d = h - g.originYaw;
	while (d > 3.14159265f) d -= 6.2831853f;
	while (d <= -3.14159265f) d += 6.2831853f;
	if (d == 0.f) return 0.f;
	// Keep the head's offset from the origin point continuous in the world while the reference heading rotates.
	const float px = 0.5f * (g.lastViews[0].pose.position.x + g.lastViews[1].pose.position.x) - g.originPos[0];
	const float pz = 0.5f * (g.lastViews[0].pose.position.z + g.lastViews[1].pose.position.z) - g.originPos[2];
	const float s0 = sinf(g.originYaw), c0 = cosf(g.originYaw);
	const float a = -s0 * px - c0 * pz, b = c0 * px - s0 * pz;	// forward, right in the old frame
	const float s1 = sinf(h), c1 = cosf(h);
	const float nx = a * -s1 + b * c1, nz = a * -c1 + b * -s1;
	g.originPos[0] = 0.5f * (g.lastViews[0].pose.position.x + g.lastViews[1].pose.position.x) - nx;
	g.originPos[2] = 0.5f * (g.lastViews[0].pose.position.z + g.lastViews[1].pose.position.z) - nz;
	g.originYaw = h;
	return d * kRad2Deg;
}

float xr_take_head_pitch_delta(void)
{
	if (!g.ok || !g.haveLastViews) return 0.f;
	Vec3 f = rotate(g.lastViews[0].pose.orientation, {0, 0, -1});
	float fy = f.y < -1.f ? -1.f : (f.y > 1.f ? 1.f : f.y);
	const float p = -asinf(fy) * kRad2Deg;
	const float d = p - g.appliedPitch;
	g.appliedPitch = p;
	return d;
}

int xr_get_hand(int hand, double bx, double by, double bz, double byawDeg, double upm, double stretch, DoomXrHand *o)
{
	if (!o || !g.ok || !g.haveOrigin || !g.haveLastViews || hand < 0 || hand > 1) return 0;
	XrPosef p;
	if (g.aimValid[hand] && g.running)
		p = g.aim[hand];
	else
	{
		// No controller pose: hold an imaginary weapon in front of the head, lower right.
		const XrPosef &hp = g.lastViews[0].pose;
		Vec3 off = rotate(hp.orientation, {hand == 1 ? 0.18f : -0.18f, -0.28f, -0.35f});
		p.orientation = hp.orientation;
		p.position = {0.5f * (g.lastViews[0].pose.position.x + g.lastViews[1].pose.position.x) + off.x,
		              0.5f * (g.lastViews[0].pose.position.y + g.lastViews[1].pose.position.y) + off.y,
		              0.5f * (g.lastViews[0].pose.position.z + g.lastViews[1].pose.position.z) + off.z};
	}
	const double so = sin(g.originYaw), co = cos(g.originYaw);
	const double rad = byawDeg * 0.017453292519943295;
	const double cy = cos(rad), sy = sin(rad);
	// A direction in OpenXR axes -> Doom world axes (heading = base yaw, origin heading removed).
	auto dir = [&](Vec3 v, double *wx, double *wy, double *wz) {
		const double fwd = -so * v.x - co * v.z, rgt = co * v.x - so * v.z;
		*wx = fwd * cy + rgt * sy;
		*wy = fwd * sy - rgt * cy;
		*wz = v.y;
	};
	const double dx = p.position.x - g.originPos[0], dz = p.position.z - g.originPos[2];
	const double fwd = -so * dx - co * dz, rgt = co * dx - so * dz, up = p.position.y - g.originPos[1];
	o->x = bx + upm * (fwd * cy + rgt * sy);
	o->y = by + upm * (fwd * sy - rgt * cy);
	o->z = bz + upm * up / stretch;
	dir(rotate(p.orientation, {0, 0, -1}), &o->fx, &o->fy, &o->fz);
	dir(rotate(p.orientation, {1, 0, 0}), &o->rx, &o->ry, &o->rz);
	dir(rotate(p.orientation, {0, 1, 0}), &o->ux, &o->uy, &o->uz);
	o->yaw = atan2(o->fy, o->fx) * 57.29577951308232;
	double fz = o->fz < -1 ? -1 : (o->fz > 1 ? 1 : o->fz);
	o->pitch = -asin(fz) * 57.29577951308232;
	return 1;
}

void xr_hud_blit(unsigned srcFbo, int sw, int sh, float width, float distance, float drop)
{
	if (!g.ok || !g.active) return;
	GLuint fbo = acquire(g.ui);
	if (!fbo) return;
	g.uiDrawn = true;
	GLint prevRead = 0, prevDraw = 0;
	glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevRead);
	glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevDraw);
	GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
	glDisable(GL_SCISSOR_TEST);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, srcFbo);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo);
	glBlitFramebuffer(0, 0, sw, sh, 0, 0, g.ui.width, g.ui.height, GL_COLOR_BUFFER_BIT,
	                  (sw == g.ui.width && sh == g.ui.height) ? GL_NEAREST : GL_LINEAR);
	const int regionH = (int)(sh * g.panelFrac);
	const int regionW = (int)(regionH * (float)g.panel.width / (float)(g.panel.height > 0 ? g.panel.height : 1));
	if (g.panelOn && g.panel.handle != XR_NULL_HANDLE && regionH > 0 && regionW > 0 && regionW <= sw)
	{
		// Interactive panel: the game drew it in the bottom-left corner of the screen image; move it to its own
		// swapchain and cut it out of the head-locked image.
		if (GLuint pfbo = acquire(g.panel))
		{
			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, pfbo);
			glBlitFramebuffer(0, 0, regionW, regionH, 0, 0, g.panel.width, g.panel.height, GL_COLOR_BUFFER_BIT, GL_LINEAR);
			g.panelDrawn = true;
			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo);
			static const GLfloat clear[4] = {0, 0, 0, 0};
			glEnable(GL_SCISSOR_TEST);
			glScissor(0, 0, regionW * g.ui.width / sw + 1, regionH * g.ui.height / sh + 1);
			glClearBufferfv(GL_COLOR, 0, clear);
			glDisable(GL_SCISSOR_TEST);
		}
	}
	glBindFramebuffer(GL_READ_FRAMEBUFFER, prevRead);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prevDraw);
	if (scissor) glEnable(GL_SCISSOR_TEST);
	g.hudWanted = true;
	g.hudW = width;
	g.hudDist = distance;
	g.hudDrop = drop;
}

void xr_hud_set_wrist(int on, float width)
{
	g.hudWrist = on != 0;
	g.hudWristW = width < 0.05f ? 0.05f : (width > 1.5f ? 1.5f : width);
}

void xr_hud_set_panel(int on, float width, float stripFrac, int watch, float back, float lift)
{
	g.panelWatch = watch != 0;
	g.panelBack = back < -0.2f ? -0.2f : (back > 0.4f ? 0.4f : back);
	g.panelLift = lift < -0.1f ? -0.1f : (lift > 0.3f ? 0.3f : lift);
	g.panelOn = on != 0;
	g.panelW = width < 0.1f ? 0.1f : (width > 0.8f ? 0.8f : width);
	g.panelFrac = stripFrac < 0.1f ? 0.1f : (stripFrac > 0.5f ? 0.5f : stripFrac);
}

int xr_frame_active(void) { return g.active ? 1 : 0; }

int xr_frame_begin(void)
{
	if (!g.ok) return 0;
	if (g.frameBegun) return g.active ? 1 : 0;
	pollEvents();
	if (!g.running)
	{
		static unsigned notRunning = 0;
		if (notRunning++ < 5 || notRunning % 300 == 0) LOGI("frame_begin: session not running (state %d)", (int)g.sessionState);
		return 0;
	}

	const bool tr = xr_trace();
	if (tr) LOGI("frame %u: xrWaitFrame", g.frameNo);
	XrFrameWaitInfo wi = {XR_TYPE_FRAME_WAIT_INFO};
	XrFrameState fs = {XR_TYPE_FRAME_STATE};
	if (!check(xrWaitFrame(g.session, &wi, &fs), "xrWaitFrame")) return 0;
	if (tr) LOGI("frame %u: xrWaitFrame done shouldRender=%d predictedDisplayTime=%lld period=%lld", g.frameNo,
	             (int)fs.shouldRender, (long long)fs.predictedDisplayTime, (long long)fs.predictedDisplayPeriod);
	XrFrameBeginInfo bi = {XR_TYPE_FRAME_BEGIN_INFO};
	if (!check(xrBeginFrame(g.session, &bi), "xrBeginFrame")) return 0;
	if (tr) LOGI("frame %u: xrBeginFrame ok", g.frameNo);

	g.frameBegun = true;
	g.shouldRender = fs.shouldRender == XR_TRUE;
	g.displayTime = fs.predictedDisplayTime;
	g.eyeDone[0] = g.eyeDone[1] = false;
	g.uiDrawn = false;
	g.active = false;
	if (!g.shouldRender) { LOGW("frame %u: shouldRender=0, submitting an empty frame", g.frameNo); return 0; }

	XrViewLocateInfo li = {XR_TYPE_VIEW_LOCATE_INFO};
	li.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
	li.displayTime = g.displayTime;
	li.space = g.space;
	XrViewState vs = {XR_TYPE_VIEW_STATE};
	XrView located[2] = {{XR_TYPE_VIEW}, {XR_TYPE_VIEW}};
	uint32_t cnt = 0;
	const bool locateOk = XR_SUCCEEDED(xrLocateViews(g.session, &li, &vs, 2, &cnt, located)) && cnt == 2;
	const bool poseValid = locateOk && (vs.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT);
	if (poseValid)
	{
		g.views[0] = located[0];
		g.views[1] = located[1];
		g.lastViews[0] = located[0];
		g.lastViews[1] = located[1];
		g.haveLastViews = true;
	}
	else
	{
		// Not worn / tracking not started yet: keep submitting frames from the last good (or an identity) pose so the
		// compositor leaves the loading screen.
		if (g.frameNo < 30 || g.frameNo % 300 == 0)
			LOGW("frame %u: view pose not valid (locate=%d flags=0x%x), using %s", g.frameNo, (int)locateOk, (unsigned)vs.viewStateFlags,
			     g.haveLastViews ? "last pose" : "identity");
		for (int i = 0; i < 2; i++)
		{
			if (g.haveLastViews) g.views[i] = g.lastViews[i];
			else
			{
				g.views[i] = XrView{XR_TYPE_VIEW};
				g.views[i].pose.orientation.w = 1.f;
				g.views[i].pose.position.x = i == 0 ? -0.032f : 0.032f;
				g.views[i].fov = {-0.8f, 0.8f, 0.8f, -0.8f};
			}
			if (locateOk) g.views[i].fov = located[i].fov;
		}
	}

	if (poseValid && (g.needRecenter || !g.haveOrigin))
	{
		for (int i = 0; i < 3; i++)
			g.originPos[i] = 0.5f * ((&g.views[0].pose.position.x)[i] + (&g.views[1].pose.position.x)[i]);
		g.originYaw = yawOf(g.views[0].pose.orientation);
		g.haveOrigin = true;
		g.needRecenter = false;
		LOGI("recentered: yaw %.1f deg", g.originYaw * kRad2Deg);
	}
	updateInput();
	g.active = true;

	// Once a second: head pose in Doom conventions, to compare with how the user actually moves.
	{
		static XrTime lastLog = 0;
		if (g.displayTime - lastLog > 1000000000LL)
		{
			lastLog = g.displayTime;
			DoomXrEyeView h;
			if (xr_get_eye_view(0, &h))
				LOGI("head: yaw=%.1f pitch=%.1f roll=%.1f (deg, left eye) pos fwd=%.2f right=%.2f up=%.2f m, valid=%d, fovtan L%.2f R%.2f U%.2f D%.2f",
				     h.yaw, h.pitch, h.roll, h.forward, h.right, h.up, (int)poseValid, h.tanLeft, h.tanRight, h.tanUp, h.tanDown);
		}
	}
	return 1;
}

int xr_get_eye_view(int eye, DoomXrEyeView *o)
{
	if (!g.active || eye < 0 || eye > 1 || !o) return 0;
	const XrView &v = g.views[eye];
	o->tanLeft = tanf(v.fov.angleLeft);
	o->tanRight = tanf(v.fov.angleRight);
	o->tanUp = tanf(v.fov.angleUp);
	o->tanDown = tanf(v.fov.angleDown);

	const XrQuaternionf &q = v.pose.orientation;
	Vec3 f = rotate(q, {0, 0, -1});
	Vec3 r = rotate(q, {1, 0, 0});
	float fy = f.y < -1.f ? -1.f : (f.y > 1.f ? 1.f : f.y);
	float ry = r.y < -1.f ? -1.f : (r.y > 1.f ? 1.f : r.y);
	o->yaw = wrapDeg((yawOf(q) - g.originYaw) * kRad2Deg);
	o->pitch = -asinf(fy) * kRad2Deg;
	o->roll = -asinf(ry) * kRad2Deg;

	float dx = v.pose.position.x - g.originPos[0];
	float dz = v.pose.position.z - g.originPos[2];
	float s = sinf(g.originYaw), c = cosf(g.originYaw);
	o->forward = -s * dx - c * dz;
	o->right = c * dx - s * dz;
	o->up = v.pose.position.y - g.originPos[1];
	return 1;
}

void xr_eye_blit(int eye, unsigned srcFbo, int sw, int sh)
{
	if (!g.active || eye < 0 || eye > 1) return;
	Swap &s = g.eye[eye];
	if (!acquire(s)) return;
	GLint prevRead = 0, prevDraw = 0;
	glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevRead);
	glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevDraw);
	GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
	glDisable(GL_SCISSOR_TEST);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, srcFbo);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, s.fbo);
	glBlitFramebuffer(0, 0, sw, sh, 0, 0, s.width, s.height, GL_COLOR_BUFFER_BIT,
	                  (sw == s.width && sh == s.height) ? GL_NEAREST : GL_LINEAR);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, prevRead);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prevDraw);
	if (scissor) glEnable(GL_SCISSOR_TEST);
	release(s);
	g.eyeDone[eye] = true;
}

unsigned xr_ui_target_fbo(void)
{
	if (!g.ok) return 0;
	xr_frame_begin();
	if (!g.frameBegun || !g.shouldRender) return 0;
	GLuint fbo = acquire(g.ui);
	if (fbo) g.uiDrawn = true;
	return fbo;
}

int xr_frame_end(int uiWanted)
{
	if (!g.ok || !g.frameBegun) return 0;

	bool showUi = uiWanted && g.uiDrawn && g.shouldRender;
	if (showUi && g.input.uiHit && g.ui.acquired)
	{
		// Pointer cursor drawn straight into the screen image: a white square with a red centre.
		GLint prev = 0;
		glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prev);
		GLboolean sc = glIsEnabled(GL_SCISSOR_TEST);
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, g.ui.fbo);
		glEnable(GL_SCISSOR_TEST);
		const int cx = (int)(g.input.uiU * g.ui.width), cy = (int)((1.f - g.input.uiV) * g.ui.height);
		static const GLfloat white[4] = {1, 1, 1, 1}, red[4] = {1, 0.1f, 0.1f, 1};
		glScissor(cx - 9, cy - 9, 18, 18);
		glClearBufferfv(GL_COLOR, 0, white);
		glScissor(cx - 5, cy - 5, 10, 10);
		glClearBufferfv(GL_COLOR, 0, red);
		if (!sc) glDisable(GL_SCISSOR_TEST);
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prev);
	}
	release(g.ui);
	release(g.panel);

	if (showUi && !g.uiWasShown)
	{
		// Float the screen in front of the head, upright and facing the user, about 2 m wide at 2 m.
		Vec3 head = {0.5f * (g.views[0].pose.position.x + g.views[1].pose.position.x),
		             0.5f * (g.views[0].pose.position.y + g.views[1].pose.position.y),
		             0.5f * (g.views[0].pose.position.z + g.views[1].pose.position.z)};
		float yaw = yawOf(g.views[0].pose.orientation);
		const float dist = 2.0f;
		g.uiPose.position = {head.x - sinf(yaw) * dist, head.y, head.z - cosf(yaw) * dist};
		g.uiPose.orientation = {0.f, sinf(yaw * 0.5f), 0.f, cosf(yaw * 0.5f)};
		g.uiW = 2.0f;
		g.uiH = g.uiW * (float)g.ui.height / (float)g.ui.width;
	}
	g.uiWasShown = showUi;

	XrCompositionLayerProjectionView pv[2];
	XrCompositionLayerProjection proj = {XR_TYPE_COMPOSITION_LAYER_PROJECTION};
	XrCompositionLayerQuad quad = {XR_TYPE_COMPOSITION_LAYER_QUAD};
	const XrCompositionLayerBaseHeader *layers[3];
	uint32_t nl = 0;

	if (g.active && g.eyeDone[0] && g.eyeDone[1])
	{
		for (int i = 0; i < 2; i++)
		{
			pv[i] = {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};
			pv[i].pose = g.views[i].pose;
			pv[i].fov = g.views[i].fov;
			pv[i].subImage.swapchain = g.eye[i].handle;
			pv[i].subImage.imageRect.offset = {0, 0};
			pv[i].subImage.imageRect.extent = {g.eye[i].width, g.eye[i].height};
		}
		proj.space = g.space;
		proj.viewCount = 2;
		proj.views = pv;
		layers[nl++] = (const XrCompositionLayerBaseHeader *)&proj;
	}
	if (showUi)
	{
		quad.space = g.space;
		quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
		quad.subImage.swapchain = g.ui.handle;
		quad.subImage.imageRect.offset = {0, 0};
		quad.subImage.imageRect.extent = {g.ui.width, g.ui.height};
		quad.pose = g.uiPose;
		quad.size = {g.uiW, g.uiH};
		layers[nl++] = (const XrCompositionLayerBaseHeader *)&quad;
	}

	// Head-locked HUD (gameplay): the screen image with a transparent background in front of the face.
	XrCompositionLayerQuad hudQuad = {XR_TYPE_COMPOSITION_LAYER_QUAD};
	if (!showUi && g.hudWanted && g.uiDrawn && g.shouldRender && g.viewSpace != XR_NULL_HANDLE)
	{
		hudQuad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
		hudQuad.space = g.viewSpace;
		hudQuad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
		hudQuad.subImage.swapchain = g.ui.handle;
		hudQuad.subImage.imageRect.offset = {0, 0};
		hudQuad.subImage.imageRect.extent = {g.ui.width, g.ui.height};
		hudQuad.pose.orientation = {0.f, 0.f, 0.f, 1.f};
		hudQuad.pose.position = {0.f, -g.hudDrop, -g.hudDist};
		hudQuad.size = {g.hudW, g.hudW * (float)g.ui.height / (float)g.ui.width};
		if (g.hudWrist && g.aimValid[0])
		{
			// Wrist HUD: a small panel just above the left controller, turned towards the head (upright).
			const XrPosef &a = g.aim[0];
			const Vec3 up = rotate(a.orientation, {0, 1, 0});
			const Vec3 c = {a.position.x + up.x * 0.12f, a.position.y + up.y * 0.12f, a.position.z + up.z * 0.12f};
			const Vec3 head = {0.5f * (g.views[0].pose.position.x + g.views[1].pose.position.x),
			                   0.5f * (g.views[0].pose.position.y + g.views[1].pose.position.y),
			                   0.5f * (g.views[0].pose.position.z + g.views[1].pose.position.z)};
			Vec3 z = {head.x - c.x, head.y - c.y, head.z - c.z};
			const float zl = sqrtf(z.x * z.x + z.y * z.y + z.z * z.z);
			if (zl > 1e-3f)
			{
				z = {z.x / zl, z.y / zl, z.z / zl};
				Vec3 x = {z.z, 0.f, -z.x};	// cross(world up, z)
				const float xl = sqrtf(x.x * x.x + x.z * x.z);
				if (xl > 1e-3f)
				{
					x = {x.x / xl, 0.f, x.z / xl};
					const Vec3 y = {z.y * x.z - z.z * x.y, z.z * x.x - z.x * x.z, z.x * x.y - z.y * x.x};	// cross(z, x)
					hudQuad.space = g.space;
					hudQuad.pose.orientation = quatFromBasis(x, y, z);
					hudQuad.pose.position = {c.x, c.y, c.z};
					hudQuad.size = {g.hudWristW, g.hudWristW * (float)g.ui.height / (float)g.ui.width};
				}
			}
		}
		layers[nl++] = (const XrCompositionLayerBaseHeader *)&hudQuad;
	}

	// Interactive forearm HUD panel above the left controller (drawn by the game, touched with the right hand).
	XrCompositionLayerQuad panelQuad = {XR_TYPE_COMPOSITION_LAYER_QUAD};
	XrPosef pp;
	if (!showUi && g.panelOn && g.panelDrawn && g.shouldRender && computePanelPose(g.panelW, &pp))
	{
		panelQuad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
		panelQuad.space = g.space;
		panelQuad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
		panelQuad.subImage.swapchain = g.panel.handle;
		panelQuad.subImage.imageRect.offset = {0, 0};
		panelQuad.subImage.imageRect.extent = {g.panel.width, g.panel.height};
		panelQuad.pose = pp;
		panelQuad.size = {g.panelW, g.panelW * (float)g.panel.height / (float)g.panel.width};
		layers[nl++] = (const XrCompositionLayerBaseHeader *)&panelQuad;
	}

	// Laser pointer: a thin quad from the pointing hand to the screen, drawn after (in front of) it.
	XrCompositionLayerQuad laserQuad = {XR_TYPE_COMPOSITION_LAYER_QUAD};
	if (showUi && g.laserHit && g.laser.handle != XR_NULL_HANDLE && g.aimValid[g.input.uiHand])
	{
		if (GLuint fbo = acquire(g.laser))
		{
			GLint prev = 0;
			glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prev);
			GLboolean sc = glIsEnabled(GL_SCISSOR_TEST);
			glDisable(GL_SCISSOR_TEST);
			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo);
			static const GLfloat cyan[4] = {0.3f, 0.9f, 1.f, 1.f};
			glClearBufferfv(GL_COLOR, 0, cyan);
			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prev);
			if (sc) glEnable(GL_SCISSOR_TEST);
			release(g.laser);

			const XrPosef &a = g.aim[g.input.uiHand];
			Vec3 dir = rotate(a.orientation, {0, 0, -1});
			const float len = g.laserLen;
			Vec3 mid = {a.position.x + dir.x * len * 0.5f, a.position.y + dir.y * len * 0.5f, a.position.z + dir.z * len * 0.5f};
			Vec3 toHead = {g.views[0].pose.position.x - mid.x, g.views[0].pose.position.y - mid.y, g.views[0].pose.position.z - mid.z};
			float dd = toHead.x * dir.x + toHead.y * dir.y + toHead.z * dir.z;
			Vec3 z = {toHead.x - dir.x * dd, toHead.y - dir.y * dd, toHead.z - dir.z * dd};
			float zl = sqrtf(z.x * z.x + z.y * z.y + z.z * z.z);
			if (zl > 1e-4f)
			{
				z = {z.x / zl, z.y / zl, z.z / zl};
				Vec3 y = {z.y * dir.z - z.z * dir.y, z.z * dir.x - z.x * dir.z, z.x * dir.y - z.y * dir.x};
				// Rotation matrix with columns x = dir, y, z -> quaternion.
				float m00 = dir.x, m10 = dir.y, m20 = dir.z, m01 = y.x, m11 = y.y, m21 = y.z, m02 = z.x, m12 = z.y, m22 = z.z;
				XrQuaternionf q;
				float tr = m00 + m11 + m22;
				if (tr > 0) { float sq = sqrtf(tr + 1.f) * 2; q = {(m21 - m12) / sq, (m02 - m20) / sq, (m10 - m01) / sq, 0.25f * sq}; }
				else if (m00 > m11 && m00 > m22) { float sq = sqrtf(1.f + m00 - m11 - m22) * 2; q = {0.25f * sq, (m01 + m10) / sq, (m02 + m20) / sq, (m21 - m12) / sq}; }
				else if (m11 > m22) { float sq = sqrtf(1.f + m11 - m00 - m22) * 2; q = {(m01 + m10) / sq, 0.25f * sq, (m12 + m21) / sq, (m02 - m20) / sq}; }
				else { float sq = sqrtf(1.f + m22 - m00 - m11) * 2; q = {(m02 + m20) / sq, (m12 + m21) / sq, 0.25f * sq, (m10 - m01) / sq}; }
				laserQuad.space = g.space;
				laserQuad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
				laserQuad.subImage.swapchain = g.laser.handle;
				laserQuad.subImage.imageRect.offset = {0, 0};
				laserQuad.subImage.imageRect.extent = {g.laser.width, g.laser.height};
				laserQuad.pose.orientation = q;
				laserQuad.pose.position = {mid.x, mid.y, mid.z};
				laserQuad.size = {len, 0.006f};
				layers[nl++] = (const XrCompositionLayerBaseHeader *)&laserQuad;
			}
		}
	}

	if (nl == 0 && g.shouldRender && g.frameNo < 600)
	{
		// The compositor only leaves its loading screen once it receives a layer: show a black screen if nothing else.
		if (GLuint fbo = acquire(g.ui))
		{
			GLint prev = 0;
			glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prev);
			GLboolean sc = glIsEnabled(GL_SCISSOR_TEST);
			glDisable(GL_SCISSOR_TEST);
			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo);
			static const GLfloat black[4] = {0, 0, 0, 1};
			glClearBufferfv(GL_COLOR, 0, black);
			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prev);
			if (sc) glEnable(GL_SCISSOR_TEST);
			release(g.ui);
			showUi = true;
			if (!g.uiWasShown) { g.uiH = 1.5f; g.uiW = g.uiH * (float)g.ui.width / (float)g.ui.height; g.uiPose = XrPosef{{0, 0, 0, 1}, {0, 0, -1.5f}}; }
			g.uiWasShown = true;
			quad.space = g.space;
			quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
			quad.subImage.swapchain = g.ui.handle;
			quad.subImage.imageRect.offset = {0, 0};
			quad.subImage.imageRect.extent = {g.ui.width, g.ui.height};
			quad.pose = g.uiPose;
			quad.size = {g.uiW, g.uiH};
			layers[nl++] = (const XrCompositionLayerBaseHeader *)&quad;
		}
	}

	XrFrameEndInfo ei = {XR_TYPE_FRAME_END_INFO};
	ei.displayTime = g.displayTime;
	ei.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
	ei.layerCount = nl;
	ei.layers = layers;
	XrResult er = xrEndFrame(g.session, &ei);
	check(er, "xrEndFrame");
	if (xr_trace())
		LOGI("frame %u: xrEndFrame result=%d layerCount=%u active=%d eyes=%d/%d ui=%d(wanted %d drawn %d)", g.frameNo, (int)er, nl,
		     (int)g.active, (int)g.eyeDone[0], (int)g.eyeDone[1], (int)showUi, uiWanted, (int)g.uiDrawn);
	g.frameNo++;

	g.hudWanted = false;
	g.panelShown = g.panelDrawn;
	g.panelDrawn = false;
	g.frameBegun = false;
	g.active = false;
	return 1;
}

// Asks the Java activity to switch back to the 2D library panel (menu item "Return to library").
void xr_open_library(void)
{
	JNIEnv *env = (JNIEnv *)SDL_AndroidGetJNIEnv();
	jobject activity = (jobject)SDL_AndroidGetActivity();
	if (!env || !activity)
	{
		LOGE("xr_open_library: no JNI environment/activity");
		return;
	}
	jclass cls = env->GetObjectClass(activity);
	jmethodID mid = cls ? env->GetMethodID(cls, "returnToLibrary", "()V") : nullptr;
	if (!mid)
	{
		env->ExceptionClear();
		LOGE("xr_open_library: DoomXRActivity.returnToLibrary not found");
	}
	else
	{
		env->CallVoidMethod(activity, mid);
	}
	if (cls) env->DeleteLocalRef(cls);
	env->DeleteLocalRef(activity);
}

} // extern "C"
