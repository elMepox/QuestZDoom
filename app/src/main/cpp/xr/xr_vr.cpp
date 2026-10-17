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

	// recentering
	bool needRecenter = true;
	bool haveOrigin = false;
	float originPos[3] = {0, 0, 0};
	float originYaw = 0; // radians, OpenXR convention (see yawOf)

	// UI quad placement
	bool uiWasShown = false;
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

extern "C" {

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

	g.ok = true;
	LOGI("OpenXR initialized");
	return 1;
}

void xr_recenter(void) { g.needRecenter = true; }

int xr_frame_active(void) { return g.active ? 1 : 0; }

int xr_frame_begin(void)
{
	if (!g.ok) return 0;
	if (g.frameBegun) return g.active ? 1 : 0;
	pollEvents();
	if (!g.running) return 0;

	XrFrameWaitInfo wi = {XR_TYPE_FRAME_WAIT_INFO};
	XrFrameState fs = {XR_TYPE_FRAME_STATE};
	if (!check(xrWaitFrame(g.session, &wi, &fs), "xrWaitFrame")) return 0;
	XrFrameBeginInfo bi = {XR_TYPE_FRAME_BEGIN_INFO};
	if (!check(xrBeginFrame(g.session, &bi), "xrBeginFrame")) return 0;

	g.frameBegun = true;
	g.shouldRender = fs.shouldRender == XR_TRUE;
	g.displayTime = fs.predictedDisplayTime;
	g.eyeDone[0] = g.eyeDone[1] = false;
	g.uiDrawn = false;
	g.active = false;
	if (!g.shouldRender) return 0;

	XrViewLocateInfo li = {XR_TYPE_VIEW_LOCATE_INFO};
	li.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
	li.displayTime = g.displayTime;
	li.space = g.space;
	XrViewState vs = {XR_TYPE_VIEW_STATE};
	g.views[0] = g.views[1] = XrView{XR_TYPE_VIEW};
	uint32_t cnt = 0;
	if (!check(xrLocateViews(g.session, &li, &vs, 2, &cnt, g.views), "xrLocateViews") || cnt != 2) return 0;
	if (!(vs.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT)) return 0;

	if (g.needRecenter || !g.haveOrigin)
	{
		for (int i = 0; i < 3; i++)
			g.originPos[i] = 0.5f * ((&g.views[0].pose.position.x)[i] + (&g.views[1].pose.position.x)[i]);
		g.originYaw = yawOf(g.views[0].pose.orientation);
		g.haveOrigin = true;
		g.needRecenter = false;
		LOGI("recentered: yaw %.1f deg", g.originYaw * kRad2Deg);
	}
	g.active = true;
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
	release(g.ui);

	bool showUi = uiWanted && g.uiDrawn && g.shouldRender && g.active;
	if (showUi && !g.uiWasShown)
	{
		// Float the screen in front of the head, upright and facing the user.
		Vec3 head = {0.5f * (g.views[0].pose.position.x + g.views[1].pose.position.x),
		             0.5f * (g.views[0].pose.position.y + g.views[1].pose.position.y),
		             0.5f * (g.views[0].pose.position.z + g.views[1].pose.position.z)};
		float yaw = yawOf(g.views[0].pose.orientation);
		const float dist = 1.5f;
		g.uiPose.position = {head.x - sinf(yaw) * dist, head.y, head.z - cosf(yaw) * dist};
		g.uiPose.orientation = {0.f, sinf(yaw * 0.5f), 0.f, cosf(yaw * 0.5f)};
		g.uiH = 1.5f;
		g.uiW = g.uiH * (float)g.ui.width / (float)g.ui.height;
	}
	g.uiWasShown = showUi;

	XrCompositionLayerProjectionView pv[2];
	XrCompositionLayerProjection proj = {XR_TYPE_COMPOSITION_LAYER_PROJECTION};
	XrCompositionLayerQuad quad = {XR_TYPE_COMPOSITION_LAYER_QUAD};
	const XrCompositionLayerBaseHeader *layers[2];
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

	XrFrameEndInfo ei = {XR_TYPE_FRAME_END_INFO};
	ei.displayTime = g.displayTime;
	ei.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
	ei.layerCount = nl;
	ei.layers = layers;
	check(xrEndFrame(g.session, &ei), "xrEndFrame");

	g.frameBegun = false;
	g.active = false;
	return 1;
}

} // extern "C"
