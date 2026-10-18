// A hand built from boxes, posed by how far the grip and the trigger are pressed. No assets: the shape is
// generated, so a better hand can replace it later without touching the renderer (which only needs the boxes).
//
// Hand frame, in meters: +x right, +y up, +z forward (the controller's aim direction), origin at the middle of
// the palm. The right hand's palm faces -x and its fingers curl towards -x; the left hand is the mirror image.
#pragma once

#include <math.h>

struct XrHandBox
{
	float c[3];		// centre
	float ax[3][3];	// three unit axes
	float h[3];		// half extent along each axis
};

enum { XR_HAND_MAX_BOXES = 24 };

namespace xrhand
{
	struct V3 { float x, y, z; };
	static inline V3 v3(float x, float y, float z) { V3 v = {x, y, z}; return v; }
	static inline V3 add(V3 a, V3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
	static inline V3 mul(V3 a, float s) { return v3(a.x * s, a.y * s, a.z * s); }
	static inline V3 cross(V3 a, V3 b) { return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
	static inline V3 norm(V3 a)
	{
		const float l = sqrtf(a.x * a.x + a.y * a.y + a.z * a.z);
		return l > 1e-6f ? mul(a, 1.f / l) : v3(0, 0, 1);
	}
	static inline V3 lerp(V3 a, V3 b, float t) { return add(mul(a, 1.f - t), mul(b, t)); }

	struct Builder
	{
		XrHandBox *out;
		int max, n;
		float mirror;	// +1 right hand, -1 left hand

		void box(V3 c, V3 along, V3 side, V3 up, float hlen, float hside, float hup)
		{
			if (n >= max) return;
			XrHandBox &b = out[n++];
			b.c[0] = c.x * mirror; b.c[1] = c.y; b.c[2] = c.z;
			const V3 a[3] = {side, up, along};
			for (int i = 0; i < 3; i++) { b.ax[i][0] = a[i].x * mirror; b.ax[i][1] = a[i].y; b.ax[i][2] = a[i].z; }
			b.h[0] = hside; b.h[1] = hup; b.h[2] = hlen;
		}

		// A box whose long axis is d; side/up are made perpendicular to it.
		void bone(V3 start, V3 d, float len, float half)
		{
			d = norm(d);
			V3 side = cross(v3(0, 1, 0), d);
			if (side.x * side.x + side.y * side.y + side.z * side.z < 1e-4f) side = v3(1, 0, 0);
			side = norm(side);
			const V3 up = cross(d, side);
			box(add(start, mul(d, len * 0.5f)), d, side, up, len * 0.5f, half, half);
		}
	};
}

// indexCurl and gripCurl are 0 (open) .. 1 (closed). Returns the number of boxes written.
static inline int xr_hand_boxes(bool left, float indexCurl, float gripCurl, XrHandBox *out, int max)
{
	using namespace xrhand;
	Builder b = {out, max, 0, left ? -1.f : 1.f};
	const float deg = 0.017453292f;
	auto clamp01 = [](float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); };
	indexCurl = clamp01(indexCurl);
	gripCurl = clamp01(gripCurl);

	// Palm and wrist.
	b.box(v3(0, 0, 0), v3(0, 0, 1), v3(1, 0, 0), v3(0, 1, 0), 0.045f, 0.017f, 0.045f);
	b.box(v3(0.002f, 0, -0.085f), v3(0, 0, 1), v3(1, 0, 0), v3(0, 1, 0), 0.040f, 0.020f, 0.028f);

	// Four fingers: they start on the front edge of the palm, stacked along y (index on top), each bending in
	// the x-z plane towards the palm side. Each joint turns a bit further than the one before.
	static const float fy[4] = {0.034f, 0.012f, -0.010f, -0.032f};
	static const float seg[4][3] = {{0.040f, 0.024f, 0.020f}, {0.044f, 0.027f, 0.021f}, {0.040f, 0.025f, 0.020f}, {0.032f, 0.019f, 0.017f}};
	static const float thick[4] = {0.0085f, 0.0088f, 0.0083f, 0.0075f};
	static const float stagger[4] = {1.f, 1.f, 0.97f, 0.92f};
	static const float jointDeg[3] = {75.f, 100.f, 70.f};
	for (int f = 0; f < 4; f++)
	{
		const float curl = f == 0 ? indexCurl : gripCurl * stagger[f];
		V3 p = v3(-0.002f, fy[f], 0.045f);
		float a = 0.f;
		for (int j = 0; j < 3; j++)
		{
			a += curl * jointDeg[j] * deg;
			const V3 d = v3(-sinf(a), 0.f, cosf(a));
			b.bone(p, d, seg[f][j], thick[f]);
			p = add(p, mul(d, seg[f][j]));
		}
	}

	// Thumb: from the top of the palm, forwards and up when open, folding towards the palm side when the grip closes.
	const float tc = gripCurl * 0.65f;
	V3 p = v3(-0.008f, 0.040f, 0.018f);
	const V3 d0 = norm(v3(-0.10f, 0.55f, 0.80f));
	b.bone(p, lerp(d0, norm(v3(-0.75f, 0.10f, 0.40f)), tc), 0.034f, 0.0105f);
	p = add(p, mul(lerp(d0, norm(v3(-0.75f, 0.10f, 0.40f)), tc), 0.034f));
	b.bone(p, lerp(d0, norm(v3(-0.95f, -0.15f, 0.10f)), tc), 0.028f, 0.0095f);
	return b.n;
}
